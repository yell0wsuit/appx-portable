#!/usr/bin/env bash
# Build and verify the native bridge for Wine's x86_64 Unix side (also WoW64).
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
output=${1:-"$root/build/wine-clr"}
mkdir -p "$output"
clang -arch x86_64 -mmacosx-version-min=10.15 -fobjc-arc -dynamiclib \
  "$root/wine/game_menu.m" -framework Cocoa -o "$output/appxmenu.so"
clang -arch x86_64 -mmacosx-version-min=10.15 -fobjc-arc \
  "$root/tests/macos/game_menu.m" -framework Cocoa -o "$output/game-menu-test"
"$output/game-menu-test" "$output/appxmenu.so"
