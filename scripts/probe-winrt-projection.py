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
# (windows.applicationmodel.h, windows.applicationmodel.activation.h, windows.ui.xaml.h,
# windows.ui.xaml.media.animation.h),
# independent of the metadata the generator reads. Slots follow IInspectable's six.
HEADER_IIDS = {
    "IPackageStatics": "4e534bdf-2960-4878-97a4-9624deb72f2d", "IPackage": "163c792f-bd75-413c-bf23-b1fe7b95d825",
    "IPackageId": "1adb665e-37c7-4790-9980-dd7ae74e8bb2", "ILaunchActivatedEventArgs": "fbc93e26-a14a-4b4f-82b0-33bed920af52",
    "IApplication": "74b861a1-7487-46a9-9a6e-c78b512726c5", "IApplicationStatics": "06499997-f7b4-45fe-b763-7577d1d3cb4a",
    "IApplicationFactory": "93bbe361-be5a-4ee3-b4a3-95118dc97a89", "IApplicationOverrides": "25f99ff7-9347-459a-9fac-b2d0e11c1a0f",
    "IApplicationInitializationCallback": "b6351c55-c284-46e4-8310-fb0967fab76f",
    "IApplicationInitializationCallbackParams": "751b792e-5772-4488-8b87-f547faa64474",
    "IActivatedEventArgs": "cf651713-cd08-4fd8-b697-a281b6544e2e", "IWindowStatics": "93328409-4ea1-4afa-83dc-0c4e73e88bb1",
    "IWindow": "3276167d-c9f6-462d-9de2-ae4c1fd8c2e5", "IFrameFactory": "02ee93d4-448e-469e-9799-0a8a1f70f171",
    "IDependencyObject": "5c526665-f60e-4912-af59-5fe0680f089d",
    "IDependencyProperty": "85b13970-9bc4-4e96-acf1-30c8fd3d55c8",
    "IDependencyPropertyStatics": "49e5f28f-8259-4d5c-aae0-83d56dbb68d9",
    "IPropertyMetadata": "814ef30d-8d18-448a-8644-f2cb51e70380",
    "ICoreWindowStatic": "4d239005-3c2a-41b1-9022-536bb9cf93b1", "ICoreWindow": "79b9d5f2-879e-4b89-b798-79e47598030c",
    "ICoreDispatcher": "60db2fa8-b705-4fde-a7d6-ebbb1891d39e",
    "IApplicationDataStatics": "5612147b-e843-45e3-94d8-06169e3c8e17", "IApplicationData": "c3da6fb7-b744-4b45-b0b8-223a0938d0dc",
    "IApplicationDataContainer": "c5aefd1e-f467-40ba-8566-ab640a441e1d", "IPropertySet": "8a43ed9f-f4e6-4421-acf9-1dab2986820c",
    "ISplashScreen": "ca4d975c-d4d6-43f0-97c0-0833c6391c24",
    "IResourceDictionary": "c1ea4f24-d6de-4191-8e3a-f48601f7489c", "IResourceDictionaryFactory": "ea3639b5-31b7-4271-92c9-7c9584a91c22",
    "ITimeline": "0bc465dc-be4d-4d0d-9549-2208b715f40d", "IStoryboard": "d45c1e6e-3594-460e-981a-32271bd3aa06",
    "IComponentConnector": "f6790987-e6e5-47f2-92c6-eccce4ba159a",
    "IUriRuntimeClass": "9e365e57-48b2-4160-956f-c7385120bbfc", "IUriRuntimeClassFactory": "44a9796f-723e-4fdf-a218-033e75b0c084",
    "IViewbox": "05252c58-ba9d-4809-9ec3-fa0d16710ba1",
    "IGrid": "fd104460-2e15-4ba3-8b8f-fa693a4161e9", "IPanel": "a50a4bbd-8361-469c-90da-e9a40c7474df",
    "ITextBlock": "ae2d9271-3b4a-45fc-8468-f7949548f4d5",
    "IProgressRing": "6da5e49e-6e9d-425c-bd7c-02173e39763f",
    "IImage": "495b7402-9af3-4e50-aa90-03388f3086d2",
    "IPage": "c4b27075-e641-453e-824d-012fc7cf95cf", "IPageFactory": "dfa149ac-1849-445e-937c-40a9590cc076",
    "IPageOverrides": "230b9cb8-3419-4306-aabe-4a8e680d3627", "IUserControl": "a7a69ec9-ea35-4679-bf29-f4f09286d314",
    "INavigationEventArgs": "b6aa9834-6691-44d1-bdf7-58820c27b0d0", "INavigatingCancelEventArgs": "fd1d67ae-eafb-4079-be80-6dc92a03aedf",
    "INetworkInformationStatics": "5074f851-950d-4165-9c15-365619481eea",
    "IConnectionProfile": "71ba143c-598e-49d0-84eb-8febaedcc195", "INetworkStatusChangedEventHandler": "71ba143f-598e-49d0-84eb-8febaedcc195"}
