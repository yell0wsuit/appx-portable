#!/usr/bin/env python3
"""Compile and run CLR prerequisite probes with an explicit Wine/prefix."""
import argparse
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess


def run(command, cwd, env, log, timeout):
    with log.open("w") as output:
        child = subprocess.Popen(command, cwd=cwd, env=env, stdout=output,
                                 stderr=subprocess.STDOUT, start_new_session=True)
        try:
            return child.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            child.wait()
            return "timeout"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wine", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--skip-compile", action="store_true", help="reuse compiled fixtures when comparing engine variants")
    parser.add_argument("--compiler", choices=("mono", "roslyn"), default="mono")
    args = parser.parse_args()
    wine, prefix, output = args.wine.resolve(), args.prefix.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG="-all")
    for key in ("WINEDLLOVERRIDES", "WINE_MONO_AOT", "MONO_ENV_OPTIONS", "MONO_PATH", "MONO_THREADS_SUSPEND"):
        env.pop(key, None)
    sources = Path(__file__).resolve().parents[1] / "tests/wine_clr"
    references = list((wine.parent.parent / "share/wine/mono").glob("wine-mono-*/lib/mono/4.8-api"))
    if not args.skip_compile and len(references) != 1:
        raise SystemExit("expected one bundled Wine Mono 4.8 API reference directory")
    compiler_refs = []
    for name in ("mscorlib.dll", "System.dll", "System.Core.dll"):
        if not references:
            break
        path = str(references[0] / name)
        compiler_refs.append("/r:" + ("Z:" + path.replace("/", "\\") if args.compiler == "mono" else path))
    # Wine's Framework csc shim injects 4.0 references even with /noconfig.
    # Invoke its bundled Mono compiler directly with the explicit 4.8 surface.
    compiler = ("Z:" + str(references[0].parent / "4.5/mcs.exe").replace("/", "\\")
                if references else "")
    command = [str(wine), compiler]
    if args.compiler == "roslyn" and not args.skip_compile:
        dotnet = shutil.which("dotnet")
        if not dotnet:
            raise SystemExit("Roslyn check requires a preinstalled .NET SDK")
        sdks = subprocess.check_output([dotnet, "--list-sdks"], text=True).splitlines()
        if not sdks:
            raise SystemExit("no preinstalled .NET SDK found")
        version, location = sdks[-1].split(" [", 1)
        csc = Path(location.rstrip("]")) / version / "Roslyn/bincore/csc.dll"
        command = [dotnet, str(csc)]
    builds = [("hello", "hello-x86", "x86"), ("hello", "hello-x64", "x64"),
              ("smoke", "clr-smoke", "x86"), ("io", "clr-io-x86", "x86"),
              ("io", "clr-io-x64", "x64"), ("worker", "clr-worker", "x86")]
    for source, name, architecture in builds:
        if args.skip_compile:
            continue
        # Keep source paths local to csc's Windows working directory.
        target = output / (source + ".cs")
        target.write_bytes((sources / (source + ".cs")).read_bytes())
        result = run([*command, "/nologo", "/noconfig", "/nostdlib+", "/langversion:5", *compiler_refs,
                      "/platform:" + architecture,
                      "/out:" + name + ".exe", target.name], output, env,
                     output / ("compile-" + name + ".log"), 90)
        if result != 0:
            raise SystemExit("compiler failed: " + str(output / ("compile-" + name + ".log")))
    cases = [("hello-x86", [], "managed probe reached Main: 4"),
             ("hello-x64", [], "managed probe reached Main: 8"),
             ("clr-smoke", [], "x86 CLR smoke PASS"),
             ("clr-io-x86", [], "io probe reached disk"),
             ("clr-io-x86", ["native"], "BCrypt result 0"),
             ("clr-io-x64", [], "io probe reached disk"),
             ("clr-worker", [], "x86 CLR smoke PASS")]
    rows = []
    for name, extra, marker in cases:
        label = name + ("-native" if extra else "")
        log = output / (label + ".log")
        result = run([str(wine), name + ".exe", *extra], output, env, log, 30)
        row = dict(case=label, exit=result, marker_found=marker in log.read_text(errors="replace"))
        rows.append(row)
        print(json.dumps(row), flush=True)
    (output / "results.json").write_text(json.dumps(rows, indent=2) + "\n")
    return int(any(row["exit"] != 0 or not row["marker_found"] for row in rows))


if __name__ == "__main__":
    raise SystemExit(main())
