#!/usr/bin/env python3
# pylint: disable=too-many-lines  # one file by design: players download only this
"""Install a Windows Store game as a portable Appx Portable game folder.

Give it a .appxbundle, a .appx, or an extracted package folder. It extracts the
game, adds the Visual C++ runtime the game needs, downloads the matching Appx
Portable release, applies the patch for a known game, and can write a
00_click2startme launcher. Run it without arguments to be asked for each step.
"""

import argparse
import hashlib
import http.client
import io
import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from xml.etree import ElementTree
import zipfile
from contextlib import ExitStack
from dataclasses import dataclass
from pathlib import Path, PureWindowsPath
from typing import Callable, Dict, Iterable, List, Optional, Tuple

ARCHITECTURES = {0x14C: "x86", 0x8664: "x64"}
# Package files the game never reads.
PACKAGE_METADATA = {
    "appxsignature.p7x",
    "appxblockmap.xml",
    "[content_types].xml",
    "appxmetadata",
    "microsoft.system.package.metadata",
}
# Bundle application packages in order of preference: Windows 8 and 8.1 games
# are tested as x86, Windows 10 games as x64.
BUNDLE_ARCHITECTURES = ("x86", "neutral", "x64")
UNIVERSAL_BUNDLE_ARCHITECTURES = ("x64", "neutral", "x86")
IDENTITY_NAME = re.compile(r"^(?=.*[A-Za-z0-9])[A-Za-z0-9.-]{3,50}$")
INVALID_NAME = re.compile(r'[<>:"|?*\x00-\x1f]')


class InstallError(Exception):
    """A problem the player can fix; main prints it as one line."""


# PE images


@dataclass
class PEInfo:
    """What the installer reads from a PE header."""

    machine: int
    managed: bool
    dll: bool


def pe_info(path: Path) -> PEInfo:
    """Machine, CLR header and DLL flag of a PE image."""
    data = Path(path).read_bytes()[:4096]
    if len(data) < 64 or data[:2] != b"MZ":
        raise InstallError(f"Not a Windows executable or DLL: {path}")
    offset = struct.unpack_from("<I", data, 0x3C)[0]
    if offset + 24 + 96 > len(data) or data[offset : offset + 4] != b"PE\0\0":
        raise InstallError(f"Invalid PE header: {path}")
    machine, characteristics = (
        struct.unpack_from("<H", data, offset + 4)[0],
        struct.unpack_from("<H", data, offset + 22)[0],
    )
    optional = offset + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    # The CLR runtime header is data directory 14.
    clr = optional + (112 if magic == 0x20B else 96) + 14 * 8
    managed = clr + 4 <= len(data) and struct.unpack_from("<I", data, clr)[0] != 0
    return PEInfo(machine, managed, bool(characteristics & 0x2000))


def architecture_of(path: Path) -> str:
    """ "x86" or "x64" for a PE image; any other machine is an error."""
    machine = pe_info(path).machine
    if machine not in ARCHITECTURES:
        raise InstallError(f"Expected an x86 or x64 image: {path}")
    return ARCHITECTURES[machine]


# Package manifest


@dataclass
class Manifest:
    """The parts of AppxManifest.xml the installer uses."""

    name: str
    version: str
    architecture: str
    executable: str
    dependencies: List[str]
    universal: bool = False  # a Windows 10 package: it names a TargetDeviceFamily


def local_name(element) -> str:
    """An XML element's tag without its namespace."""
    return element.tag.rsplit("}", 1)[-1]


def read_manifest(data: bytes) -> Manifest:
    """Parse AppxManifest.xml, refusing names that are unsafe as paths."""
    try:
        root = ElementTree.fromstring(data)
    except ElementTree.ParseError as error:
        raise InstallError(f"Unreadable AppxManifest.xml: {error}") from error
    # By local name: Windows 8, 8.1 and 10 manifests use different namespaces.
    elements = list(root.iter())
    identity = next((e for e in elements if local_name(e) == "Identity"), None)
    application = next((e for e in elements if local_name(e) == "Application"), None)
    if identity is None or application is None:
        raise InstallError("AppxManifest.xml declares no Identity or Application.")
    executable = PureWindowsPath(application.get("Executable", ""))
    if not executable.parts or executable.anchor or ".." in executable.parts:
        raise InstallError(
            f"AppxManifest.xml has an unusable Executable: {application.get('Executable')!r}"
        )
    name = identity.get("Name", "")
    # The identity name becomes a folder name; hold it to the appx schema.
    if not IDENTITY_NAME.match(name):
        raise InstallError(f"AppxManifest.xml has an unusable Identity Name: {name!r}")
    return Manifest(
        name=name,
        version=identity.get("Version", ""),
        architecture=identity.get("ProcessorArchitecture", "neutral").lower(),
        executable=executable.as_posix(),
        dependencies=[
            e.get("Name", "") for e in elements if local_name(e) == "PackageDependency"
        ],
        universal=any(local_name(e) == "TargetDeviceFamily" for e in elements),
    )


# Package extraction


def target_parts(name: str) -> Optional[List[str]]:
    """The staged path of a package entry, or None for one never staged.

    A raw .appx keeps OPC percent-encoded names (%21_Name.xml, User%20Controls);
    installed packages have them decoded, and games open the decoded names."""
    decoded = urllib.parse.unquote(name)
    if decoded.endswith(("/", "\\")):
        return None  # a folder entry
    parts = re.split(r"[\\/]", decoded)
    if (
        decoded.startswith(("/", "\\"))
        or re.match(r"^[A-Za-z]:", decoded)
        or any(part in ("", ".", "..") or INVALID_NAME.search(part) for part in parts)
    ):
        raise InstallError(f"Unsafe file name in package: {name}")
    first = parts[0].lower()
    if first in PACKAGE_METADATA:
        return None
    # Saves, settings and logs belong to the player, not the package.
    if first in ("savedata", "appx-portable.ini") or (
        first.startswith("appx-portable") and first.endswith(".log")
    ):
        return None
    return parts


