#!/usr/bin/env python3
"""Package the source-only component projection tool for install-time Wine Mono."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

GENERATOR = ("generator.cs", "storage_generation.cs", "component_generation.cs")
RUNTIME = ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs")


def package(destination):
    root = Path(__file__).resolve().parents[1]
    files = {name: (root / "host/winrt" / name).read_bytes() for name in GENERATOR + RUNTIME + ("interop.cs",)}
    files["LICENSE"] = (root / "LICENSE").read_bytes()
    manifest = dict(schema=2, generator=GENERATOR, runtime=RUNTIME, interop=("interop.cs",),
                    sha256={name: hashlib.sha256(data).hexdigest() for name, data in files.items()})
    files["tools.json"] = json.dumps(manifest, sort_keys=True).encode()
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("build/appx-portable-winrt-tools.zip"))
    print(package(parser.parse_args().output))
