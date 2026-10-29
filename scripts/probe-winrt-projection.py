#!/usr/bin/env python3
"""Verify metadata generation and a real Windows.dll ABI projection under Wine Mono."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import struct
import subprocess

# IIDs and vtable slots taken from the Windows SDK's ABI headers
# (windows.applicationmodel.h, windows.applicationmodel.activation.h, windows.ui.xaml.h),
# independent of the metadata the generator reads. Slots follow IInspectable's six.
HEADER_IIDS = {
    "IPackageStatics": "4e534bdf-2960-4878-97a4-9624deb72f2d", "IPackage": "163c792f-bd75-413c-bf23-b1fe7b95d825",
    "IPackageId": "1adb665e-37c7-4790-9980-dd7ae74e8bb2", "ILaunchActivatedEventArgs": "fbc93e26-a14a-4b4f-82b0-33bed920af52",
    "IApplication": "74b861a1-7487-46a9-9a6e-c78b512726c5", "IApplicationStatics": "06499997-f7b4-45fe-b763-7577d1d3cb4a",
    "IApplicationFactory": "93bbe361-be5a-4ee3-b4a3-95118dc97a89", "IApplicationOverrides": "25f99ff7-9347-459a-9fac-b2d0e11c1a0f",
    "IApplicationInitializationCallback": "b6351c55-c284-46e4-8310-fb0967fab76f",
    "IApplicationInitializationCallbackParams": "751b792e-5772-4488-8b87-f547faa64474",
    "IActivatedEventArgs": "cf651713-cd08-4fd8-b697-a281b6544e2e", "IWindowStatics": "93328409-4ea1-4afa-83dc-0c4e73e88bb1",
    "IWindow": "3276167d-c9f6-462d-9de2-ae4c1fd8c2e5", "IFrameFactory": "02ee93d4-448e-469e-9799-0a8a1f70f171"}
HEADER_SLOTS = [
    "Windows.ApplicationModel.IPackageStatics::get_Current, slot 6", "Windows.ApplicationModel.IPackage::get_Id, slot 6",
    "Windows.ApplicationModel.IPackageId::get_Name, slot 6", "Windows.ApplicationModel.IPackageId::get_Architecture, slot 8",
    "Windows.ApplicationModel.IPackageId::get_ResourceId, slot 9", "Windows.ApplicationModel.IPackageId::get_Publisher, slot 10",
    "Windows.ApplicationModel.Activation.ILaunchActivatedEventArgs::get_Arguments, slot 6",
    "Windows.UI.Xaml.IApplicationFactory::CreateInstance, slot 6", "Windows.UI.Xaml.IApplicationStatics::get_Current, slot 6",
    "Windows.UI.Xaml.IApplicationStatics::Start, slot 7", "Windows.UI.Xaml.IApplication::Exit, slot 17",
    "Windows.UI.Xaml.IApplicationOverrides::OnLaunched, slot 7", "Windows.UI.Xaml.IApplicationOverrides::OnActivated, slot 6",
    "Windows.ApplicationModel.Activation.IActivatedEventArgs::get_Kind, slot 6",
    "Windows.ApplicationModel.Activation.IActivatedEventArgs::get_PreviousExecutionState, slot 7",
    "Windows.UI.Xaml.IApplication::add_Suspending, slot 13", "Windows.UI.Xaml.IApplication::remove_Suspending, slot 14",
    "Windows.UI.Xaml.IWindowStatics::get_Current, slot 6", "Windows.UI.Xaml.IWindow::get_Content, slot 8",
    "Windows.UI.Xaml.IWindow::put_Content, slot 9", "Windows.UI.Xaml.IWindow::Activate, slot 20",
    "Windows.UI.Xaml.Controls.IFrameFactory::CreateInstance, slot 6"]
# The runtime's dispatcher context calls CoreWindow and CoreDispatcher directly
# (windows.ui.core.h): each IID and slot, and the call that uses the slot.
HEADER_RUNTIME = [
    "// Windows.UI.Core.ICoreWindowStatic::GetForCurrentThread, slot 6", 'Guid("4d239005-3c2a-41b1-9022-536bb9cf93b1")',
    "statics.Object(6)", "// Windows.UI.Core.ICoreWindow::get_Dispatcher, slot 9", 'Guid("79b9d5f2-879e-4b89-b798-79e47598030c")',
    "core.Object(9)", "// Windows.UI.Core.ICoreDispatcher::RunAsync, slot 8", "dispatcher.Call(8, 0, ",
    "// Windows.UI.Core.IDispatchedHandler::Invoke, slot 3", 'Guid("d1f276c4-98d8-4636-bf49-eb79507548e9"), false, invoke']
# Delegates are IUnknown-based: Invoke is their only method, at slot 3.
HEADER_DELEGATES = ['NativeInterface(new global::System.Guid("' + iid + '"), false, invoke)'
                    for iid in ("b6351c55-c284-46e4-8310-fb0967fab76f", "23429465-e36a-40e2-b139-a4704602a6e1")]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("wine", "prefix", "metadata", "shim", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--game", type=Path, help="copy and launch this installed game after completion")
    parser.add_argument("--expected-game-error", help="required named failure in the copied game managed.log")
    parser.add_argument("--cecil", type=Path)
    parser.add_argument("--runtime-case", choices=("inspectable", "identity", "stale", "invocation", "reentrant"))
    parser.add_argument("--consumer", type=Path, action="append", default=[])
    parser.add_argument("--architecture", choices=("x86", "x64"), default="x86")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    wine, prefix, metadata, shim, output = (getattr(args, name).resolve()
                                          for name in ("wine", "prefix", "metadata", "shim", "output"))
    if args.game:
        args.game = args.game.resolve()
        if not args.consumer or not args.expected_game_error:
            parser.error("--game requires --consumer inputs and --expected-game-error")
        if args.game == output or args.game in output.parents or output in args.game.parents:
            parser.error("source game and output directories must be disjoint")
    output.mkdir(parents=True, exist_ok=True)
    libraries = list((wine.parent.parent / "share/wine/mono").glob("wine-mono-*/lib/mono/4.8-api"))
    if len(libraries) != 1:
        parser.error("expected one bundled Wine Mono 4.8 API directory")
    references = libraries[0]
    mono = references.parent
    cecils = [args.cecil.resolve()] if args.cecil else list((mono / "gac/Mono.Cecil").glob("0.11.*__*/Mono.Cecil.dll"))
    if len(cecils) != 1:
        parser.error("expected one bundled Mono.Cecil 0.11 library")
    image = shim.read_bytes()
    pe = struct.unpack_from("<I", image, 0x3c)[0]
    if (image[pe:pe + 4] != b"PE\0\0" or struct.unpack_from("<H", image, pe + 4)[0]
            != (0x14c if args.architecture == "x86" else 0x8664)):
        parser.error("shim architecture does not match the requested fixture")
    shutil.copyfile(cecils[0], output / "Mono.Cecil.dll")
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG="-all", MVK_CONFIG_LOG_LEVEL="0")
    for key in ("WINE_MONO_AOT", "WINEDLLOVERRIDES", "MONO_PATH", "MONO_ENV_OPTIONS"):
        env.pop(key, None)
    results = []

    def windows(path):
        return "Z:" + str(path).replace("/", "\\")

    def require(value, message):
        if not value:
            raise RuntimeError(message)

    def run(name, command, cwd=output, succeeds=True, marker=None, evidence=None):
        log = output / (name + ".log")
        with log.open("w") as stream:
            child = subprocess.Popen([str(wine), *command], env=env, cwd=cwd,
                                     stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = child.wait(timeout=45)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()
                code = "timeout"
        text = log.read_text(errors="replace")
        if evidence is not None:
            require(evidence.is_file(), name + " did not produce " + str(evidence))
            text += evidence.read_text(errors="replace")
        passed = (code == 0 if succeeds else code not in (0, "timeout"))
        passed = passed and (marker is None or marker in text)
        row = dict(case=name, exit=code, passed=passed, log=str(log))
        results.append(row)
        print(json.dumps(row), flush=True)
        require(passed, name + " failed: " + text[-2400:])
        return text

    def compile_file(name, sources, extra=()):
        destination = output / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        command = [windows(mono / "4.5/mcs.exe"), "/nologo", "/noconfig", "/nostdlib+", "/warnaserror+",
                   "/langversion:5", "/platform:" + args.architecture, "/out:" + windows(destination)]
        command += ["/r:" + windows(references / name)
                    for name in ("mscorlib.dll", "System.dll", "System.Core.dll", "System.Xml.dll")]
        run("compile-" + name.replace("/", "-"), command + list(extra) + [windows(path) for path in sources])
        return destination

    def fixture(name, executable, identity, publisher):
        folder = output / name
        folder.mkdir(exist_ok=True)
        shutil.copyfile(shim, folder / "appxportable.dll")
        (folder / "AppxManifest.xml").write_text(
            '<Package><Identity Name="' + identity + '" Publisher="' + publisher +
            '" Version="1.0.0.0" ProcessorArchitecture="' + args.architecture +
            '"/><Applications><Application Id="Test" Executable="' + executable + '"/></Applications></Package>')
        return folder

    try:
        if args.runtime_case:
            executable = compile_file("runtime.exe", [repo / "host/winrt/abi.cs", repo / "host/winrt/ccw.cs", repo / "host/winrt/dispatcher.cs",
                                      repo / "tests/winrt_projection/runtime.cs"])
            run("runtime-" + args.runtime_case, [executable.name, args.runtime_case], marker="PASS: runtime " + args.runtime_case)
            return 0
        runtime = compile_file("runtime.exe", [repo / "host/winrt/abi.cs", repo / "host/winrt/ccw.cs", repo / "host/winrt/dispatcher.cs",
                               repo / "tests/winrt_projection/runtime.cs"])
        for case in ("inspectable", "identity", "stale", "invocation", "reentrant"):
            run("runtime-" + case, [runtime.name, case], marker="PASS: runtime " + case)
        # The earlier bridge remains an independent native ownership/HRESULT check.
        bridge = compile_file("AppxPortable.WinRT.dll", [repo / "host/winrt" / name
                              for name in ("abi.cs", "ccw.cs", "dispatcher.cs", "package.cs", "assembly.cs")], ["/target:library"])
        legacy = compile_file("managed-winrt-bridge.exe", [repo / "tests/managed_winrt_bridge.cs"], ["/r:" + windows(bridge)])
        folder = fixture("legacy", legacy.name, "ManagedWinrtBridgeTest", "CN=bridge-đá-💎")
        for path in (bridge, legacy):
            shutil.copyfile(path, folder / path.name)
        run("legacy-bridge", [legacy.name], cwd=folder, marker="PASS: managed WinRT activation")
        reference = compile_file("reference/Windows.dll", [repo / "tests/winrt_projection/reference.cs"], ["/target:library"])
        consumer = compile_file("consumer.exe", [repo / "tests/winrt_projection/consumer.cs"], ["/r:" + windows(reference)])
        marker = compile_file("mark-reference.exe", [repo / "tests/winrt_projection/mark_reference.cs"], ["/r:Mono.Cecil.dll"])
        run("mark-reference", [marker.name, consumer.name, "consumer-winrt.exe"], marker="flags=WindowsRuntime")
        folder = fixture("fixture", consumer.name, "GeneratedProjectionTest", "CN=projection-đá-💎")
        shutil.copyfile(output / "consumer-winrt.exe", folder / consumer.name)
        shutil.copyfile(reference, folder / "Windows.dll")
        run("unimplemented-contract", [consumer.name], cwd=folder, succeeds=False,
            marker="NotImplementedException: Windows.ApplicationModel.Package.Current")
        generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs"], ["/r:Mono.Cecil.dll"])
        generated = output / "Windows.generated.cs"
        surface = repo / "host/winrt/bootstrap.xml"
        run("generate", [generator.name, windows(metadata), windows(surface), windows(generated)])
        text = generated.read_text(encoding="utf-8")
        missing = [name for name, iid in HEADER_IIDS.items() if 'System.Guid("' + iid + '")' not in text]
        missing += [slot for slot in HEADER_SLOTS if "// " + slot + "\n" not in text]
        missing += [delegate for delegate in HEADER_DELEGATES if delegate not in text]
        runtime_source = (repo / "host/winrt/dispatcher.cs").read_text(encoding="utf-8")
        missing += [value for value in HEADER_RUNTIME if value not in runtime_source]
        results.append(dict(case="header-values", missing=missing, passed=not missing))
        require(not missing, "generated IIDs or slots differ from the SDK headers: " + ", ".join(missing))
        projection = compile_file("projection/Windows.dll", [repo / "host/winrt/abi.cs", repo / "host/winrt/ccw.cs", repo / "host/winrt/dispatcher.cs", generated], ["/target:library"])
        shutil.copyfile(projection, folder / "Windows.dll")
        run("generated-contract", [consumer.name], cwd=folder, marker="PASS: generated Windows projection")
        invalid = [
            ("unknown-member", '<Class Name="Windows.ApplicationModel.Package"><Unknown Name="NoOp" /></Class>', "Unsupported member kind"),
            ("duplicate-property", '<Class Name="Windows.ApplicationModel.Package"><Property Name="Current" /><Property Name="Current" /></Class>', "Duplicate property"),
            ("unsupported-return", '<Class Name="Windows.ApplicationModel.Package"><Property Name="InstalledLocation" /></Class>', "Windows.Storage.StorageFolder"),
        ]
        for label, body, expected in invalid:
            bad = output / (label + ".xml")
            bad.write_text("<Projection>" + body + "</Projection>")
            target = output / (label + ".cs")
            if target.exists():
                target.unlink()
            run(label, [generator.name, windows(metadata), windows(bad), windows(target)], succeeds=False, marker=expected)
            require(not target.exists(), "invalid generation left a usable output: " + label)
        mutator = compile_file("mutate-metadata.exe", [repo / "tests/winrt_projection/mutate_metadata.cs"], ["/r:Mono.Cecil.dll"])
        mutated = output / "mutated-metadata"
        mutation = run("mutate-metadata", [mutator.name, windows(metadata), windows(mutated)])
        changed = re.search(r"Changed IID first word=([0-9a-f]{8})", mutation).group(1)
        target = output / "Mutated.generated.cs"
        run("generate-mutated", [generator.name, windows(mutated), windows(surface), windows(target)])
        text = target.read_text()
        require(changed + "-37c7-4790-9980-dd7ae74e8bb2" in text, "IID did not follow metadata")
        require("native.String(7)" in text, "method slot did not follow metadata")
        mutant = compile_file("mutant/Windows.dll", [repo / "host/winrt/abi.cs", repo / "host/winrt/ccw.cs", repo / "host/winrt/dispatcher.cs", target], ["/target:library"])
        try:
            shutil.copyfile(mutant, folder / "Windows.dll")
            run("changed-iid-rejected", [consumer.name], cwd=folder, succeeds=False, marker="80004002")
        finally:
            shutil.copyfile(projection, folder / "Windows.dll")
        application = compile_file("application.exe", [repo / "tests/winrt_projection/application.cs"], ["/r:" + windows(reference)])
        run("mark-application", [marker.name, application.name, "application-winrt.exe"], marker="flags=WindowsRuntime")
        app = fixture("application", application.name, "ApplicationProjectionTest", "CN=projection")
        shutil.copyfile(output / "application-winrt.exe", app / application.name)
        shutil.copyfile(projection, app / "Windows.dll")
        run("application-launch", [application.name], cwd=app, marker="PASS: managed App.OnLaunched ran on the shim view thread")
        launch = (output / "application-launch.log").read_text(errors="replace")
        require("PASS: Suspending raised on the managed App" in launch and "UNUSED HANDLER" not in launch,
                "Suspending must reach the subscribed handler and not the removed one")
        run("application-exception", [application.name, "throw"], cwd=app, marker="PASS: OnLaunched exception crossed the native boundary; callback released")
        unimplemented = compile_file("unimplemented.exe", [repo / "tests/winrt_projection/unimplemented.cs"], ["/r:" + windows(reference)])
        run("mark-unimplemented", [marker.name, unimplemented.name, "unimplemented-winrt.exe"], marker="flags=WindowsRuntime")
        missing = fixture("unimplemented", unimplemented.name, "UnimplementedProjectionTest", "CN=projection")
        shutil.copyfile(output / "unimplemented-winrt.exe", missing / unimplemented.name)
        shutil.copyfile(projection, missing / "Windows.dll")
        run("before-completion", [unimplemented.name], cwd=missing, succeeds=False, marker="UNBOUND:")
        resolver = compile_file("resolve-references.exe", [repo / "tests/winrt_projection/resolve_references.cs"], ["/r:Mono.Cecil.dll"])
        completer = compile_file("complete.exe", [repo / "host/winrt/complete.cs"], ["/r:Mono.Cecil.dll"])
        consumers = [output / "unimplemented-winrt.exe", output / "consumer-winrt.exe", output / "application-winrt.exe"]
        consumers += [p.resolve() for p in args.consumer]
        run("resolve-before-completion", [resolver.name, windows(projection.parent)] + [windows(p) for p in consumers],
            succeeds=False, marker="UNRESOLVED ")
        completed = output / "completed/Windows.dll"
        completed.parent.mkdir(exist_ok=True)
        run("complete", [completer.name, windows(metadata), windows(projection), windows(completed)] + [windows(p) for p in consumers], marker="Completed ")
        run("resolve-completed", [resolver.name, windows(completed.parent)] + [windows(p) for p in consumers], marker="0 unresolved")
        shutil.copyfile(completed, missing / "Windows.dll")
        run("after-completion", [unimplemented.name], cwd=missing, marker="UNIMPLEMENTED: Windows.Devices.Geolocation.Geolocator..ctor")
        # Completion must keep implemented members native, not replace them with stubs.
        shutil.copyfile(completed, folder / "Windows.dll")
        run("completed-contract", [consumer.name], cwd=folder, marker="PASS: generated Windows projection")
        shutil.copyfile(completed, app / "Windows.dll")
        run("completed-application", [application.name], cwd=app, marker="PASS: managed App.OnLaunched ran on the shim view thread")
        if args.game:
            source = args.game
            paths = [source / "appx-host.exe", source / "AppxManifest.xml"] + [p.resolve() for p in args.consumer]
            def hashes():
                return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
            before = hashes()
            game = output / "game"
            require(not game.exists(), "game output already exists; choose a fresh --output directory")
            # Keep links as links: an installed game's own Wine prefix maps z: to /.
            # The launch uses --prefix, so that prefix is not copied at all.
            shutil.copytree(source, game, symlinks=True, ignore=shutil.ignore_patterns("*.log", ".wine-prefix"))
            shutil.copyfile(shim, game / "appxportable.dll")
            shutil.copyfile(completed, game / "Windows.dll")
            run("game-launch", ["appx-host.exe"], cwd=game, succeeds=False,
                marker=args.expected_game_error, evidence=game / "managed.log")
            require(before == hashes(), "source game files changed during probe")
            (output / "game-inputs.json").write_text(json.dumps(before, indent=2) + "\n")
        return 0
    except (OSError, RuntimeError) as error:
        print(error)
        return 1
    finally:
        (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