@dataclass
class Entry:
    """A package file: where it is staged, and how to read it."""

    parts: List[str]
    open: Callable


def zip_entries(archive: zipfile.ZipFile) -> List[Entry]:
    """The staged files of a package zip, every name checked."""
    entries = []
    for info in archive.infolist():
        parts = target_parts(info.filename)
        if parts is not None:
            entries.append(Entry(parts, lambda info=info: archive.open(info)))
    return entries


def folder_entries(folder: Path) -> List[Entry]:
    """The staged files of an extracted package folder; links are refused."""
    entries = []
    for parent, directories, files in os.walk(folder):
        for name in directories + files:
            path = Path(parent) / name
            if path.is_symlink():
                raise InstallError(
                    f"Links are not allowed in the package folder: {path}"
                )
        for name in files:
            path = Path(parent) / name
            parts = target_parts(path.relative_to(folder).as_posix())
            if parts is not None:
                entries.append(Entry(parts, lambda path=path: path.open("rb")))
    return entries


def write_entries(entries: Iterable[Entry], destination: Path, overwrite: bool) -> None:
    """Write entries under destination; overwrite=False keeps existing files."""
    for entry in entries:
        target = destination.joinpath(*entry.parts)
        if not overwrite and target.exists():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        with entry.open() as source, target.open("wb") as output:
            shutil.copyfileobj(source, output)


def windows_is_32bit() -> bool:
    """32-bit Windows, which cannot run an x64 game; 32-bit Python reports the OS's
    architecture in PROCESSOR_ARCHITEW6432."""
    machine = os.environ.get("PROCESSOR_ARCHITEW6432") or os.environ.get(
        "PROCESSOR_ARCHITECTURE", ""
    )
    return sys.platform == "win32" and machine.upper() == "X86"


def select_bundle_packages(bundle: zipfile.ZipFile) -> Tuple[str, List[str]]:
    """The bundle's game package entry and its resource package entries."""
    try:
        root = ElementTree.fromstring(
            bundle.read("AppxMetadata/AppxBundleManifest.xml")
        )
    except KeyError:
        raise InstallError(
            "Not a recognized .appxbundle: it has no AppxMetadata/AppxBundleManifest.xml."
        ) from None
    except ElementTree.ParseError as error:
        raise InstallError(f"Unreadable AppxBundleManifest.xml: {error}") from error
    names = set(bundle.namelist())

    def entry(package) -> str:
        name = package.get("FileName", "")
        for candidate in (name, urllib.parse.quote(name)):
            if candidate in names:
                return candidate
        raise InstallError(f"The bundle lists {name}, but does not contain it.")

    packages = [e for e in root.iter() if local_name(e) == "Package"]
    applications = {
        e.get("Architecture", "neutral").lower(): e
        for e in packages
        if e.get("Type", "application").lower() == "application"
    }
    resources = [entry(e) for e in packages if e.get("Type", "").lower() == "resource"]
    found = [a for a in BUNDLE_ARCHITECTURES if a in applications]
    if found:
        # Every application package of a bundle targets the same Windows.
        with bundle.open(entry(applications[found[0]])) as stream, zipfile.ZipFile(
            stream
        ) as inner:
            universal = package_manifest(zip_entries(inner)).universal
        order = (
            UNIVERSAL_BUNDLE_ARCHITECTURES
            if universal and not windows_is_32bit()
            else BUNDLE_ARCHITECTURES
        )
        best = next(a for a in order if a in applications)
        return entry(applications[best]), resources
    if any(a.startswith("arm") for a in applications):
        raise InstallError(
            "This bundle only has ARM packages; Appx Portable supports x86 and x64 games."
        )
    raise InstallError("Could not find an x86 or x64 game package in the bundle.")


def package_manifest(entries: List[Entry]) -> Manifest:
    """The manifest among entries, checked to name a file the package has."""
    found = next(
        (e for e in entries if [p.lower() for p in e.parts] == ["appxmanifest.xml"]),
        None,
    )
    if found is None:
        raise InstallError(
            "Missing AppxManifest.xml. Give the whole .appx or .appxbundle,"
            " or a complete extraction."
        )
    with found.open() as stream:
        manifest = read_manifest(stream.read())
    wanted = manifest.executable.lower().split("/")
    if not any([p.lower() for p in e.parts] == wanted for e in entries):
        raise InstallError(f"The package has no {manifest.executable}.")
    return manifest


def extract_game(source: Path, destination: Path) -> Manifest:
    """Stage a package's files into destination; returns its manifest.

    Every name is checked before anything is written. Re-running replaces the
    package's files and keeps saves, appx-portable.ini and logs."""
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if source.is_dir():
        if (
            source == destination
            or source in destination.parents
            or destination in source.parents
        ):
            raise InstallError(
                "The package folder and the game folder must not overlap."
            )
        entries = folder_entries(source)
        manifest = package_manifest(entries)
        destination.mkdir(parents=True, exist_ok=True)
        write_entries(entries, destination, overwrite=True)
        return manifest
    try:
        with zipfile.ZipFile(source) as archive:
            if (
                source.suffix.lower() == ".appxbundle"
                or "AppxMetadata/AppxBundleManifest.xml" in archive.namelist()
            ):
                return extract_bundle(archive, destination)
            entries = zip_entries(archive)
            manifest = package_manifest(entries)
            destination.mkdir(parents=True, exist_ok=True)
            write_entries(entries, destination, overwrite=True)
            return manifest
    except (zipfile.BadZipFile, zipfile.LargeZipFile) as error:
        raise InstallError(f"Cannot read {source} as a package: {error}") from error