HEADER_SLOTS = [
    "Windows.UI.Xaml.Controls.ITextBlock::get_Text, slot 26",
    "Windows.UI.Xaml.Controls.ITextBlock::put_Text, slot 27",
    "Windows.UI.Xaml.Controls.IProgressRing::get_IsActive, slot 6",
    "Windows.UI.Xaml.Controls.IProgressRing::put_IsActive, slot 7",
    "Windows.UI.Xaml.Media.Animation.IStoryboard::Begin, slot 9",
    "Windows.ApplicationModel.IPackageStatics::get_Current, slot 6", "Windows.ApplicationModel.IPackage::get_Id, slot 6",
    "Windows.ApplicationModel.IPackageId::get_Name, slot 6", "Windows.ApplicationModel.IPackageId::get_Architecture, slot 8",
    "Windows.ApplicationModel.IPackageId::get_ResourceId, slot 9", "Windows.ApplicationModel.IPackageId::get_Publisher, slot 10",
    "Windows.ApplicationModel.Activation.ILaunchActivatedEventArgs::get_Arguments, slot 6",
    "Windows.UI.Xaml.IApplicationFactory::CreateInstance, slot 6", "Windows.UI.Xaml.IApplicationStatics::get_Current, slot 6",
    "Windows.UI.Xaml.IApplicationStatics::LoadComponent, slot 8",
    "Windows.UI.Xaml.IApplicationStatics::LoadComponentWithResourceLocation, slot 9",
    "Windows.UI.Xaml.IFrameworkElement::FindName, slot 51",
    "Windows.UI.Xaml.IFrameworkElement::get_Resources, slot 7", "Windows.UI.Xaml.IFrameworkElement::put_Resources, slot 8",
    "Windows.UI.Xaml.IResourceDictionaryFactory::CreateInstance, slot 6",
    "Windows.UI.Xaml.IApplicationStatics::Start, slot 7", "Windows.UI.Xaml.IApplication::Exit, slot 17",
    "Windows.UI.Xaml.IApplicationOverrides::OnLaunched, slot 7", "Windows.UI.Xaml.IApplicationOverrides::OnActivated, slot 6",
    "Windows.ApplicationModel.Activation.IActivatedEventArgs::get_Kind, slot 6",
    "Windows.ApplicationModel.Activation.IActivatedEventArgs::get_PreviousExecutionState, slot 7",
    "Windows.UI.Xaml.IApplication::add_Suspending, slot 13", "Windows.UI.Xaml.IApplication::remove_Suspending, slot 14",
    "Windows.UI.Xaml.IWindowStatics::get_Current, slot 6", "Windows.UI.Xaml.IWindow::get_Content, slot 8",
    "Windows.UI.Xaml.IWindow::put_Content, slot 9", "Windows.UI.Xaml.IWindow::Activate, slot 20",
    "Windows.UI.Xaml.Controls.IFrameFactory::CreateInstance, slot 6",
    "Windows.UI.Xaml.IDependencyPropertyStatics::get_UnsetValue, slot 6",
    "Windows.UI.Xaml.IDependencyPropertyStatics::Register, slot 7",
    "Windows.UI.Xaml.IDependencyPropertyStatics::RegisterAttached, slot 8",
    "Windows.UI.Xaml.IDependencyObject::GetValue, slot 6", "Windows.UI.Xaml.IDependencyObject::SetValue, slot 7",
    "Windows.UI.Xaml.IDependencyObject::ClearValue, slot 8", "Windows.UI.Xaml.IDependencyObject::ReadLocalValue, slot 9",
    "Windows.UI.Xaml.IDependencyObject::GetAnimationBaseValue, slot 10",
    "Windows.UI.Core.ICoreWindowStatic::GetForCurrentThread, slot 6", "Windows.UI.Core.ICoreWindow::get_Dispatcher, slot 9",
    "Windows.Storage.IApplicationDataStatics::get_Current, slot 6", "Windows.Storage.IApplicationData::get_LocalSettings, slot 10",
    "Windows.Storage.IApplicationData::get_RoamingSettings, slot 11", "Windows.Storage.IApplicationData::get_LocalFolder, slot 12", "Windows.Storage.IApplicationDataContainer::get_Name, slot 6",
    "Windows.Storage.IApplicationDataContainer::get_Locality, slot 7", "Windows.Storage.IApplicationDataContainer::get_Values, slot 8",
    "Windows.Storage.IApplicationDataContainer::CreateContainer, slot 10", "Windows.Storage.IApplicationDataContainer::DeleteContainer, slot 11",
    "Windows.Networking.Connectivity.INetworkInformationStatics::GetInternetConnectionProfile, slot 7",
    "Windows.Networking.Connectivity.INetworkInformationStatics::add_NetworkStatusChanged, slot 12",
    "Windows.Networking.Connectivity.INetworkInformationStatics::remove_NetworkStatusChanged, slot 13",
    "Windows.Networking.Connectivity.IConnectionProfile::get_ProfileName, slot 6",
    "Windows.Networking.Connectivity.IConnectionProfile::GetNetworkConnectivityLevel, slot 7",
    "Windows.ApplicationModel.Activation.IActivatedEventArgs::get_SplashScreen, slot 8",
    "Windows.ApplicationModel.Activation.ISplashScreen::add_Dismissed, slot 7",
    "Windows.ApplicationModel.Activation.ISplashScreen::remove_Dismissed, slot 8",
    "Windows.UI.Xaml.Controls.IPageFactory::CreateInstance, slot 6", "Windows.UI.Xaml.Controls.IPage::get_Frame, slot 6",
    "Windows.UI.Xaml.Controls.IPageOverrides::OnNavigatedFrom, slot 6", "Windows.UI.Xaml.Controls.IPageOverrides::OnNavigatedTo, slot 7",
    "Windows.UI.Xaml.Controls.IPageOverrides::OnNavigatingFrom, slot 8", "Windows.UI.Xaml.Controls.IUserControl::get_Content, slot 6",
    "Windows.UI.Xaml.Controls.IUserControl::put_Content, slot 7",
    "Windows.UI.Xaml.Markup.IComponentConnector::Connect, slot 6",
    "Windows.Foundation.IUriRuntimeClass::get_AbsoluteUri, slot 6", "Windows.Foundation.IUriRuntimeClassFactory::CreateUri, slot 6",
    "Windows.UI.Xaml.Navigation.INavigationEventArgs::get_Uri, slot 10", "Windows.UI.Xaml.Navigation.INavigationEventArgs::put_Uri, slot 11"]
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


