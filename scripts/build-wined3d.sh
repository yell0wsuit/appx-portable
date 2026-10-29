#!/usr/bin/env bash
set -euo pipefail
# Usage: build-wined3d.sh [x86|x64] [BUILD_DIRECTORY]
# Builds a matched D3D11/DXGI/WineD3D stack with wine/wined3d.patch, by default
# for both architectures into build/macos-x86 and build/macos-x64. The stack
# exposes public Windows APIs to the installed Wine, rather than mixing its
# private wined3d exports with another release's d3d11/dxgi. installappx.py
# stages it from a --build folder and the Wine launcher loads it.
root=$(cd "$(dirname "$0")/.." && pwd)
case "${1:-both}" in
  x86) architectures=(x86) ;;
  x64) architectures=(x64) ;;
  both) architectures=(x86 x64) ;;
  *) echo "Usage: $0 [x86|x64] [BUILD_DIRECTORY]" >&2; exit 2 ;;
esac
if [[ $# -ge 2 && ${#architectures[@]} -ne 1 ]]; then
  echo "Name one architecture with a build directory." >&2
  exit 2
fi
# Resolve the build directory now; the build below runs from the object tree.
if [[ $# -ge 2 ]]; then
  mkdir -p "$2"
  output=$(cd "$2" && pwd)
fi
# Pin the source this patch is maintained against. Runtime Wine version is
# deliberately irrelevant: all three consumers of the private ABI ship together.
version=11.18
sha256=c6282f6d4daecf11f3ab9f9a2b6c598f67be7e28c363ba2cae8a3c44b611e7a4
if [[ $(uname) == Darwin ]]; then
  for tool in /opt/homebrew/opt/bison/bin/bison /opt/homebrew/opt/llvm/bin/clang /opt/homebrew/opt/lld/bin/lld-link; do
    [[ -x "$tool" ]] || { echo "Missing $tool: brew install bison llvm lld" >&2; exit 1; }
  done
  export PATH="/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/llvm/bin:/opt/homebrew/opt/lld/bin:$PATH"
  # The Wine bundle is x86_64 macOS code, so the build tools run under Rosetta.
  host=(arch -x86_64)
  host_cc='clang -arch x86_64'
else
  # CI builds on x86_64 Linux; the PE DLLs are the same for any host.
  for tool in bison flex clang lld-link llvm-strip; do
    command -v "$tool" > /dev/null || { echo "Missing $tool: apt install bison flex clang lld llvm" >&2; exit 1; }
  done
  host=()
  host_cc=gcc
fi

work="$root/local/wine-src"
tarball="$work/wine-$version.tar.xz"
mkdir -p "$work"
if [[ ! -f "$tarball" ]]; then
  series=${version%%.*}.x
  [[ "${version#*.}" != 0 ]] || series=${version%%.*}.0
  curl -fL -o "$tarball.part" "https://dl.winehq.org/wine/source/$series/wine-$version.tar.xz"
  mv "$tarball.part" "$tarball"
fi
if command -v sha256sum > /dev/null; then sum=(sha256sum); else sum=(shasum -a 256); fi
echo "$sha256  $tarball" | "${sum[@]}" -c - > /dev/null || { echo "Checksum mismatch: $tarball" >&2; exit 1; }

# A fresh, patched source tree every time; the object tree is reused.
source="$work/wine-$version"
rm -rf "$source"
tar xf "$tarball" -C "$work"
patch -d "$source" -p1 --quiet < "$root/wine/wined3d.patch"
objects="$work/build-$version"
mkdir -p "$objects"
cd "$objects"
# Only the PE side of wined3d is built, with clang as the Windows compiler. A
# relative source path keeps the home folder out of the DLL's messages.
if [[ ! -f Makefile ]]; then
  ${host[@]+"${host[@]}"} "../wine-$version/configure" --enable-archs=i386,x86_64 --without-mingw \
    --without-freetype --without-gnutls --without-x --without-vulkan --without-opencl --without-pcap \
    --without-sdl --without-gstreamer --without-cups --without-sane --without-usb --without-v4l2 \
    --without-krb5 --without-netapi --without-gphoto --without-pulse --without-alsa --without-oss \
    --without-capi --without-dbus --without-inotify --without-ffmpeg --without-wayland --without-coreaudio \
    CC="$host_cc" i386_CC=clang x86_64_CC=clang > configure.log 2>&1 \
    || { tail -20 configure.log >&2; exit 1; }
fi

for architecture in "${architectures[@]}"; do
  case "$architecture" in
    x86) pe=i386-windows ;;
    x64) pe=x86_64-windows ;;
  esac
  build=${output:-"$root/build/macos-$architecture"}
  mkdir -p "$build"
  ${host[@]+"${host[@]}"} make -j"$(getconf _NPROCESSORS_ONLN)" "dlls/wined3d/$pe/wined3d.dll" \
    "dlls/d3d11/$pe/d3d11.dll" "dlls/dxgi/$pe/dxgi.dll" > "make-$architecture.log" 2>&1 \
    || { tail -20 "make-$architecture.log" >&2; exit 1; }

  for dll in wined3d d3d11 dxgi; do
    llvm-strip -o "$build/$dll.dll.part" "dlls/$dll/$pe/$dll.dll"
    # Wine treats a DLL carrying its "Wine builtin DLL" marker as builtin and
    # won't load it as a native override from the game folder. Clearing the
    # marker only loses wined3d's Unix library, which serves Vulkan video decoding.
    python3 -I - "$build/$dll.dll.part" <<'CLEAR_MARKER'
import sys
path = sys.argv[1]
data = bytearray(open(path, 'rb').read())
if data[0x40:0x50] != b'Wine builtin DLL':
    sys.exit(f'{path} has no builtin marker at 0x40')
data[0x40:0x50] = bytes(16)
open(path, 'wb').write(data)
CLEAR_MARKER
    mv "$build/$dll.dll.part" "$build/$dll.dll"
  done
  python3 -I - "$build" "$version" <<'MANIFEST'
import hashlib, json, pathlib, sys
build = pathlib.Path(sys.argv[1])
data = {'source_version': sys.argv[2], 'sha256': {
    name: hashlib.sha256((build / name).read_bytes()).hexdigest()
    for name in ('d3d11.dll', 'dxgi.dll', 'wined3d.dll')}}
temporary = build / 'wine-graphics.json.part'
temporary.write_text(json.dumps(data, indent=2) + '\n')
temporary.replace(build / 'wine-graphics.json')
MANIFEST
  echo "Built $architecture D3D11/DXGI/WineD3D stack in $build (source Wine $version)"
done