def copy_entry(bundle: zipfile.ZipFile, name: str, path: Path) -> Path:
    """Copy one bundle entry out: seeking inside a compressed entry restarts it."""
    with bundle.open(name) as stream, path.open("wb") as output:
        shutil.copyfileobj(stream, output)
    return path


def extract_bundle(bundle: zipfile.ZipFile, destination: Path) -> Manifest:
    """Stage a bundle's game package, then add its resource packages' files."""
    application, resources = select_bundle_packages(bundle)
    # The zips close before their temporary folder is removed.
    with tempfile.TemporaryDirectory(
        prefix="installappx-"
    ) as temporary, ExitStack() as stack:
        try:
            archives = [
                stack.enter_context(
                    zipfile.ZipFile(
                        copy_entry(bundle, name, Path(temporary) / f"{i}.appx")
                    )
                )
                for i, name in enumerate([application] + resources)
            ]
            game_entries = zip_entries(archives[0])
            manifest = package_manifest(game_entries)
            extra = [zip_entries(archive) for archive in archives[1:]]
        except zipfile.BadZipFile as error:
            raise InstallError(
                f"A package inside the bundle is unreadable: {error}"
            ) from error
        destination.mkdir(parents=True, exist_ok=True)
        write_entries(game_entries, destination, overwrite=True)
        # Resource packages add scale and language files; the main
        # package's own files, resources.pri among them, always win.
        for entries in extra:
            write_entries(entries, destination, overwrite=False)
        return manifest


# Visual C++ runtime (VCLibs)

# The runtime DLLs each VCLibs framework package provides to Store apps.
VCLIBS = {
    "Microsoft.VCLibs.110.00": ("msvcr110.dll", "msvcp110.dll", "vccorlib110.dll"),
    "Microsoft.VCLibs.120.00": (
        "msvcr120_app.dll",
        "msvcp120_app.dll",
        "vccorlib120_app.dll",
    ),
    "Microsoft.VCLibs.140.00": (
        "vcruntime140_app.dll",
        "msvcp140_app.dll",
        "vccorlib140_app.dll",
    ),
}
OPTIONAL_VCLIBS140 = (
    "concrt140_app.dll",
    "vcruntime140_1_app.dll",
    "msvcp140_1_app.dll",
    "msvcp140_2_app.dll",
    "msvcp140_atomic_wait_app.dll",
    "msvcp140_codecvt_ids_app.dll",
    "vcamp140_app.dll",
    "vcomp140_app.dll",
)
VCLIBS_RELEASE = "https://github.com/yell0wsuit/vclibs-appx/releases/download/v1/"
# The Microsoft-signed package for each dependency and architecture in
# VCLIBS_RELEASE, with its SHA-256 so a replaced release asset is refused.
VCLIBS_PACKAGES = {
    ("Microsoft.VCLibs.110.00", "x86"): (
        "Microsoft.VCLibs.110.00_11.0.51106.1_x86__8wekyb3d8bbwe.Appx",
        "22d49fa295486e0955a305bea65cfa4ff5d154d296878a1c577ec5507b602c9c",
    ),
    ("Microsoft.VCLibs.110.00", "x64"): (
        "Microsoft.VCLibs.110.00_11.0.51106.1_x64__8wekyb3d8bbwe.Appx",
        "b3383e6543e9ad90968143347f2ec5cb7b1e24acdd1c797af4a1ba98df98ae85",
    ),
    ("Microsoft.VCLibs.120.00", "x86"): (
        "Microsoft.VCLibs.120.00_12.0.21005.1_x86__8wekyb3d8bbwe.Appx",
        "f1302e899f015818b1b807f7a1dc44c221c184e3f34a19fe81b5874d4ab974bd",
    ),
    ("Microsoft.VCLibs.120.00", "x64"): (
        "Microsoft.VCLibs.120.00_12.0.21005.1_x64__8wekyb3d8bbwe.Appx",
        "64dd765b88d1983c09ebb33dc631bdfefb415f45f29177ab423bbc12c506e64f",
    ),
    ("Microsoft.VCLibs.140.00", "x86"): (
        "Microsoft.VCLibs.140.00_14.0.30704.0_x86__8wekyb3d8bbwe.Appx",
        "397b610554d67abaddfdc287ced6a9bf1ada4d4c9ddf53e01c6a061bed2860ef",
    ),
    ("Microsoft.VCLibs.140.00", "x64"): (
        "Microsoft.VCLibs.140.00_14.0.30704.0_x64__8wekyb3d8bbwe.Appx",
        "009f7db134c6061fe8f260e075374a28abbbc44e6cf23de107f93ec8b8c59816",
    ),
}
VCLIBS_DOWNLOADS = {
    key: VCLIBS_RELEASE + name for key, (name, _) in VCLIBS_PACKAGES.items()
}


def missing_vclibs(manifest: Manifest, game: Path) -> List[str]:
    """The manifest's VCLibs dependencies whose DLLs are not in game yet."""
    return [
        dependency
        for dependency in manifest.dependencies
        if dependency in VCLIBS
        and not all((game / name).is_file() for name in VCLIBS[dependency])
    ]