def copy_owned_file(source, destination):
    """Copy into owned output without following a copied destination symlink."""
    destination = Path(destination)
    if destination.is_symlink():
        destination.unlink()
    shutil.copyfile(source, destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("wine", "prefix", "metadata", "shim", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--page-fixture", type=Path, help="test-only native Page callback DLL (default beside shim)")
    parser.add_argument("--page-only", action="store_true", help="run native Page composition and override fixtures")
    parser.add_argument("--splash-only", action="store_true", help="run the native splash activation fixture")
    parser.add_argument("--network-fixture", type=Path, help="test-only native network callback DLL (default beside shim)")
    parser.add_argument("--network-only", action="store_true", help="run the native connectivity projection fixture")
    parser.add_argument("--io-only", action="store_true", help="run the native storage/stream projection fixture")
    parser.add_argument("--async-only", action="store_true", help="run the focused WinRT task adapters fixture")
    parser.add_argument("--storage-only", action="store_true", help="run the focused persistent settings projection fixture")
    parser.add_argument("--xbox-only", action="store_true", help="run the focused Xbox projection fixture")
    parser.add_argument("--xbox-metadata", type=Path, help="package Microsoft.Xbox.winmd, metadata only")
    parser.add_argument("--game", type=Path, help="copy and launch this installed game after completion")
    parser.add_argument("--expected-game-error", help="required named failure in the copied game managed.log")
    parser.add_argument("--cecil", type=Path)
    parser.add_argument("--runtime-case", choices=("inspectable", "identity", "stale", "invocation", "reentrant"))
    parser.add_argument("--consumer", type=Path, action="append", default=[])
    parser.add_argument("--architecture", choices=("x86", "x64"), default="x86")
    args = parser.parse_args()
    if sum((args.page_only, args.splash_only, args.network_only, args.io_only, args.async_only, args.storage_only, args.xbox_only, bool(args.runtime_case))) > 1:
        parser.error("focused probe modes cannot be combined")
    if (args.page_only or args.splash_only or args.network_only or args.io_only or args.async_only or args.storage_only) and (args.game or args.consumer or args.xbox_metadata):
        parser.error("focused projection probes cannot include game or Xbox inputs")
    if args.xbox_only and (not args.xbox_metadata or args.game):
        parser.error("--xbox-only requires --xbox-metadata and cannot launch a game")
    repo = Path(__file__).resolve().parents[1]
    wine, prefix, metadata, shim, output = (getattr(args, name).resolve()
                                          for name in ("wine", "prefix", "metadata", "shim", "output"))
    network_fixture = args.network_fixture.resolve() if args.network_fixture else shim.parent / "network_projection_fixture.dll"
    if (args.network_only or not any((args.page_only, args.splash_only, args.io_only, args.async_only, args.storage_only, args.xbox_only, args.runtime_case))) and not network_fixture.is_file():
        parser.error("build network_projection_fixture or pass --network-fixture")
    page_fixture = args.page_fixture.resolve() if args.page_fixture else shim.parent / "page_projection_fixture.dll"
    if (args.page_only or not any((args.splash_only, args.network_only, args.io_only, args.async_only, args.storage_only, args.xbox_only, args.runtime_case))) and not page_fixture.is_file():
        parser.error("build page_projection_fixture or pass --page-fixture")
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
    copy_owned_file(cecils[0], output / "Mono.Cecil.dll")
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG="-all", MVK_CONFIG_LOG_LEVEL="0")
    for key in ("WINE_MONO_AOT", "WINEDLLOVERRIDES", "MONO_PATH", "MONO_ENV_OPTIONS"):
        env.pop(key, None)
    results = []

    def windows(path):
        return "Z:" + str(path).replace("/", "\\")

    def require(value, message):
        if not value:
            raise RuntimeError(message)

    def run(name, command, cwd=output, succeeds=True, marker=None, evidence=None, environment=None):
        log = output / (name + ".log")
        with log.open("w") as stream:
            child = subprocess.Popen([str(wine), *command], env=dict(env, **(environment or {})), cwd=cwd,
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
        copy_owned_file(shim, folder / "appxportable.dll")
        (folder / "AppxManifest.xml").write_text(
            '<Package><Identity Name="' + identity + '" Publisher="' + publisher +
            '" Version="1.0.0.0" ProcessorArchitecture="' + args.architecture +
            '"/><Applications><Application Id="Test" Executable="' + executable + '"/></Applications></Package>')
        return folder

    def check_storage_metadata(generator):
        generated = (output / "Windows.generated.cs").read_text(encoding="utf-8")
        for value in ('Map = new global::System.Guid("1b0d3570-0877-5ec2-8a2c-3b9539506aca")',
                      'Iterable = new global::System.Guid("fe2f3d47-5d47-5499-8374-430c7cda0204")',
                      "Lookup = 6,", "Insert = 10,", "Remove = 11,", "Clear = 12,", "Size = 7, HasKey = 8,",
                      "First = 6, Current = 6,", "HasCurrent = 7, MoveNext = 8,", "Key = 6, Value = 7"):
            require(value in generated, "collection ABI differs from the SDK header: " + value)
        mutator = compile_file("mutate-collections.exe", [repo / "tests/winrt_projection/mutate_collections_metadata.cs"], ["/r:Mono.Cecil.dll"])
        for kind, method in (("insert", "IMap`2.Insert"), ("iterator", "IIterator`1.get_HasCurrent"), ("order", "IMap`2 method order")):
            mutated = output / ("collections-" + kind)
            target = output / ("collections-" + kind + ".cs")
            run("mutate-collections-" + kind, [mutator.name, windows(metadata), windows(mutated), kind])
            run("reject-collections-" + kind, [generator.name, windows(mutated), windows(repo / "host/winrt/bootstrap.xml"), windows(target)],
                succeeds=False, marker=method)
            require(not target.exists(), "invalid collection ABI produced a usable output")

    def probe_async(projection):
        bridge = compile_file("tasks/System.Runtime.WindowsRuntime.dll", [repo / "host/winrt/task_extensions.cs", repo / "host/winrt/stream_extensions.cs"],
                              ["/target:library", "/r:" + windows(projection)])
        consumer = compile_file("async.exe", [repo / "tests/winrt_projection/async.cs"],
                                ["/r:" + windows(projection), "/r:" + windows(bridge)])
        marker = compile_file("mark-async.exe", [repo / "tests/winrt_projection/mark_reference.cs"], ["/r:Mono.Cecil.dll"])
        run("mark-async-winrt", [marker.name, windows(consumer), "async-winrt.exe"], marker="flags=WindowsRuntime")
        signed = output / "async-bound.exe"
        run("mark-async-runtime", [marker.name, "async-winrt.exe", windows(signed), "--runtime-reference"],
            marker="PublicKeyToken=b77a5c561934e089")
        folder = fixture("async", signed.name, "AsyncProjectionTest", "CN=projection")
        for source in (signed, projection):
            copy_owned_file(source, folder / source.name)
        copied_bridge = folder / bridge.name
        if copied_bridge.exists() or copied_bridge.is_symlink():
            copied_bridge.unlink()
        run("missing-task-runtime", [signed.name], cwd=folder, succeeds=False, marker="System.Runtime.WindowsRuntime")
        copy_owned_file(bridge, copied_bridge)
        run("task-adapters", [signed.name], cwd=folder,
            marker="PASS: WinRT task adapters preserve completion, failure, cancellation, progress and ownership")
        return bridge

    def probe_page(projection):
        generated = (output / "Windows.generated.cs").read_text(encoding="utf-8")
        for name in ("IPage", "IPageFactory", "IPageOverrides", "IUserControl", "INavigationEventArgs", "INavigatingCancelEventArgs", "IComponentConnector", "IUriRuntimeClass", "IUriRuntimeClassFactory", "IResourceDictionary", "IResourceDictionaryFactory", "ITimeline", "IStoryboard", "IImage", "IProgressRing", "ITextBlock", "IViewbox", "IGrid", "IPanel"):
            require(HEADER_IIDS[name] in generated, "Page IID differs from SDK header: " + name)
        for slot in HEADER_SLOTS:
            if any(name in slot for name in ("Controls.IPage", "Controls.IUserControl", "IComponentConnector", "IUriRuntimeClass", "INavigationEventArgs::", "IApplicationStatics::LoadComponent", "IFrameworkElement::FindName", "IFrameworkElement::get_Resources", "IFrameworkElement::put_Resources", "IResourceDictionaryFactory", "IStoryboard::", "IProgressRing::", "ITextBlock::")):
                require("// " + slot in generated, "Page slot differs from SDK header: " + slot)
        mutator = compile_file("mutate-component.exe", [repo / "tests/winrt_projection/mutate_collections_metadata.cs"], ["/r:Mono.Cecil.dll"])
        for kind, member in (("connector-id", "IComponentConnector.Connect"), ("uri-return", "IUriRuntimeClass.get_AbsoluteUri"), ("uri-factory", "IUriRuntimeClassFactory.CreateUri")):
            mutated = output / kind
            run("mutate-" + kind, [mutator.name, windows(metadata), windows(mutated), kind])
            run("reject-" + kind, ["generator.exe", windows(mutated), windows(repo / "host/winrt/bootstrap.xml"), windows(output / (kind + ".cs"))], succeeds=False, marker=member)
        marker = compile_file("mark-page.exe", [repo / "tests/winrt_projection/mark_reference.cs"], ["/r:Mono.Cecil.dll"])
        reference = compile_file("page-reference/Windows.dll", [repo / "tests/winrt_projection/reference.cs"], ["/target:library"])
        consumer = compile_file("page.exe", [repo / "tests/winrt_projection/page.cs"], ["/r:" + windows(reference)])
        run("mark-page", [marker.name, windows(consumer), "page-winrt.exe"], marker="flags=WindowsRuntime")
        folder = fixture("page", consumer.name, "PageProjectionTest", "CN=projection")
        copy_owned_file(output / "page-winrt.exe", folder / consumer.name)
        copy_owned_file(projection, folder / "Windows.dll")
        copy_owned_file(page_fixture, folder / "page_projection_fixture.dll")
        run("page", [consumer.name], cwd=folder, marker="PASS: managed Page composes native inheritance, content and window identity")
        runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs", "storage_abi.cs", "array_buffer.cs")]
        abi = compile_file("page-abi.exe", runtime + [output / "Windows.generated.cs", repo / "tests/winrt_projection/page.cs"], ["/define:NATIVE_ABI"])
        copy_owned_file(abi, folder / abi.name)
        run("page-abi", [abi.name], cwd=folder, marker="PASS: native Page callbacks dispatch through derived managed overrides and base methods")
        source = compile_file("source.exe", [repo / "tests/winrt_projection/source.cs"], ["/r:" + windows(reference)])
        run("mark-source", [marker.name, windows(source), "source-winrt.exe"], marker="flags=WindowsRuntime")
        folder = fixture("source", source.name, "SourceProjectionTest", "CN=projection")
        copy_owned_file(output / "source-winrt.exe", folder / source.name)
        copy_owned_file(projection, folder / "Windows.dll")
        copy_owned_file(page_fixture, folder / "page_projection_fixture.dll")
        run("source", [source.name], cwd=folder, marker="PASS: source XAML builds native content, properties, names and managed connections; unsupported input fails explicitly")

    def probe_splash(projection, generator):
        generated = (output / "Windows.generated.cs").read_text(encoding="utf-8")
        for value in (HEADER_IIDS["ISplashScreen"], "7725b2a5-287d-5ed2-a789-2a6a2673c7fe"):
            require(value in generated, "splash IID differs from SDK header: " + value)
        for value in HEADER_SLOTS:
            if "SplashScreen" in value or "ISplashScreen" in value:
                require("// " + value in generated, "splash ABI differs from SDK header: " + value)
        mutator = compile_file("mutate-splash.exe", [repo / "tests/winrt_projection/mutate_collections_metadata.cs"], ["/r:Mono.Cecil.dll"])
        changed = output / "splash-bad-handler"
        invalid = output / "splash-bad-handler.cs"
        if invalid.exists():
            invalid.unlink()
        run("mutate-splash-handler", [mutator.name, windows(metadata), windows(changed), "typed-event"])
        run("reject-splash-handler", [generator.name, windows(changed), windows(repo / "host/winrt/bootstrap.xml"), windows(invalid)], succeeds=False, marker="Unsupported typed event delegate")
        require(not invalid.exists(), "unqualified typed event produced output")
        marker = compile_file("mark-splash.exe", [repo / "tests/winrt_projection/mark_reference.cs"], ["/r:Mono.Cecil.dll"])
        reference = compile_file("splash-reference/Windows.dll", [repo / "tests/winrt_projection/reference.cs"], ["/target:library"])
        consumer = compile_file("splash.exe", [repo / "tests/winrt_projection/splash.cs"], ["/r:" + windows(reference)])
        run("mark-splash", [marker.name, windows(consumer), "splash-winrt.exe"], marker="flags=WindowsRuntime")
        folder = fixture("splash", consumer.name, "SplashProjectionTest", "CN=projection")
        copy_owned_file(output / "splash-winrt.exe", folder / consumer.name)
        copy_owned_file(projection, folder / "Windows.dll")
        run("splash", [consumer.name], cwd=folder, marker="PASS: managed splash identity and typed dismissal follow native activation")

    def probe_network(projection, generator):
        generated = (output / "Windows.generated.cs").read_text(encoding="utf-8")
        checks = ['System.Guid("' + HEADER_IIDS[name] + '")' for name in ("INetworkInformationStatics", "IConnectionProfile", "INetworkStatusChangedEventHandler")]
        checks += ["// " + slot for slot in HEADER_SLOTS if "Networking.Connectivity" in slot]
        checks += ["self, out int result);", "public static global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_NetworkStatusChanged"]
        require(all(value in generated for value in checks), "network ABI differs from SDK header values")
        mutator = compile_file("mutate-network.exe", [repo / "tests/winrt_projection/mutate_collections_metadata.cs"], ["/r:Mono.Cecil.dll"])
        changed = output / "network-wide-enum"
        invalid = output / "network-wide-enum.cs"
        if invalid.exists():
            invalid.unlink()
        run("mutate-network-enum", [mutator.name, windows(metadata), windows(changed), "network-enum"])
        run("reject-network-enum", [generator.name, windows(changed), windows(repo / "host/winrt/bootstrap.xml"), windows(invalid)], succeeds=False, marker="Unsupported enum return")
        require(not invalid.exists(), "invalid connectivity enum left a usable output")
        marker = compile_file("mark-network.exe", [repo / "tests/winrt_projection/mark_reference.cs"], ["/r:Mono.Cecil.dll"])
        reference = compile_file("network-reference/Windows.dll", [repo / "tests/winrt_projection/reference.cs"], ["/target:library"])
        consumer = compile_file("network.exe", [repo / "tests/winrt_projection/network.cs"], ["/r:" + windows(reference)])
        run("mark-network", [marker.name, windows(consumer), "network-winrt.exe"], marker="flags=WindowsRuntime")
        folder = fixture("network", consumer.name, "NetworkProjectionTest", "CN=projection")
        copy_owned_file(output / "network-winrt.exe", folder / consumer.name)
        copy_owned_file(reference, folder / "Windows.dll")
        run("network-reference-contract", [consumer.name], cwd=folder, succeeds=False, marker="NetworkInformation.GetInternetConnectionProfile")
        copy_owned_file(projection, folder / "Windows.dll")
        for mode in ("offline", "stub"):
            run("network-" + mode, [consumer.name, mode], cwd=folder, environment={"APPX_PORTABLE_STUB_IAP": "1" if mode == "stub" else "0"},
                marker="PASS: managed network projection preserves native connectivity and event lifetime (" + mode + ")")
        runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs", "storage_abi.cs", "array_buffer.cs")]
        abi = compile_file("network-abi.exe", runtime + [output / "Windows.generated.cs", repo / "tests/winrt_projection/network_abi.cs"])
        copy_owned_file(abi, folder / abi.name)
        copy_owned_file(network_fixture, folder / "network_projection_fixture.dll")
        run("network-abi", [abi.name], cwd=folder, marker="PASS: native network delegate ABI preserves sender and callback failures")

    def check_io_metadata(generator):
        mutator = compile_file("mutate-storage-abi.exe", [repo / "tests/winrt_projection/mutate_collections_metadata.cs"], ["/r:Mono.Cecil.dll"])
        for kind, expected in (("stream-size", "IRandomAccessStream.get_Size"), ("read-count", "IInputStream.ReadAsync"),
                               ("buffer-order", "Unsupported buffer method order"), ("future-order", "Unsupported storage async method order")):
            changed = output / ("storage-" + kind)
            target = output / ("storage-" + kind + ".cs")
            if target.exists():
                target.unlink()
            run("mutate-storage-" + kind, [mutator.name, windows(metadata), windows(changed), kind])
            run("reject-storage-" + kind, [generator.name, windows(changed), windows(repo / "host/winrt/bootstrap.xml"), windows(target)], succeeds=False, marker=expected)
            require(not target.exists(), "unqualified storage ABI produced output")

    def probe_io(projection, bridge):
        generated = (output / "Windows.generated.cs").read_text(encoding="utf-8")
        # Independent closed generic IID constants from windows.storage(.streams).h.
        for iid in ("5e52f8ce-aced-5a42-95b4-f674dd84885e", "c4a57c5e-32b0-55b3-ad13-ce1c23041ed6", "d26b2819-897f-5c7d-84d6-56d796561431", "e521c894-2c26-5946-9e61-2b5e188d01ed", "3dddecf4-1d39-58e8-83b1-dbed541c7f35", "06386a7a-e009-5b0b-ab68-a8e48b516647", "bf666554-7605-5d9a-b14e-18d8c8472afe"):
            require(iid in generated, "storage async IID differs from SDK header: " + iid)
        # Slots independently counted from the SDK ABI declarations.
        expected_slots = {"Item_get_Name": 11, "Item_get_Path": 12, "Folder_GetFileAsync": 10,
                          "FileStatics_GetFileFromPathAsync": 6, "FileReference_OpenReadAsync": 6,
                          "Info_get_Id": 6, "Info_get_Status": 7, "Info_get_ErrorCode": 8, "Info_Cancel": 9, "Info_Close": 10,
                          "Buffer_get_Capacity": 6, "Buffer_get_Length": 7, "Buffer_put_Length": 8, "BufferFactory_Create": 6,
                          "Input_ReadAsync": 6, "Random_get_Size": 6, "Random_put_Size": 7, "Random_GetInputStreamAt": 8,
                          "Random_GetOutputStreamAt": 9, "Random_get_Position": 10, "Random_Seek": 11, "Random_CloneStream": 12,
                          "Random_get_CanRead": 13, "Random_get_CanWrite": 14, "Content_get_ContentType": 6, "Closable_Close": 6}
        for member, slot in expected_slots.items():
            require("int " + member + " = " + str(slot) + ";" in generated, "storage ABI slot differs from SDK header: " + member)
        consumer = compile_file("storage-io.exe", [repo / "tests/winrt_projection/storage_io.cs"],
                                ["/r:" + windows(projection), "/r:" + windows(bridge)])
        marker = output / "mark-async.exe"
        run("mark-storage-io-winrt", [marker.name, windows(consumer), "storage-io-winrt.exe"], marker="flags=WindowsRuntime")
        signed = output / "storage-io-bound.exe"
        run("mark-storage-io-runtime", [marker.name, "storage-io-winrt.exe", windows(signed), "--runtime-reference"], marker="PublicKeyToken=b77a5c561934e089")
        folder = fixture("storage-io", signed.name, "StorageIoProjectionTest", "CN=projection")
        for source in (signed, projection, bridge):
            copy_owned_file(source, folder / source.name)
        runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs", "storage_abi.cs", "array_buffer.cs")]
        identity = compile_file("storage-identity.exe", runtime + [output / "Windows.generated.cs", repo / "tests/winrt_projection/storage_identity.cs"])
        copy_owned_file(identity, folder / identity.name)
        run("storage-identity", [identity.name], cwd=folder, marker="PASS: buffer projection preserves object identity and native-owned pin lifetime")
        run("storage-io", [signed.name], cwd=folder, environment={"APPX_PORTABLE_DATA": windows(output / "io-data")},
            marker="PASS: managed storage awaits native files and reads seekable streams and buffers")

    def probe_storage(marker, projection):
        reference = compile_file("storage-reference/Windows.dll", [repo / "tests/winrt_projection/reference.cs"], ["/target:library"])
        consumer = compile_file("storage.exe", [repo / "tests/winrt_projection/storage.cs"], ["/r:" + windows(reference)])
        run("mark-storage", [marker.name, consumer.name, "storage-winrt.exe"], marker="flags=WindowsRuntime")
        folder = fixture("storage", consumer.name, "StorageProjectionTest", "CN=projection")
        copy_owned_file(output / "storage-winrt.exe", folder / consumer.name)
        copy_owned_file(projection, folder / "Windows.dll")
        environment = {"APPX_PORTABLE_DATA": windows(output / "settings-data")}
        for mode in ("write", "read"):
            run("storage-" + mode, [consumer.name, mode], cwd=folder, environment=environment,
                marker="PASS: managed settings " + mode + " through the native persistent map")

    def probe_xbox(marker, generator, completer):
        xbox_reference = compile_file("xbox-reference/Microsoft.Xbox.dll", [repo / "tests/winrt_projection/xbox_reference.cs"], ["/target:library"])
        xbox_consumer = compile_file("xbox.exe", [repo / "tests/winrt_projection/xbox.cs"], ["/r:" + windows(xbox_reference)])
        run("mark-xbox", [marker.name, xbox_consumer.name, "xbox-winrt.exe", "--assembly", "Microsoft.Xbox"], marker="flags=WindowsRuntime")
        xbox_folder = fixture("xbox", xbox_consumer.name, "XboxProjectionTest", "CN=projection")
        copy_owned_file(output / "xbox-winrt.exe", xbox_folder / xbox_consumer.name)
        copy_owned_file(xbox_reference, xbox_folder / "Microsoft.Xbox.dll")
        run("xbox-reference-contract", [xbox_consumer.name], cwd=xbox_folder, succeeds=False, marker="Microsoft.Xbox.User..ctor")
        xbox_metadata = output / "xbox-metadata"
        xbox_metadata.mkdir(exist_ok=True)
        xbox_winmd = xbox_metadata / "Microsoft.Xbox.winmd"
        copy_owned_file(args.xbox_metadata.resolve(), xbox_winmd)
        xbox_generated = output / "Microsoft.Xbox.generated.cs"
        run("generate-xbox", [generator.name, windows(xbox_metadata), windows(repo / "host/winrt/xbox.xml"), windows(xbox_generated)])
        xbox_text = xbox_generated.read_text(encoding="utf-8")
        for value in ("d7fba214-e744-4093-9fee-ab68795b70a0", "115ccd10-26c2-471c-b97b-5b3dac160c6e", "4b00e95d-3006-4f02-bd96-3919d984d4c7"):
            require('System.Guid("' + value + '")' in xbox_text, "Xbox IID differs from native header: " + value)
        for member in ("Microsoft.Xbox.IUser::get_Identity, slot 6", "Microsoft.Xbox.Foundation.IServiceClient::get_Token, slot 11",
                       "Microsoft.Xbox.Foundation.IServiceClient::get_SignedInUserIdentity, slot 10", "Microsoft.Xbox.IXboxLIVEService::get_ServiceClient, slot 11",
                       "Microsoft.Xbox.IXboxLIVEService::get_SignedInUserIdentity, slot 10"):
            require("// " + member in xbox_text, "Xbox member differs from native header: " + member)
        no_constructor = output / "xbox-no-default-constructor.xml"
        no_constructor.write_text('<Projection><Class Name="Microsoft.Xbox.Foundation.UserIdentity"><Constructor /></Class></Projection>')
        run("reject-xbox-constructor", [generator.name, windows(xbox_metadata), windows(no_constructor), windows(output / "xbox-no-constructor.cs")],
            succeeds=False, marker="No default activation constructor")
        qualifier = compile_file("xbox-metadata.exe", [repo / "host/winrt/xbox_metadata.cs"], ["/r:Mono.Cecil.dll"])
        version = output / "Microsoft.Xbox.version.cs"
        qualification = run("qualify-xbox", [qualifier.name, windows(xbox_winmd), windows(version)], marker="Xbox IUser ABI ")
        abi_version = re.search(r"Xbox IUser ABI ([0-9.]+)", qualification).group(1)
        metadata_hash = hashlib.sha256(xbox_winmd.read_bytes()).hexdigest()
        require(metadata_hash in xbox_text and metadata_hash in qualification,
                "Xbox generation and qualification must use the same metadata snapshot")
        mutator = compile_file("mutate-xbox.exe", [repo / "tests/winrt_projection/mutate_xbox_metadata.cs"], ["/r:Mono.Cecil.dll"])
        for mutation in ("iid", "layout", "signature", "service-slot", "live-slot"):
            changed = output / ("xbox-" + mutation + ".winmd")
            invalid = output / ("xbox-" + mutation + ".cs")
            run("mutate-xbox-" + mutation, [mutator.name, windows(xbox_winmd), windows(changed), mutation])
            run("reject-xbox-" + mutation, [qualifier.name, windows(changed), windows(invalid)], succeeds=False, marker="Unsupported Xbox")
            require(not invalid.exists(), "unqualified Xbox metadata produced a version")
        runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs")]
        bootstrap = compile_file("xbox-projection/Microsoft.Xbox.dll", runtime + [xbox_generated, version], ["/target:library"])
        all_consumers = [output / "xbox-winrt.exe"] + [p.resolve() for p in args.consumer]
        xbox_projection = output / "completed/Microsoft.Xbox.dll"
        xbox_projection.parent.mkdir(exist_ok=True)
        run("complete-xbox", [completer.name, windows(xbox_metadata), windows(bootstrap), windows(xbox_projection)] + [windows(p) for p in all_consumers], marker="Completed ")
        copy_owned_file(xbox_projection, xbox_folder / "Microsoft.Xbox.dll")
        resolver = compile_file("resolve-xbox.exe", [repo / "tests/winrt_projection/resolve_references.cs"], ["/r:Mono.Cecil.dll"])
        run("resolve-xbox", [resolver.name, "--assembly", "Microsoft.Xbox", windows(xbox_projection.parent)] + [windows(p) for p in all_consumers], marker="0 unresolved")
        run("xbox-offline-contract", [xbox_consumer.name, abi_version], cwd=xbox_folder, marker="PASS: Xbox projection uses native signed-out user and service client")
        return xbox_projection

    try:
        if args.page_only:
            generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs"], ["/r:Mono.Cecil.dll"])
            generated = output / "Windows.generated.cs"
            run("generate", [generator.name, windows(metadata), windows(repo / "host/winrt/bootstrap.xml"), windows(generated)])
            runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs", "storage_abi.cs", "array_buffer.cs")]
            projection = compile_file("projection/Windows.dll", runtime + [generated], ["/target:library"])
            probe_page(projection)
            return 0
        if args.splash_only:
            generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs"], ["/r:Mono.Cecil.dll"])
            generated = output / "Windows.generated.cs"
            run("generate", [generator.name, windows(metadata), windows(repo / "host/winrt/bootstrap.xml"), windows(generated)])
            runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs", "storage_abi.cs", "array_buffer.cs")]
            projection = compile_file("projection/Windows.dll", runtime + [generated], ["/target:library"])
            probe_splash(projection, generator)
            return 0
        if args.network_only:
            generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs"], ["/r:Mono.Cecil.dll"])
            generated = output / "Windows.generated.cs"
            run("generate", [generator.name, windows(metadata), windows(repo / "host/winrt/bootstrap.xml"), windows(generated)])
            runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs", "storage_abi.cs", "array_buffer.cs")]
            projection = compile_file("projection/Windows.dll", runtime + [generated], ["/target:library"])
            probe_network(projection, generator)
            return 0
        if args.io_only:
            generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs"], ["/r:Mono.Cecil.dll"])
            generated = output / "Windows.generated.cs"
            run("generate", [generator.name, windows(metadata), windows(repo / "host/winrt/bootstrap.xml"), windows(generated)])
            runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs")]
            projection = compile_file("projection/Windows.dll", runtime + [repo / "host/winrt/storage_abi.cs", repo / "host/winrt/array_buffer.cs", generated], ["/target:library"])
            check_io_metadata(generator)
            bridge = probe_async(projection)
            probe_io(projection, bridge)
            return 0
        if args.async_only:
            generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs"], ["/r:Mono.Cecil.dll"])
            generated = output / "Windows.generated.cs"
            run("generate", [generator.name, windows(metadata), windows(repo / "host/winrt/bootstrap.xml"), windows(generated)])
            runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs")]
            projection = compile_file("projection/Windows.dll", runtime + [repo / "host/winrt/storage_abi.cs", repo / "host/winrt/array_buffer.cs", generated], ["/target:library"])
            probe_async(projection)
            return 0
        if args.storage_only:
            marker = compile_file("mark-reference.exe", [repo / "tests/winrt_projection/mark_reference.cs"], ["/r:Mono.Cecil.dll"])
            generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs"], ["/r:Mono.Cecil.dll"])
            generated = output / "Windows.generated.cs"
            run("generate", [generator.name, windows(metadata), windows(repo / "host/winrt/bootstrap.xml"), windows(generated)])
            check_storage_metadata(generator)
            runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs")]
            projection = compile_file("projection/Windows.dll", runtime + [repo / "host/winrt/storage_abi.cs", repo / "host/winrt/array_buffer.cs", generated], ["/target:library"])
            probe_storage(marker, projection)
            return 0
        if args.xbox_only:
            marker = compile_file("mark-reference.exe", [repo / "tests/winrt_projection/mark_reference.cs"], ["/r:Mono.Cecil.dll"])
            generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs"], ["/r:Mono.Cecil.dll"])
            completer = compile_file("complete.exe", [repo / "host/winrt/complete.cs"], ["/r:Mono.Cecil.dll"])
            probe_xbox(marker, generator, completer)
            return 0
        if args.runtime_case:
            executable = compile_file("runtime.exe", [repo / "host/winrt/abi.cs", repo / "host/winrt/ccw.cs", repo / "host/winrt/dispatcher.cs", repo / "host/winrt/property_abi.cs", repo / "host/winrt/collections.cs",
                                      repo / "tests/winrt_projection/runtime.cs"])
            run("runtime-" + args.runtime_case, [executable.name, args.runtime_case], marker="PASS: runtime " + args.runtime_case)
            return 0
        runtime = compile_file("runtime.exe", [repo / "host/winrt/abi.cs", repo / "host/winrt/ccw.cs", repo / "host/winrt/dispatcher.cs", repo / "host/winrt/property_abi.cs", repo / "host/winrt/collections.cs",
                               repo / "tests/winrt_projection/runtime.cs"])
        for case in ("inspectable", "identity", "stale", "invocation", "reentrant"):
            run("runtime-" + case, [runtime.name, case], marker="PASS: runtime " + case)
        # The earlier bridge remains an independent native ownership/HRESULT check.
        bridge = compile_file("AppxPortable.WinRT.dll", [repo / "host/winrt" / name
                              for name in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs", "package.cs", "assembly.cs")], ["/target:library"])
        legacy = compile_file("managed-winrt-bridge.exe", [repo / "tests/managed_winrt_bridge.cs"], ["/r:" + windows(bridge)])
        folder = fixture("legacy", legacy.name, "ManagedWinrtBridgeTest", "CN=bridge-đá-💎")
        for path in (bridge, legacy):
            copy_owned_file(path, folder / path.name)
        run("legacy-bridge", [legacy.name], cwd=folder, marker="PASS: managed WinRT activation")
        reference = compile_file("reference/Windows.dll", [repo / "tests/winrt_projection/reference.cs"], ["/target:library"])
        consumer = compile_file("consumer.exe", [repo / "tests/winrt_projection/consumer.cs"], ["/r:" + windows(reference)])
        marker = compile_file("mark-reference.exe", [repo / "tests/winrt_projection/mark_reference.cs"], ["/r:Mono.Cecil.dll"])
        run("mark-reference", [marker.name, consumer.name, "consumer-winrt.exe"], marker="flags=WindowsRuntime")
        folder = fixture("fixture", consumer.name, "GeneratedProjectionTest", "CN=projection-đá-💎")
        copy_owned_file(output / "consumer-winrt.exe", folder / consumer.name)
        copy_owned_file(reference, folder / "Windows.dll")
        run("unimplemented-contract", [consumer.name], cwd=folder, succeeds=False,
            marker="NotImplementedException: Windows.ApplicationModel.Package.Current")
        generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs"], ["/r:Mono.Cecil.dll"])
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
        projection = compile_file("projection/Windows.dll", [repo / "host/winrt/abi.cs", repo / "host/winrt/ccw.cs", repo / "host/winrt/dispatcher.cs", repo / "host/winrt/property_abi.cs", repo / "host/winrt/collections.cs", repo / "host/winrt/storage_abi.cs", repo / "host/winrt/array_buffer.cs", generated], ["/target:library"])
        copy_owned_file(projection, folder / "Windows.dll")
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
        mutant = compile_file("mutant/Windows.dll", [repo / "host/winrt/abi.cs", repo / "host/winrt/ccw.cs", repo / "host/winrt/dispatcher.cs", repo / "host/winrt/property_abi.cs", repo / "host/winrt/collections.cs", repo / "host/winrt/storage_abi.cs", repo / "host/winrt/array_buffer.cs", target], ["/target:library"])
        try:
            copy_owned_file(mutant, folder / "Windows.dll")
            run("changed-iid-rejected", [consumer.name], cwd=folder, succeeds=False, marker="80004002")
        finally:
            copy_owned_file(projection, folder / "Windows.dll")
        application = compile_file("application.exe", [repo / "tests/winrt_projection/application.cs"], ["/r:" + windows(reference)])
        run("mark-application", [marker.name, application.name, "application-winrt.exe"], marker="flags=WindowsRuntime")
        app = fixture("application", application.name, "ApplicationProjectionTest", "CN=projection")
        copy_owned_file(output / "application-winrt.exe", app / application.name)
        copy_owned_file(projection, app / "Windows.dll")
        run("application-launch", [application.name], cwd=app, marker="PASS: managed App.OnLaunched ran on the shim view thread")
        launch = (output / "application-launch.log").read_text(errors="replace")
        require("PASS: managed dependency properties use the shim value store" in launch,
                "dependency-property fixture did not finish")
        require("PASS: Suspending raised on the managed App" in launch and "UNUSED HANDLER" not in launch,
                "Suspending must reach the subscribed handler and not the removed one")
        run("mark-abi-setters", [marker.name, application.name, "application-abi.exe", "--abi-setters"], marker="flags=WindowsRuntime")
        copy_owned_file(output / "application-abi.exe", app / "application-abi.exe")
        run("abi-setter-before-completion", ["application-abi.exe"], cwd=app, succeeds=False, marker="put_Content")
        run("application-exception", [application.name, "throw"], cwd=app, marker="PASS: OnLaunched exception crossed the native boundary; callback released")
        unimplemented = compile_file("unimplemented.exe", [repo / "tests/winrt_projection/unimplemented.cs"], ["/r:" + windows(reference)])
        run("mark-unimplemented", [marker.name, unimplemented.name, "unimplemented-winrt.exe"], marker="flags=WindowsRuntime")
        missing = fixture("unimplemented", unimplemented.name, "UnimplementedProjectionTest", "CN=projection")
        copy_owned_file(output / "unimplemented-winrt.exe", missing / unimplemented.name)
        copy_owned_file(projection, missing / "Windows.dll")
        run("before-completion", [unimplemented.name], cwd=missing, succeeds=False, marker="UNBOUND:")
        resolver = compile_file("resolve-references.exe", [repo / "tests/winrt_projection/resolve_references.cs"], ["/r:Mono.Cecil.dll"])
        completer = compile_file("complete.exe", [repo / "host/winrt/complete.cs"], ["/r:Mono.Cecil.dll"])
        consumers = [output / "unimplemented-winrt.exe", output / "consumer-winrt.exe", output / "application-winrt.exe", output / "application-abi.exe"]
        consumers += [p.resolve() for p in args.consumer]
        run("resolve-before-completion", [resolver.name, windows(projection.parent)] + [windows(p) for p in consumers],
            succeeds=False, marker="UNRESOLVED ")
        completed = output / "completed/Windows.dll"
        completed.parent.mkdir(exist_ok=True)
        run("complete", [completer.name, windows(metadata), windows(projection), windows(completed)] + [windows(p) for p in consumers], marker="Completed ")
        run("resolve-completed", [resolver.name, windows(completed.parent)] + [windows(p) for p in consumers], marker="0 unresolved")
        copy_owned_file(completed, missing / "Windows.dll")
        run("after-completion", [unimplemented.name], cwd=missing, marker="UNIMPLEMENTED: Windows.Devices.Geolocation.Geolocator..ctor")
        # Completion must keep implemented members native, not replace them with stubs.
        copy_owned_file(completed, folder / "Windows.dll")
        run("completed-contract", [consumer.name], cwd=folder, marker="PASS: generated Windows projection")
        copy_owned_file(completed, app / "Windows.dll")
        run("completed-application", [application.name], cwd=app, marker="PASS: managed App.OnLaunched ran on the shim view thread")
        run("completed-abi-setter", ["application-abi.exe"], cwd=app, marker="PASS: managed App.OnLaunched ran on the shim view thread")
        check_storage_metadata(generator)
        probe_storage(marker, projection)
        probe_page(projection)
        probe_splash(projection, generator)
        probe_network(projection, generator)
        check_io_metadata(generator)
        task_bridge = probe_async(projection)
        probe_io(projection, task_bridge)
        xbox_projection = probe_xbox(marker, generator, completer) if args.xbox_metadata else None
        if args.game:
            source = args.game
            paths = [source / "appx-host.exe", source / "AppxManifest.xml"] + [p.resolve() for p in args.consumer]
            paths += [source / name for name in ("appxportable.dll", "Windows.dll", "Microsoft.Xbox.dll", "System.Runtime.WindowsRuntime.dll") if (source / name).is_file()]
            def hashes():
                return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
            before = hashes()
            game = output / "game"
            require(not game.exists(), "game output already exists; choose a fresh --output directory")
            # Keep links as links: an installed game's own Wine prefix maps z: to /.
            # The launch uses --prefix, so that prefix is not copied at all.
            shutil.copytree(source, game, symlinks=True, ignore=shutil.ignore_patterns("*.log", ".wine-prefix"))
            copy_owned_file(shim, game / "appxportable.dll")
            copy_owned_file(completed, game / "Windows.dll")
            copy_owned_file(task_bridge, game / task_bridge.name)
            if xbox_projection:
                copy_owned_file(xbox_projection, game / "Microsoft.Xbox.dll")
            run("game-launch", ["appx-host.exe", "--trace"], cwd=game, succeeds=False,
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
