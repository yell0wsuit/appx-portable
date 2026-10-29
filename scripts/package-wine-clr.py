#!/usr/bin/env python3
"""Package the pinned Gcenx runtime with the source-built WoW64 component."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile
import urllib.request

# The Wine Devel build the patched runtime is made from; pyappxportable pins the same one.
WINE_DEVEL = (
    "https://github.com/Gcenx/macOS_Wine_builds/releases/download/11.18/wine-devel-11.18-osx64.tar.xz",
    "aa0ea4c82e636ae7bca2076387cb0a5affa26509ad13f119ecd0d62bd7ba6f82",
)


def download_verified(url, sha256):
    """Download url, refusing data whose SHA-256 is not the pinned one."""
    with urllib.request.urlopen(url) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != sha256:
        raise SystemExit(f"{url} does not have the expected SHA-256 {sha256}")
    return data


def extract_tar_xz(data, destination):
    """Unpack a .tar.xz into destination, refusing entries that would land outside it."""
    root = Path(destination).resolve()

    def inside(path):
        return path == root or root in path.parents

    with tarfile.open(fileobj=io.BytesIO(data), mode="r:xz") as archive:
        members = archive.getmembers()
        for member in members:
            target = (root / member.name).resolve()
            if member.issym():
                link = (target.parent / member.linkname).resolve()
            elif member.islnk():
                link = (root / member.linkname).resolve()
            else:
                link = target
            if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
                raise SystemExit(f"Refusing {member.name}: not a file, folder or link")
            if not inside(target) or not inside(link):
                raise SystemExit(f"Refusing {member.name}: it leaves {root}")
        if hasattr(tarfile, "data_filter"):
            # Checked above; "tar" keeps permissions and the bundle's own links.
            archive.extractall(root, members, filter="tar")
        else:
            archive.extractall(root, members)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--base-archive", type=Path)
    args = parser.parse_args()
    build = args.build.resolve()
    cpu = build / "wow64cpu.dll"
    data = cpu.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    if (data[pe:pe + 4] != b"PE\0\0" or struct.unpack_from("<H", data, pe + 4)[0] != 0x8664
            or data[0x40:0x50] != b"Wine builtin DLL"):
        raise SystemExit("wow64cpu.dll must be a Wine x64 builtin")
    url, expected = WINE_DEVEL
    if args.base_archive:
        archive = args.base_archive.read_bytes()
        if hashlib.sha256(archive).hexdigest() != expected:
            raise SystemExit("base archive checksum mismatch")
    else:
        print("Downloading pinned Gcenx Wine 11.18", flush=True)
        archive = download_verified(url, expected)
    stage = build / "runtime"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir()
    extract_tar_xz(archive, stage)
    app = stage / "Wine Devel.app"
    target = app / "Contents/Resources/wine/lib/wine/x86_64-windows/wow64cpu.dll"
    if not target.is_file():
        raise SystemExit("base archive has no expected WoW64 component")
    shutil.copyfile(cpu, target)
    # Replacing a sealed resource invalidates the upstream app signature.
    subprocess.run(["codesign", "--force", "--deep", "--sign", "-",
                    "--preserve-metadata=entitlements,requirements,flags,runtime", str(app)], check=True)
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)
    manifest = dict(schema=1, wine_version="11.18", cpu_sha256=hashlib.sha256(data).hexdigest(),
                    base_url=url, base_sha256=expected,
                    patch_sha256=hashlib.sha256((build / "wow64cpu.patch").read_bytes()).hexdigest(),
                    source_sha256=hashlib.sha256((build / "wine-11.18.tar.xz").read_bytes()).hexdigest())
    (stage / "wine-clr.json").write_text(json.dumps(manifest, indent=2) + "\n")
    artifact = build / "appx-portable-wine-clr-macos.tar.xz"
    print("Packaging patched runtime", flush=True)
    with tarfile.open(artifact, "w:xz", preset=1) as output:
        output.add(app, arcname=app.name)
        output.add(stage / "wine-clr.json", arcname="wine-clr.json")
    with tarfile.open(build / "appx-portable-wine-clr-sources.tar.xz", "w:xz", preset=1) as output:
        for name in ("wine-11.18.tar.xz", "wow64cpu.patch", "build-wine-clr.sh"):
            output.add(build / name, arcname=name)
        output.add(stage / "wine-clr.json", arcname="wine-clr.json")
    print(json.dumps(dict(archive=str(artifact), sha256=hashlib.sha256(artifact.read_bytes()).hexdigest())))


if __name__ == "__main__":
    main()