def add_vclibs(dependency: str, source: Path, game: Path, architecture: str) -> None:
    """Copy one dependency's DLLs from a VCLibs .appx or a folder into game."""
    source = Path(source)
    names = VCLIBS[dependency] + (
        OPTIONAL_VCLIBS140 if dependency.startswith("Microsoft.VCLibs.140.") else ()
    )
    with tempfile.TemporaryDirectory(prefix="installappx-vclibs-") as temporary:
        folder = source
        if source.is_file():
            # An .appx is a zip with the DLLs at its root. Only those names are
            # extracted, so entry paths never reach the file system.
            folder = Path(temporary)
            try:
                with zipfile.ZipFile(source) as archive:
                    wanted = {name.lower(): name for name in names}
                    for entry in archive.namelist():
                        if entry.lower() in wanted:
                            (folder / wanted[entry.lower()]).write_bytes(
                                archive.read(entry)
                            )
            except zipfile.BadZipFile as error:
                raise InstallError(
                    f"Cannot read {source} as an .appx package."
                ) from error
        elif not source.is_dir():
            raise InstallError(f"Not found: {source}")
        missing = [name for name in VCLIBS[dependency] if not (folder / name).is_file()]
        if missing:
            raise InstallError(
                f"{source} does not have {', '.join(missing)} ({dependency})."
            )
        found = [name for name in names if (folder / name).is_file()]
        for name in found:
            actual = architecture_of(folder / name)
            if actual != architecture:
                raise InstallError(
                    f"{name} in {source} is {actual}, but the game is {architecture}."
                    f" Use the {architecture} {dependency}."
                )
        for name in found:
            shutil.copyfile(folder / name, game / name)


def download_vclibs(dependency: str, game: Path, architecture: str) -> None:
    """Add one dependency's DLLs from its pinned package in VCLIBS_RELEASE."""
    name, sha256 = VCLIBS_PACKAGES[(dependency, architecture)]
    data = fetch(VCLIBS_RELEASE + name, "pass it with --vclibs")
    if hashlib.sha256(data).hexdigest() != sha256:
        raise InstallError(
            f"{VCLIBS_RELEASE + name} does not have the expected SHA-256 {sha256}."
        )
    with tempfile.TemporaryDirectory(prefix="installappx-vclibs-") as temporary:
        package = Path(temporary) / name
        package.write_bytes(data)
        add_vclibs(dependency, package, game, architecture)


# Appx Portable runtime

# System DLLs games import from their folder; each loads appxportable.dll.
PROXIES = ("d3dcompiler_47.dll", "XAudio2_8.dll", "DWrite.dll")


def replace_file(source: Path, target: Path) -> None:
    """Replace target by rename, never in place, so it is never half-written."""
    fd, temporary = tempfile.mkstemp(
        prefix=target.name + ".", suffix=".tmp", dir=target.parent
    )
    os.close(fd)
    try:
        shutil.copyfile(source, temporary)
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def stage_runtime(
    build: Path, game: Path, architecture: str, managed: bool
) -> List[str]:
    """Copy the shim, the proxies and, for managed games, appx-host.exe into game."""
    if managed and architecture == "x64":
        raise InstallError(
            "Managed x64 games are not supported; managed hosting is x86 only."
        )
    build = Path(build)
    names = ["appxportable.dll", *PROXIES]
    if (build / "XAudio2_9.dll").is_file():
        names.append("XAudio2_9.dll")
    if managed:
        names.append("appx-host.exe")
    for name in names:
        if not (build / name).is_file():
            raise InstallError(f"Build output missing: {build / name}")
        actual = architecture_of(build / name)
        if actual != architecture:
            raise InstallError(
                f"{build / name} is {actual}, but the game is {architecture}."
            )
    for name in names:
        replace_file(build / name, game / name)
    # The WebView2 static loader's redistribution notice travels with the DLL.
    # Older releases/local builds do not have this dependency.
    notice = build / "WebView2-LICENSE.txt"
    if notice.is_file():
        replace_file(notice, game / notice.name)
    return names


# appx-portable.ini

SETTINGS_TEMPLATE: str = """\
; appx-portable settings. installappx.py writes this file once; edit it here.

[storage]
; Where the game keeps its saves (<package> is the manifest's identity name):
;   game       SaveData beside the game executable (the default)
;   documents  Documents\\appx-portable\\<package>
;   appdata    %LOCALAPPDATA%\\appx-portable\\<package>
;   or a folder path; relative paths start beside the game, and %VARIABLES% expand.
; If the folder cannot be written (for example under Program Files), saves go
; to documents, then appdata. Changing the location does not move saves.
SaveLocation=game

[options]
; Store overrides, off with 0 and on with 1. Restart the game after a change.
;   UnlockTrial  report a full license, for games with a trial mode
;   StubIap      complete in-app purchases locally, without the Store
UnlockTrial=0
StubIap=0
"""
STORE_CHOICES = {
    "none": (False, False),
    "trial": (True, False),
    "iap": (False, True),
    "both": (True, True),
}
STORE_KEYS = ("UnlockTrial", "StubIap")


def settings_encoding(data: bytes) -> str:
    """The encoding to read and write an appx-portable.ini back in."""
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return "utf-16"
    return "utf-8-sig" if data.startswith(b"\xef\xbb\xbf") else "utf-8"


def settings_lines(game: Path) -> List[str]:
    """appx-portable.ini's lines; ANSI bytes survive as surrogates, written back."""
    path = game / "appx-portable.ini"
    if not path.is_file():
        return []
    data = path.read_bytes()
    return data.decode(settings_encoding(data), errors="surrogateescape").splitlines()


def option_values(lines: List[str]) -> Dict[str, str]:
    """The [options] keys (lower case) and values."""
    values, section = {}, ""
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("["):
            section = stripped.strip("[]").strip().lower()
        elif section == "options" and "=" in stripped and not stripped.startswith(";"):
            key, value = stripped.split("=", 1)
            values[key.strip().lower()] = value.strip()
    return values


def read_store(game: Path) -> str:
    """The Store choice appx-portable.ini holds: none, trial, iap or both."""
    values = option_values(settings_lines(Path(game)))
    on = tuple(values.get(key.lower(), "0") not in ("", "0") for key in STORE_KEYS)
    return next(name for name, choice in STORE_CHOICES.items() if choice == on)


