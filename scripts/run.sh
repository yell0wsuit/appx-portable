#!/usr/bin/env bash
set -euo pipefail
# Usage: run.sh [GAME_DIRECTORY] [--trace] [--capture] [--diagnostics] [--passthrough] [--unlock-trial]
root=$(cd "$(dirname "$0")/.." && pwd)
game="$root/local/game"
# --log always: the relaunch check below reads the log.
flags=(--log)
for arg in "$@"; do
  case "$arg" in
    --*) flags+=("$arg") ;;
    *) game=$arg ;;
  esac
done
export WINEPREFIX=${WINEPREFIX:-"$root/local/wine-test"}
export WINEDEBUG=${WINEDEBUG:--all}
export MVK_CONFIG_LOG_LEVEL=${MVK_CONFIG_LOG_LEVEL:-0}
# The proxies load the shim; the VC runtimes are the package's own (VCLibs 110, 120, 140).
export WINEDLLOVERRIDES="d3dcompiler_47,xaudio2_8,dwrite=n,b;msvcr110,msvcp110,vccorlib110,msvcr120_app,msvcp120_app,vccorlib120_app,vcruntime140_app,msvcp140_app,vccorlib140_app=n${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}"
wine=${WINE:-"$HOME/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine"}
[[ -x "$wine" ]] || { echo 'Set WINE to your Wine executable.' >&2; exit 1; }
# Share managed/native executable selection with the Windows Python launcher.
exe=$(python3 -c '
import pathlib, sys
sys.path.insert(0, sys.argv[1])
from play import launch_executable
print(launch_executable(pathlib.Path(sys.argv[2])))' "$root/scripts" "$game")
[[ -f "$game/$exe" && -f "$game/appxportable.dll" ]] || { echo 'Run setup.sh first.' >&2; exit 1; }
# A crash otherwise opens Wine's crash dialog, which keeps the process alive
# until closed and stalls the relaunch below; print the backtrace and exit.
"$wine" reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f > /dev/null 2>&1 || true
cd "$game"
# Wine 11 WoW64 sometimes kills the process inside wined3d's first OpenGL
# setup ("Exception frame is not in stack limits"). That startup death leaves
# "CreateDXGIFactory1 begin" without a result in the log; retry only that case.
status=0
for attempt in 1 2 3; do
  "$wine" "$exe" "${flags[@]}" && exit 0 || status=$?
  # Wine launches always log, and each logging run starts a new appx-portable.log.
  run=$(cat appx-portable.log 2>/dev/null || true)
  if grep -q 'CreateDXGIFactory1 begin' <<< "$run" && ! grep -q 'CreateDXGIFactory1 hr=' <<< "$run"; then
    echo "Wine graphics startup crashed (attempt $attempt/3); relaunching." >&2
    continue
  fi
  exit "$status"
done
exit "$status"
