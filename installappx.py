#!/usr/bin/env python3
"""Install a Windows Store game as a portable Appx Portable game folder.

Give it a .appxbundle, a .appx, or an extracted package folder. It extracts the
game, adds the Visual C++ runtime the game needs, downloads the matching Appx
Portable release, applies the patch for a known game, and can write a
00_click2startme launcher. Run it without arguments to be asked for each step.
"""

import os
import re
import shutil
import struct
import sys
import tempfile
import urllib.parse
import xml.etree.ElementTree as ElementTree
import zipfile
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
# Bundle application packages in order of preference: x86 is the tested path.
BUNDLE_ARCHITECTURES = ("x86", "neutral", "x64")
INVALID_NAME = re.compile(r'[<>:"|?*\x00-\x1f]')


class InstallError(Exception):
    pass


# PE images


@dataclass
class PEInfo:
    machine: int
    managed: bool
    dll: bool


def pe_info(path: Path) -> PEInfo:
    data = Path(path).read_bytes()[:4096]
    if len(data) < 64 or data[:2] != b"MZ":
        raise InstallError(f"Not a Windows executable or DLL: {path}")
    offset = struct.unpack_from("<I", data, 0x3C)[0]
    if offset + 24 + 96 > len(data) or data[offset : offset + 4] != b"PE\0\0":
        raise InstallError(f"Invalid PE header: {path}")
    machine, characteristics = struct.unpack_from("<H", data, offset + 4)[0], struct.unpack_from("<H", data, offset + 22)[0]
    optional = offset + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    # The CLR runtime header is data directory 14.
    clr = optional + (112 if magic == 0x20B else 96) + 14 * 8
    managed = clr + 4 <= len(data) and struct.unpack_from("<I", data, clr)[0] != 0
    return PEInfo(machine, managed, bool(characteristics & 0x2000))


def architecture_of(path: Path) -> str:
    machine = pe_info(path).machine
    if machine not in ARCHITECTURES:
        raise InstallError(f"Expected an x86 or x64 image: {path}")
    return ARCHITECTURES[machine]


# Package manifest


@dataclass
class Manifest:
    name: str
    version: str
    architecture: str
    executable: str
    dependencies: List[str]


def local_name(element) -> str:
    return element.tag.rsplit("}", 1)[-1]