def write_settings(game: Path, store: Optional[str]) -> None:
    """Create appx-portable.ini once, and set the Store choice unless store is
    None. Only the UnlockTrial and StubIap lines change; the rest is kept."""
    game = Path(game)
    path = game / "appx-portable.ini"
    lines = settings_lines(game) or SETTINGS_TEMPLATE.splitlines()
    if store is not None:
        wanted = {
            key: "1" if on else "0" for key, on in zip(STORE_KEYS, STORE_CHOICES[store])
        }
        start = next(
            (i for i, line in enumerate(lines) if line.strip().lower() == "[options]"),
            None,
        )
        if start is None:
            lines += ["", "[options]"] + [
                f"{key}={value}" for key, value in wanted.items()
            ]
        else:
            end = next(
                (
                    i
                    for i in range(start + 1, len(lines))
                    if lines[i].strip().startswith("[")
                ),
                len(lines),
            )
            for i in range(start + 1, end):
                key = lines[i].split("=", 1)[0].strip()
                match = next((k for k in wanted if k.lower() == key.lower()), None)
                if "=" in lines[i] and not lines[i].strip().startswith(";") and match:
                    lines[i] = f"{match}={wanted.pop(match)}"
            lines[start + 1 : start + 1] = [
                f"{key}={value}" for key, value in wanted.items()
            ]
    elif path.is_file():
        return
    encoding = settings_encoding(path.read_bytes()) if path.is_file() else "utf-8"
    errors = "strict" if encoding == "utf-16" else "surrogateescape"
    # CRLF, so Notepad on Windows 8 shows the lines.
    path.write_bytes(("\r\n".join(lines) + "\r\n").encode(encoding, errors))


# Releases and patches

REPOSITORY = "yell0wsuit/appx-portable"


def fetch(url: str, instead: str = "pass --build with a local build folder") -> bytes:
    """Download url; any failure is an InstallError naming it and what to do instead."""
    request = urllib.request.Request(url, headers={"User-Agent": "installappx"})
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return response.read()
    except (urllib.error.URLError, OSError, http.client.HTTPException) as error:
        reason = getattr(error, "reason", error)
        raise InstallError(
            f"Could not download {url}: {reason}. Check the connection, or {instead}."
        ) from error


def resolve_release(tag: Optional[str]) -> str:
    """tag, or the latest release's when it is None."""
    if tag:
        return tag
    try:
        return json.loads(
            fetch(f"https://api.github.com/repos/{REPOSITORY}/releases/latest")
        )["tag_name"]
    except (ValueError, KeyError) as error:
        raise InstallError("Could not read the latest release from GitHub.") from error


def download_runtime(tag: str, architecture: str, folder: Path) -> Path:
    """Download and unpack appx-portable-<tag>-<architecture>.zip into folder."""
    try:
        release = json.loads(
            fetch(f"https://api.github.com/repos/{REPOSITORY}/releases/tags/{tag}")
        )
        assets = {
            asset["name"]: asset["browser_download_url"]
            for asset in release.get("assets", [])
        }
    except (ValueError, KeyError, TypeError) as error:
        raise InstallError(f"Could not read release {tag} from GitHub.") from error
    name = f"appx-portable-{tag}-{architecture}.zip"
    if name not in assets:
        raise InstallError(f"Release {tag} has no {name}.")
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    try:
        with zipfile.ZipFile(io.BytesIO(fetch(assets[name]))) as archive:
            for entry in archive.namelist():
                # The release zip holds plain DLLs; anything in a folder is ignored.
                if (
                    "/" not in entry
                    and "\\" not in entry
                    and entry not in ("", ".", "..")
                ):
                    (folder / entry).write_bytes(archive.read(entry))
    except zipfile.BadZipFile as error:
        raise InstallError(f"{name} is not a readable zip.") from error
    return folder


@dataclass
class KnownPatch:
    """A patch in patch/<folder> and what it is run against."""

    folder: str
    files: Tuple[str, ...]
    target: str  # what the patch is given: "" for the game folder, else a path in it


# Patches in patch/<folder>, for the exact package identity and version each supports.
KNOWN_PATCHES = {
    ("PopCap.BejweledLIVE", "4.0.0.10"): KnownPatch("bejeweled", ("patch.py",), ""),
    ("Microsoft.MicrosoftSudoku", "1.7.10190.0"): KnownPatch(
        "mssudoku", ("patch.py",), ""
    ),
    ("Disney.WheresMyMickey", "1.1.0.30"): KnownPatch(
        "wheresmymickey", ("patch.py",), "WheresMyMickey.Windows8.exe"
    ),
    # Keep the loose caption as the default compatibility route. Embedded PRI
    # has passed Windows 11 and user Windows 10 tests but uses undocumented APIs.
    ("Ubisoft.RaymanFiestaRunWindows10Edition", "1.0.23.0"): KnownPatch(
        "raymanwin10", ("patch.py", "CustomTitleBar.xaml"), ""
    ),
}


def apply_known_patch(
    manifest: Manifest, game: Path, tag: Optional[str], local: Optional[Path]
) -> Optional[str]:
    """Run the patch for a known game; None when there is none.

    local is a repository's patch/ folder; otherwise the patch is downloaded at
    the release tag, so it always matches the runtime."""
    patch = KNOWN_PATCHES.get((manifest.name, manifest.version))
    if patch is None:
        return None
    game = Path(game)
    with tempfile.TemporaryDirectory(prefix="installappx-patch-") as temporary:
        folder = Path(temporary)
        for name in patch.files:
            if local is not None:
                shutil.copyfile(Path(local) / patch.folder / name, folder / name)
            else:
                url = f"https://raw.githubusercontent.com/{REPOSITORY}/{tag}"
                (folder / name).write_bytes(fetch(f"{url}/patch/{patch.folder}/{name}"))
        target = game / patch.target if patch.target else game
        result = subprocess.run(
            [sys.executable, "-X", "utf8", "-I", str(folder / "patch.py"), str(target)],
            capture_output=True,
            encoding="utf-8",
            errors="replace",
            cwd=temporary,
            check=False,
        )
    if result.returncode != 0:
        raise InstallError(
            f"The {patch.folder} patch failed: {(result.stderr or result.stdout).strip()}"
        )
    return result.stdout.strip()


