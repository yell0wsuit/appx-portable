#!/usr/bin/env python3
"""Build the release's prebuilt WinRT assemblies for managed games.

Windows.dll is the interop bootstrap projection completed against every type of the
given SDK contracts; System.Runtime.WindowsRuntime.dll is the task and stream bridge;
AppxPortable.WinRT.Interop.dll is the contract they share with package components.
Contract metadata is read here only and never copied into the output.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
WINRT = REPO / "host/winrt"
GENERATOR = ("generator.cs", "storage_generation.cs", "component_generation.cs")
RUNTIME = ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs", "storage_abi.cs", "array_buffer.cs")
BRIDGE = ("task_extensions.cs", "stream_extensions.cs")
REFERENCES = ("mscorlib.dll", "System.dll", "System.Core.dll", "System.Xml.dll")
INTEROP = "AppxPortable.WinRT.Interop.dll"
TASKS = "System.Runtime.WindowsRuntime.dll"


class Toolchain:
    """.NET Framework's csc.exe on Windows, or the Mono compiler inside a Wine."""

    def __init__(self, wine=None, framework=None):
        self.wine = Path(wine) if wine else None
        if self.wine:
            engine = self.wine.resolve().parent.parent
            monos = sorted((engine / "share/wine/mono").glob("wine-mono-*/lib/mono"))
            if len(monos) != 1:
                raise SystemExit("Cannot identify one Wine Mono in " + str(engine))
            self.compiler = [str(self.wine), self.path(monos[0] / "4.5/mcs.exe")]
            self.references = monos[0] / "4.8-api"
        else:
            framework = Path(framework or os.path.join(os.environ.get("WINDIR", r"C:\Windows"), r"Microsoft.NET\Framework\v4.0.30319"))
            self.compiler = [str(framework / "csc.exe")]
            self.references = framework

    def path(self, path):
        return "Z:" + str(Path(path).resolve()).replace("/", "\\") if self.wine else str(path)

    def tool(self, executable):
        return [str(self.wine), self.path(executable)] if self.wine else [str(executable)]

    def compile(self, output, sources, extra=()):
        return self.compiler + ["/nologo", "/noconfig", "/nostdlib+", "/warnaserror+", "/langversion:5", "/platform:x86",
                                "/out:" + self.path(output)] + \
            ["/r:" + self.path(self.references / name) for name in REFERENCES] + list(extra) + [self.path(s) for s in sources]


def commands(toolchain, metadata, cecil, output, work):
    """Every step of the build, in order, as (name, command)."""
    t = toolchain
    generator, completer = work / "generator.exe", work / "complete.exe"
    generated, bootstrap = work / "Windows.generated.cs", work / "bootstrap/Windows.dll"
    interop, compiled_tasks = output / INTEROP, work / ("compiled/" + TASKS)
    interop_flags = ["/target:library", "/define:APPX_PORTABLE_INTEROP", "/r:" + t.path(interop)]
    return [
        ("compile-generator", t.compile(generator, [WINRT / n for n in GENERATOR], ["/r:" + t.path(cecil)])),
        ("compile-completer", t.compile(completer, [WINRT / "complete.cs"], ["/r:" + t.path(cecil)])),
        ("generate-bootstrap", t.tool(generator) + [t.path(metadata), t.path(WINRT / "bootstrap.xml"), t.path(generated), "--interop"]),
        ("compile-interop", t.compile(interop, [WINRT / "interop.cs"], ["/target:library"])),
        ("compile-bootstrap", t.compile(bootstrap, [WINRT / n for n in RUNTIME] + [generated], interop_flags)),
        ("complete-surface", t.tool(completer) + ["--all", t.path(metadata), t.path(bootstrap), t.path(output / "Windows.dll"),
                                                  t.path(output / "surface-report.txt")]),
        # C# binds WinRT setters as set_X against the bootstrap; the surface names them put_X.
        ("compile-tasks", t.compile(compiled_tasks, [WINRT / n for n in BRIDGE], ["/target:library", "/r:" + t.path(bootstrap), "/r:" + t.path(interop)])),
        ("rebind-tasks", t.tool(completer) + ["--rebind", t.path(output / "Windows.dll"), t.path(compiled_tasks), t.path(output / TASKS)]),
        # The artifact itself, on this runtime: every type loads and every signature type resolves.
        ("compile-surface-check", t.compile(work / "surface-load.exe", [REPO / "tests/winrt_projection/surface_load.cs"])),
        ("verify-surface", t.tool(work / "surface-load.exe") + [t.path(output / "Windows.dll")]),
    ]


def build(toolchain, metadata, cecil, output, work):
    for directory in (output, work, work / "bootstrap", work / "compiled"):
        directory.mkdir(parents=True, exist_ok=True)
    (work / "Mono.Cecil.dll").write_bytes(Path(cecil).read_bytes())
    env = dict(os.environ, WINEDEBUG="-all", MVK_CONFIG_LOG_LEVEL="0")
    for name in ("MONO_PATH", "MONO_ENV_OPTIONS", "WINE_MONO_AOT", "WINEDLLOVERRIDES"):
        env.pop(name, None)
    for name, command in commands(toolchain, metadata, work / "Mono.Cecil.dll", output, work):
        log = work / (name + ".log")
        # Wine services can hold a pipe open past the tool's exit; log to a file.
        with log.open("w") as stream:
            code = subprocess.run(command, cwd=work, env=env, stdin=subprocess.DEVNULL, stdout=stream,
                                  stderr=subprocess.STDOUT, start_new_session=True, timeout=1800, check=False).returncode
        if code:
            raise SystemExit(name + " failed:\n" + log.read_text(errors="replace")[-4000:])
        print(name, "ok")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metadata", type=Path, required=True, help="directory of SDK contract .winmd files")
    parser.add_argument("--cecil", type=Path, required=True, help="Mono.Cecil.dll, used only at build time")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--work", type=Path, help="intermediate directory (default: OUTPUT/../winrt-release-work)")
    parser.add_argument("--wine", type=Path, help="build with this Wine's Mono instead of .NET Framework")
    args = parser.parse_args()
    output = args.output.resolve()
    build(Toolchain(args.wine), args.metadata.resolve(), args.cecil.resolve(), output,
          (args.work or output.parent / "winrt-release-work").resolve())
    sys.exit(0)
