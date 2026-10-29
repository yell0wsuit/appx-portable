#!/usr/bin/env bash
# Build the WoW64 engine component for the pinned macOS Wine runtime.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
output=${1:-"$root/build/wine-clr"}
mkdir -p "$output"
output=$(cd "$output" && pwd)
version=11.18
source_sha=c6282f6d4daecf11f3ab9f9a2b6c598f67be7e28c363ba2cae8a3c44b611e7a4
work="$root/local/wine-clr-build"
archive="$root/local/wine-src/wine-$version.tar.xz"
mkdir -p "$work" "$(dirname "$archive")"
if [[ ! -f "$archive" ]]; then
  curl -fL -o "$archive.part" "https://dl.winehq.org/wine/source/11.x/wine-$version.tar.xz"
  mv "$archive.part" "$archive"
fi
if command -v sha256sum > /dev/null; then sum=(sha256sum); else sum=(shasum -a 256); fi
echo "$source_sha  $archive" | "${sum[@]}" -c -
source="$work/wine-$version"
rm -rf "$source"
tar xf "$archive" -C "$work"
patch -d "$source" -p1 < "$root/wine/wow64cpu.patch"
if [[ $(uname) == Darwin ]]; then
  export PATH="$(brew --prefix bison)/bin:$(brew --prefix llvm)/bin:$(brew --prefix lld)/bin:$PATH"
  host=(arch -x86_64)
  host_cc='clang -arch x86_64'
else
  host=()
  host_cc=gcc
fi
objects="$work/objects"
mkdir -p "$objects"
cd "$objects"
if [[ ! -f Makefile ]]; then
  ${host[@]+"${host[@]}"} "../wine-$version/configure" --enable-archs=i386,x86_64 --without-mingw \
    --without-freetype --without-gnutls --without-x --without-vulkan --without-opencl --without-pcap \
    --without-sdl --without-gstreamer --without-cups --without-sane --without-usb --without-v4l2 \
    --without-krb5 --without-netapi --without-gphoto --without-pulse --without-alsa --without-oss \
    --without-capi --without-dbus --without-inotify --without-ffmpeg --without-wayland --without-coreaudio \
    CC="$host_cc" i386_CC=clang x86_64_CC=clang > configure.log 2>&1 \
    || { tail -20 configure.log >&2; exit 1; }
fi
${host[@]+"${host[@]}"} make -j"$(getconf _NPROCESSORS_ONLN)" \
  dlls/wow64cpu/x86_64-windows/wow64cpu.dll > make.log 2>&1 \
  || { tail -20 make.log >&2; exit 1; }
# This is an engine builtin, so retain its Wine builtin marker.
cp dlls/wow64cpu/x86_64-windows/wow64cpu.dll "$output/wow64cpu.dll"
cp "$archive" "$output/wine-$version.tar.xz"
cp "$root/wine/wow64cpu.patch" "$root/scripts/build-wine-clr.sh" "$output/"
echo "Built Wine $version WoW64 component in $output"