# Windows 8/8.1: AppContainer flag

APPCONTAINER = 0x1000  # IMAGE_DLLCHARACTERISTICS_APPCONTAINER


def needs_desktop_copy() -> bool:
    """Windows 8 and 8.1 refuse to start an AppContainer image outside a package."""
    # getattr: the function exists only on Windows, where type checkers skip it.
    version = getattr(sys, "getwindowsversion", lambda: (0, 0))
    return sys.platform == "win32" and tuple(version()[:2]) in ((6, 2), (6, 3))


def appcontainer_offset(data: bytes, dll: bool) -> Optional[int]:
    """The DllCharacteristics offset of an x86 PE32 image, or None for x64."""
    if len(data) < 64 or data[:2] != b"MZ":
        raise InstallError("Expected a DOS/PE image.")
    # Bounds checks before every read: the offsets come from the file.
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe < 64 or pe + 24 > len(data) or data[pe : pe + 4] != b"PE\0\0":
        raise InstallError("Invalid or truncated PE header.")
    machine, size, characteristics = struct.unpack_from("<H", data, pe + 4)[
        0
    ], *struct.unpack_from("<HH", data, pe + 20)
    if machine == 0x8664:
        return None
    optional = pe + 24
    if size < 96 or optional + size > len(data):
        raise InstallError("Invalid or truncated PE optional header.")
    if (
        machine != 0x14C
        or struct.unpack_from("<H", data, optional)[0] != 0x10B
        or not characteristics & 0x2
        or bool(characteristics & 0x2000) != dll
    ):
        raise InstallError(
            "Expected an x86 PE32 image of the appropriate EXE/DLL type."
        )
    return optional + 70


def cleared(data: bytes, dll: bool) -> Optional[bytes]:
    """data with only the AppContainer flag cleared, or None when it is not set."""
    offset = appcontainer_offset(data, dll)
    if offset is None or not struct.unpack_from("<H", data, offset)[0] & APPCONTAINER:
        return None
    patched = bytearray(data)
    struct.pack_into(
        "<H",
        patched,
        offset,
        struct.unpack_from("<H", data, offset)[0] & ~APPCONTAINER & 0xFFFF,
    )
    return bytes(patched)


def write_replacing(target: Path, data: bytes) -> None:
    """Write data to target through a temporary file and a rename."""
    fd, temporary = tempfile.mkstemp(
        prefix=target.name + ".", suffix=".tmp", dir=target.parent
    )
    try:
        with os.fdopen(fd, "wb") as output:
            output.write(data)
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def make_desktop_copy(game: Path, executable: str) -> str:
    """Write <name>.desktop.exe with the AppContainer flag cleared, and clear it
    in the staged VCLibs DLLs; returns the executable to launch."""
    game = Path(game)
    source = game / executable
    patched = cleared(source.read_bytes(), dll=False)
    if patched is None:
        return executable
    relative = Path(executable)
    desktop = relative.with_name(relative.stem + ".desktop" + relative.suffix)
    # The staged DLLs are copies, so they are patched in place.
    names = [name for names in VCLIBS.values() for name in names] + list(
        OPTIONAL_VCLIBS140
    )
    for name in names:
        if (game / name).is_file():
            image = cleared((game / name).read_bytes(), dll=True)
            if image is not None:
                write_replacing(game / name, image)
    write_replacing(game / desktop, patched)
    return desktop.as_posix()


# Folder depth

# Store games keep paths in MAX_PATH buffers, so they cannot open a file whose
# path is longer; some (Fruit Ninja) crash in any game folder near 180 characters.
MAX_PATH = 260
LONG_FOLDER = 150


def path_warning(game: Path, files: Iterable[str]) -> Optional[str]:
    """Why the game may fail in its folder, given its files' paths; None if not."""
    longest = max(files, key=len, default="")
    if len(longest) >= MAX_PATH:
        return (
            f"{longest} is {len(longest)} characters long, and games cannot open"
            f" a file past {MAX_PATH - 1}. Install into a shorter folder."
        )
    if len(str(game)) > LONG_FOLDER:
        return (
            f"{game} is {len(str(game))} characters long, and some games crash in"
            " a folder this deep. If the game does not start, install into a shorter folder."
        )
    return None


# Launchers