def read_manifest(data: bytes) -> Manifest:
    try:
        root = ElementTree.fromstring(data)
    except ElementTree.ParseError as error:
        raise InstallError(f"Unreadable AppxManifest.xml: {error}")
    # By local name: Windows 8, 8.1 and 10 manifests use different namespaces.
    elements = list(root.iter())
    identity = next((e for e in elements if local_name(e) == "Identity"), None)
    application = next((e for e in elements if local_name(e) == "Application"), None)
    if identity is None or application is None:
        raise InstallError("AppxManifest.xml declares no Identity or Application.")
    executable = PureWindowsPath(application.get("Executable", ""))
    if not executable.parts or executable.anchor or ".." in executable.parts:
        raise InstallError(f"AppxManifest.xml has an unusable Executable: {application.get('Executable')!r}")
    return Manifest(
        name=identity.get("Name", ""),
        version=identity.get("Version", ""),
        architecture=identity.get("ProcessorArchitecture", "neutral").lower(),
        executable=executable.as_posix(),
        dependencies=[e.get("Name", "") for e in elements if local_name(e) == "PackageDependency"],
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
    if first in ("savedata", "appx-portable.ini") or (first.startswith("appx-portable") and first.endswith(".log")):
        return None
    return parts


@dataclass
class Entry:
    parts: List[str]
    open: Callable


def zip_entries(archive: zipfile.ZipFile) -> List[Entry]:
    entries = []
    for info in archive.infolist():
        parts = target_parts(info.filename)
        if parts is not None:
            entries.append(Entry(parts, lambda info=info: archive.open(info)))
    return entries


def folder_entries(folder: Path) -> List[Entry]:
    entries = []
    for parent, directories, files in os.walk(folder):
        for name in directories + files:
            path = Path(parent) / name
            if path.is_symlink():
                raise InstallError(f"Links are not allowed in the package folder: {path}")
        for name in files:
            path = Path(parent) / name
            parts = target_parts(path.relative_to(folder).as_posix())
            if parts is not None:
                entries.append(Entry(parts, lambda path=path: path.open("rb")))
    return entries


def write_entries(entries: Iterable[Entry], destination: Path, overwrite: bool) -> None:
    for entry in entries:
        target = destination.joinpath(*entry.parts)
        if not overwrite and target.exists():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        with entry.open() as source, target.open("wb") as output:
            shutil.copyfileobj(source, output)


def select_bundle_packages(bundle: zipfile.ZipFile) -> Tuple[str, List[str]]:
    try:
        root = ElementTree.fromstring(bundle.read("AppxMetadata/AppxBundleManifest.xml"))
    except KeyError:
        raise InstallError("Not a recognized .appxbundle: it has no AppxMetadata/AppxBundleManifest.xml.")
    except ElementTree.ParseError as error:
        raise InstallError(f"Unreadable AppxBundleManifest.xml: {error}")
    names = set(bundle.namelist())

    def entry(package) -> str:
        name = package.get("FileName", "")
        for candidate in (name, urllib.parse.quote(name)):
            if candidate in names:
                return candidate
        raise InstallError(f"The bundle lists {name}, but does not contain it.")

    packages = [e for e in root.iter() if local_name(e) == "Package"]
    applications = {
        e.get("Architecture", "neutral").lower(): e for e in packages if e.get("Type", "application").lower() == "application"
    }
    resources = [entry(e) for e in packages if e.get("Type", "").lower() == "resource"]
    for architecture in BUNDLE_ARCHITECTURES:
        if architecture in applications:
            return entry(applications[architecture]), resources
    if any(a.startswith("arm") for a in applications):
        raise InstallError("This bundle only has ARM packages; Appx Portable supports x86 and x64 games.")
    raise InstallError("Could not find an x86 or x64 game package in the bundle.")


def package_manifest(entries: List[Entry]) -> Manifest:
    found = next((e for e in entries if [p.lower() for p in e.parts] == ["appxmanifest.xml"]), None)
    if found is None:
        raise InstallError("Missing AppxManifest.xml. Give the whole .appx or .appxbundle, or a complete extraction.")
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
        if source == destination or source in destination.parents or destination in source.parents:
            raise InstallError("The package folder and the game folder must not overlap.")
        entries = folder_entries(source)
        manifest = package_manifest(entries)
        destination.mkdir(parents=True, exist_ok=True)
        write_entries(entries, destination, overwrite=True)
        return manifest
    try:
        with zipfile.ZipFile(source) as archive:
            if source.suffix.lower() == ".appxbundle" or "AppxMetadata/AppxBundleManifest.xml" in archive.namelist():
                return extract_bundle(archive, destination)
            entries = zip_entries(archive)
            manifest = package_manifest(entries)
            destination.mkdir(parents=True, exist_ok=True)
            write_entries(entries, destination, overwrite=True)
            return manifest
    except (zipfile.BadZipFile, zipfile.LargeZipFile) as error:
        raise InstallError(f"Cannot read {source} as a package: {error}")


def extract_bundle(bundle: zipfile.ZipFile, destination: Path) -> Manifest:
    application, resources = select_bundle_packages(bundle)
    with tempfile.TemporaryDirectory(prefix="installappx-") as temporary:
        # Copied out first: seeking inside a compressed entry restarts it.
        archives = []
        try:
            for index, name in enumerate([application] + resources):
                path = Path(temporary) / f"{index}.appx"
                with bundle.open(name) as stream, path.open("wb") as output:
                    shutil.copyfileobj(stream, output)
                archives.append(zipfile.ZipFile(path))
            main = zip_entries(archives[0])
            manifest = package_manifest(main)
            extra = [zip_entries(archive) for archive in archives[1:]]
            destination.mkdir(parents=True, exist_ok=True)
            write_entries(main, destination, overwrite=True)
            # Resource packages add scale and language files; the main
            # package's own files, resources.pri among them, always win.
            for entries in extra:
                write_entries(entries, destination, overwrite=False)
            return manifest
        except zipfile.BadZipFile as error:
            raise InstallError(f"A package inside the bundle is unreadable: {error}")
        finally:
            for archive in archives:
                archive.close()


# Visual C++ runtime (VCLibs)

# The runtime DLLs each VCLibs framework package provides to Store apps.
VCLIBS = {
    "Microsoft.VCLibs.110.00": ("msvcr110.dll", "msvcp110.dll", "vccorlib110.dll"),
    "Microsoft.VCLibs.120.00": ("msvcr120_app.dll", "msvcp120_app.dll", "vccorlib120_app.dll"),
    "Microsoft.VCLibs.140.00": ("vcruntime140_app.dll", "msvcp140_app.dll", "vccorlib140_app.dll"),
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
VCLIBS_120_REDIST = "https://download.microsoft.com/download/5/f/0/5f0f8404-9329-44a9-8176-ed6f7f746f25/vclibs_redist_packages.zip"
VCLIBS_140 = "https://download.videolan.org/pub/videolan/vlc-winrt/3.0.3/VLC.Universal_3.0.3.0_ARM_Test/Dependencies"
VCLIBS_DOWNLOADS = {
    ("Microsoft.VCLibs.110.00", "x86"): "https://archive.org/download/vclibs-appxs/Microsoft.VCLibs.APPXs.zip/Microsoft.VCLibs.110.00_11.0.51106.1_x86__8wekyb3d8bbwe.Appx",
    ("Microsoft.VCLibs.120.00", "x86"): VCLIBS_120_REDIST + " (extract the x86 .appx from it)",
    ("Microsoft.VCLibs.120.00", "x64"): VCLIBS_120_REDIST + " (extract the x64 .appx from it)",
    ("Microsoft.VCLibs.140.00", "x86"): VCLIBS_140 + "/x86/Microsoft.VCLibs.x86.14.00.appx",
    ("Microsoft.VCLibs.140.00", "x64"): VCLIBS_140 + "/x64/Microsoft.VCLibs.x64.14.00.appx",
}


def missing_vclibs(manifest: Manifest, game: Path) -> List[str]:
    return [
        dependency
        for dependency in manifest.dependencies
        if dependency in VCLIBS and not all((game / name).is_file() for name in VCLIBS[dependency])
    ]


def add_vclibs(dependency: str, source: Path, game: Path, architecture: str) -> None:
    """Copy one dependency's DLLs from a VCLibs .appx or a folder into game."""
    source = Path(source)
    names = VCLIBS[dependency] + (OPTIONAL_VCLIBS140 if dependency.startswith("Microsoft.VCLibs.140.") else ())
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
                            (folder / wanted[entry.lower()]).write_bytes(archive.read(entry))
            except zipfile.BadZipFile:
                raise InstallError(f"Cannot read {source} as an .appx package.")
        elif not source.is_dir():
            raise InstallError(f"Not found: {source}")
        missing = [name for name in VCLIBS[dependency] if not (folder / name).is_file()]
        if missing:
            raise InstallError(f"{source} does not have {', '.join(missing)} ({dependency}).")
        found = [name for name in names if (folder / name).is_file()]
        for name in found:
            actual = architecture_of(folder / name)
            if actual != architecture:
                raise InstallError(
                    f"{name} in {source} is {actual}, but the game is {architecture}. Use the {architecture} {dependency}."
                )
        for name in found:
            shutil.copyfile(folder / name, game / name)


# Appx Portable runtime

# System DLLs games import from their folder; each loads appxportable.dll.
PROXIES = ("d3dcompiler_47.dll", "XAudio2_8.dll", "DWrite.dll")


def replace_file(source: Path, target: Path) -> None:
    # By rename, never in place: a running game maps the old file.
    fd, temporary = tempfile.mkstemp(prefix=target.name + ".", suffix=".tmp", dir=target.parent)
    os.close(fd)
    try:
        shutil.copyfile(source, temporary)
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def stage_runtime(build: Path, game: Path, architecture: str, managed: bool) -> List[str]:
    if managed and architecture == "x64":
        raise InstallError("Managed x64 games are not supported; managed hosting is x86 only.")
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
            raise InstallError(f"{build / name} is {actual}, but the game is {architecture}.")
    for name in names:
        replace_file(build / name, game / name)
    return names


# appx-portable.ini

SETTINGS_TEMPLATE = """\
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
STORE_CHOICES = {"none": (False, False), "trial": (True, False), "iap": (False, True), "both": (True, True)}
STORE_KEYS = ("UnlockTrial", "StubIap")


def settings_lines(game: Path) -> List[str]:
    path = game / "appx-portable.ini"
    return path.read_text(encoding="utf-8-sig").splitlines() if path.is_file() else []


def option_values(lines: List[str]) -> Dict[str, str]:
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
        wanted = {key: "1" if on else "0" for key, on in zip(STORE_KEYS, STORE_CHOICES[store])}
        start = next((i for i, line in enumerate(lines) if line.strip().lower() == "[options]"), None)
        if start is None:
            lines += ["", "[options]"] + [f"{key}={value}" for key, value in wanted.items()]
        else:
            end = next((i for i in range(start + 1, len(lines)) if lines[i].strip().startswith("[")), len(lines))
            for i in range(start + 1, end):
                key = lines[i].split("=", 1)[0].strip()
                match = next((k for k in wanted if k.lower() == key.lower()), None)
                if "=" in lines[i] and not lines[i].strip().startswith(";") and match:
                    lines[i] = f"{match}={wanted.pop(match)}"
            lines[start + 1 : start + 1] = [f"{key}={value}" for key, value in wanted.items()]
    elif path.is_file():
        return
    # CRLF, so Notepad on Windows 8 shows the lines.
    path.write_text("\r\n".join(lines) + "\r\n", encoding="utf-8", newline="")
