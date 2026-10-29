#!/usr/bin/env python3
"""Run rendertest under an explicit Wine engine with the game graphics stack, keeping its frame."""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

GRAPHICS = ("d3d11.dll", "dxgi.dll", "wined3d.dll")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wine", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True,
                        help="x86 build folder with rendertest.exe, appxportable.dll and the patched graphics DLLs")
    parser.add_argument("--output", type=Path, required=True, help="fresh folder for the run")
    parser.add_argument("--hold-ms", type=int, default=0, help="keep the presented frame on screen this long")
    parser.add_argument("--screenshot", action="store_true", help="capture the macOS desktop while the frame is held")
    args = parser.parse_args()
    build, output = args.build.resolve(), args.output.resolve()
    if output.exists():
        parser.error("output folder already exists; choose a fresh one")
    if args.screenshot and (sys.platform != "darwin" or args.hold_ms < 2000):
        parser.error("--screenshot needs macOS and --hold-ms of at least 2000")
    graphics = all((build / name).is_file() for name in GRAPHICS)
    if not graphics:
        parser.error("missing patched graphics DLLs in " + str(build) + "; run scripts/build-wined3d.sh")
    output.mkdir(parents=True)
    for name in ("rendertest.exe", "appxportable.dll") + GRAPHICS:
        shutil.copyfile(build / name, output / name)
    env = dict(os.environ, WINEPREFIX=str(args.prefix.resolve()), WINEDEBUG="-all", MVK_CONFIG_LOG_LEVEL="0",
               WINEDLLOVERRIDES="d3d11,dxgi,wined3d=n", WINE_D3D_CONFIG="renderer=vulkan")
    for key in ("WINE_MONO_AOT", "MONO_PATH", "MONO_ENV_OPTIONS", "APPX_RENDER_HOLD_MS"):
        env.pop(key, None)
    if args.hold_ms:
        env["APPX_RENDER_HOLD_MS"] = str(args.hold_ms)
    log = output / "rendertest.log"
    with log.open("w") as stream:
        child = subprocess.Popen([str(args.wine.resolve()), "rendertest.exe"], cwd=output, env=env,
                                 stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
        screenshot = None
        if args.screenshot:
            # Wine startup and the first present take a moment; capture inside the hold.
            time.sleep(min(args.hold_ms / 1000 - 0.5, 1.5 + args.hold_ms / 2000))
            screenshot = output / "desktop.png"
            subprocess.run(["screencapture", "-x", str(screenshot)], check=False)
        try:
            code = child.wait(timeout=60 + args.hold_ms / 1000)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            child.wait()
            code = "timeout"
    text = log.read_text(errors="replace")
    frame = output / "appx-frame.bmp"
    if frame.is_file() and shutil.which("sips"):
        subprocess.run(["sips", "-s", "format", "png", str(frame), "--out", str(output / "frame.png")],
                       check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    result = dict(exit=code, passed=code == 0 and "rendered=1" in text, frame=str(frame) if frame.is_file() else None,
                  screenshot=str(screenshot) if screenshot and screenshot.is_file() else None,
                  lines=[line for line in text.splitlines() if line.startswith("rendertest:")])
    (output / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