LAUNCHER = "00_click2startme"
BAT_LAUNCHER = """\
@echo off
rem Starts the game. Written by installappx.py; options are in appx-portable.ini.
cd /d "%~dp0"
start "" "%~dp0{exe}" %*
"""
SH_LAUNCHER = """\
#!/usr/bin/env bash
# Starts the game under Wine. Written by installappx.py; options are in
# appx-portable.ini. Set WINE to choose the Wine executable.
set -euo pipefail
cd "$(dirname "$0")"
# --log always: the relaunch check below reads the log.
flags=(--log "$@")
export WINEDEBUG=${{WINEDEBUG:--all}}
export MVK_CONFIG_LOG_LEVEL=${{MVK_CONFIG_LOG_LEVEL:-0}}
# The proxies load the shim; the VC runtimes are the package's own (VCLibs 110, 120, 140).
export WINEDLLOVERRIDES="d3dcompiler_47,xaudio2_8,xaudio2_9,dwrite=n,b;msvcr110,msvcp110,vccorlib110,msvcr120_app,msvcp120_app,vccorlib120_app,vcruntime140_app,msvcp140_app,vccorlib140_app=n${{WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}}"
wine=${{WINE:-$(command -v wine || echo "$HOME/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine")}}
[[ -x "$wine" ]] || {{ echo 'Install Wine, or set WINE to your Wine executable.' >&2; exit 1; }}
# A crash otherwise opens Wine's crash dialog, which keeps the process alive
# until closed and stalls the relaunch below; print the backtrace and exit.
"$wine" reg add 'HKCU\\Software\\Wine\\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f > /dev/null 2>&1 || true
# Wine 11 WoW64 sometimes kills the process inside wined3d's first OpenGL
# setup ("Exception frame is not in stack limits"). That startup death leaves
# "CreateDXGIFactory1 begin" without a result in the log; retry only that case.
status=0
for attempt in 1 2 3; do
  "$wine" {exe} "${{flags[@]}}" && exit 0 || status=$?
  # Wine launches always log, and each logging run starts a new appx-portable.log.
  run=$(cat appx-portable.log 2>/dev/null || true)
  if grep -q 'CreateDXGIFactory1 begin' <<< "$run" && ! grep -q 'CreateDXGIFactory1 hr=' <<< "$run"; then
    echo "Wine graphics startup crashed (attempt $attempt/3); relaunching." >&2
    continue
  fi
  exit "$status"
done
exit "$status"
"""


def launch_executable(executable: str, managed: bool) -> str:
    """What starts the game: appx-host.exe, beside it, for managed games."""
    return "appx-host.exe" if managed else executable


def write_launcher(game: Path, executable: str, windows: bool) -> Path:
    """Write 00_click2startme.bat (CRLF) or .sh (executable) into game."""
    if windows:
        path = Path(game) / (LAUNCHER + ".bat")
        text = BAT_LAUNCHER.format(
            exe=str(PureWindowsPath(executable)).replace("%", "%%")
        )
        path.write_bytes(text.replace("\n", "\r\n").encode())
    else:
        path = Path(game) / (LAUNCHER + ".sh")
        path.write_bytes(SH_LAUNCHER.format(exe=shlex.quote(executable)).encode())
        path.chmod(0o755)
    return path


# Command line

STORE_ANSWERS = {"0": "none", "1": "trial", "2": "iap", "3": "both"}


def peek_manifest(source: Path) -> Manifest:
    """The package's manifest, read without extracting anything."""
    source = Path(source)
    if source.is_dir():
        path = source / "AppxManifest.xml"
        if not path.is_file():
            raise InstallError(
                f"Missing {path}. Give the whole .appx or .appxbundle,"
                " or a complete extraction."
            )
        return read_manifest(path.read_bytes())
    try:
        with zipfile.ZipFile(source) as archive:
            if (
                source.suffix.lower() == ".appxbundle"
                or "AppxMetadata/AppxBundleManifest.xml" in archive.namelist()
            ):
                application, _ = select_bundle_packages(archive)
                with archive.open(application) as stream, zipfile.ZipFile(
                    stream
                ) as inner:
                    return package_manifest(zip_entries(inner))
            return package_manifest(zip_entries(archive))
    except (zipfile.BadZipFile, zipfile.LargeZipFile) as error:
        raise InstallError(f"Cannot read {source} as a package: {error}") from error
    except FileNotFoundError:
        raise InstallError(f"Not found: {source}") from None


def unquote_path(text: str) -> str:
    """A typed path without the quotes "Copy as path" and dragging add."""
    return text.strip().strip('"').strip()


def parse_arguments(argv: List[str]):
    """The command line; every prompt has an option."""
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "package", nargs="?", help=".appxbundle, .appx, or extracted package folder"
    )
    parser.add_argument(
        "--dest", help="folder to install the game in (default: beside the package)"
    )
    parser.add_argument(
        "--vclibs",
        action="append",
        default=[],
        help="VCLibs .appx or folder; repeat for several",
    )
    parser.add_argument(
        "--build",
        help="use the DLLs in this local build folder instead of downloading a release",
    )
    parser.add_argument(
        "--release", help="release tag to download (default: the latest)"
    )
    parser.add_argument(
        "--store", choices=sorted(STORE_CHOICES), help="Store overrides to turn on"
    )
    launcher = parser.add_mutually_exclusive_group()
    launcher.add_argument(
        "--launcher",
        action="store_true",
        default=None,
        help=f"write {LAUNCHER} (the default)",
    )
    launcher.add_argument(
        "--no-launcher",
        dest="launcher",
        action="store_false",
        help=f"do not write {LAUNCHER}",
    )
    parser.add_argument(
        "--no-patch",
        action="store_true",
        help="do not apply the patch for a known game",
    )
    parser.add_argument(
        "--desktop-copy",
        action="store_true",
        help="clear the AppContainer flag as Windows 8/8.1 need",
    )
    parser.add_argument(
        "--yes",
        action="store_true",
        help="never ask; fail when something required is missing",
    )
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None, ask: Callable[[str], str] = input) -> int:
    """Install, printing any error as one line; returns the exit code."""
    argv = sys.argv[1:] if argv is None else argv
    # Double-clicked, or a package dropped on the script: keep the window open.
    interactive = "--yes" not in argv
    try:
        return install(parse_arguments(argv), ask)
    except InstallError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    except (KeyboardInterrupt, EOFError):
        print(file=sys.stderr)
        return 1
    # A double-clicked window must show any failure before it closes.
    except Exception as error:  # pylint: disable=broad-exception-caught
        print(f"Error: {type(error).__name__}: {error}", file=sys.stderr)
        return 1
    finally:
        if interactive:
            try:
                ask("Press Enter to close.")
            except (KeyboardInterrupt, EOFError):
                pass


def absolute(path: str) -> Path:
    """An absolute path: Python 3.8/3.9 on Windows resolve a missing one to itself."""
    return Path(os.path.abspath(Path(unquote_path(path)).expanduser())).resolve()


def choose_folders(args, ask: Callable[[str], str]) -> Tuple[Path, Path, Manifest]:
    """The package, the folder to install it in, and the package's manifest."""
    package = args.package
    if not package and not args.yes:
        package = ask("Game package (.appxbundle, .appx or extracted folder): ")
    if not unquote_path(package or ""):
        raise InstallError("No game package given.")
    source = absolute(package)
    manifest = peek_manifest(source)
    default = source.parent / manifest.name
    if default == source:
        default = source.parent / (manifest.name + "-portable")
    destination = args.dest
    if destination is None and not args.yes:
        destination = ask(f"Folder to install the game in [{default}]: ")
    chosen = unquote_path(destination or "")
    return source, absolute(chosen) if chosen else default, manifest


def try_vclibs(
    dependency: str, sources: List[str], game: Path, architecture: str
) -> List[str]:
    """Add dependency from the first source that has it; why the others did not."""
    refused = []
    for source in sources:
        try:
            add_vclibs(dependency, absolute(source), game, architecture)
            return []
        except InstallError as error:
            refused.append(str(error))
    return refused


def add_missing_vclibs(
    args, ask: Callable[[str], str], manifest: Manifest, game: Path, architecture: str
) -> None:
    """Add each VCLibs dependency the game lacks: --vclibs, download, then ask."""
    for dependency in missing_vclibs(manifest, game):
        refused = try_vclibs(dependency, args.vclibs, game, architecture)
        if dependency in missing_vclibs(manifest, game):
            print(f"Downloading {dependency} ({architecture})")
            try:
                download_vclibs(dependency, game, architecture)
            except InstallError as error:
                refused.append(str(error))
        link = VCLIBS_DOWNLOADS[(dependency, architecture)]
        missing = f"Missing {dependency} ({architecture}). Download: {link}."
        if args.yes and dependency in missing_vclibs(manifest, game):
            raise InstallError(" ".join(refused + [missing, "Pass it with --vclibs."]))
        while dependency in missing_vclibs(manifest, game):
            for message in refused:
                print(message)
            print(missing)
            answer = ask(f"{dependency} .appx or folder: ")
            if not unquote_path(answer):
                raise InstallError(f"Missing {dependency} ({architecture}).")
            refused = try_vclibs(dependency, [answer], game, architecture)


def stage_dlls(args, game: Path, architecture: str, managed: bool) -> Optional[str]:
    """Copy the runtime from --build or the release; returns the release tag used."""
    if args.build:
        stage_runtime(absolute(args.build), game, architecture, managed)
        return args.release
    tag = resolve_release(args.release)
    print(f"Downloading Appx Portable {tag} ({architecture})")
    with tempfile.TemporaryDirectory(prefix="installappx-runtime-") as temporary:
        stage_runtime(
            download_runtime(tag, architecture, Path(temporary)),
            game,
            architecture,
            managed,
        )
    return tag


def patch_known_game(manifest: Manifest, game: Path, tag: Optional[str]) -> None:
    """Apply a known game's patch, from a checkout's patch/ or at the release tag."""
    local = Path(__file__).resolve().parent / "patch"
    local = local if local.is_dir() else None
    if (
        local is None
        and tag is None
        and (manifest.name, manifest.version) in KNOWN_PATCHES
    ):
        tag = resolve_release(None)
    output = apply_known_patch(manifest, game, tag, local)
    if output:
        print(output)


def choose_store(args, ask: Callable[[str], str], game: Path) -> Optional[str]:
    """The Store choice from --store or the prompt; None keeps the current one."""
    if args.store is not None or args.yes:
        return args.store
    current = read_store(game)
    while True:
        answer = ask(
            "Store options: 1 unlock trial, 2 stub in-app purchases, 3 both, 0 neither"
            f" [Enter keeps {current}]: "
        ).strip()
        if not answer or answer in STORE_ANSWERS:
            return STORE_ANSWERS.get(answer)


def install(args, ask: Callable[[str], str]) -> int:
    """Install the game the arguments and answers describe."""
    source, destination, manifest = choose_folders(args, ask)
    print(f"Installing {manifest.name} {manifest.version} into {destination}")
    manifest = extract_game(source, destination)
    executable = destination / manifest.executable
    architecture = architecture_of(executable)
    managed = pe_info(executable).managed
    add_missing_vclibs(args, ask, manifest, destination, architecture)
    tag = stage_dlls(args, destination, architecture, managed)
    if not args.no_patch:
        patch_known_game(manifest, destination, tag)
    launch = launch_executable(manifest.executable, managed)
    if not managed and (args.desktop_copy or needs_desktop_copy()):
        launch = make_desktop_copy(destination, manifest.executable)
    write_settings(destination, choose_store(args, ask, destination))
    launcher = args.launcher
    if launcher is None:
        answer = "" if args.yes else ask(f"Create the {LAUNCHER} launcher? [Y/n]: ")
        launcher = answer.strip().lower() not in ("n", "no")
    if launcher:
        launch = str(
            write_launcher(destination, launch, windows=sys.platform == "win32")
        )
    warning = path_warning(
        destination, [str(p) for p in destination.rglob("*") if p.is_file()]
    )
    if warning:
        print(f"Warning: {warning}")
    print(f"Start the game with {destination / launch}")
    print(
        f"Store options: {read_store(destination)}. Settings: {destination / 'appx-portable.ini'}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
