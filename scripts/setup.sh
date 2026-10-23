#!/usr/bin/env bash
# Stages an extracted Store package as a portable game folder for Wine; the
# same steps as setup.ps1. Runtimes hold the x86 VCLibs DLLs when the game
# folder lacks them: a Microsoft.VCLibs.*.appx, or its extracted folder.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
if [[ $# -lt 2 ]]; then
  echo 'usage: setup.sh UNPACKED_GAME_DIRECTORY DESTINATION [BUILD_DIRECTORY [RUNTIME_APPX_OR_DIRECTORY...]]' >&2; exit 2
fi
source_dir=$(cd "$1" && pwd -P)
python3 - "$source_dir" "$2" <<'CHECK_PATHS'
import pathlib, sys
source, destination = (pathlib.Path(p).resolve() for p in sys.argv[1:])
if source == destination or source in destination.parents or destination in source.parents:
    sys.exit('Source and destination must not overlap.')
if destination.exists() and any(p.is_symlink() for p in destination.rglob('*')):
    sys.exit('Destination contains symbolic links; use a separate clean directory.')
CHECK_PATHS
build=${3:-"$root/build/macos-x86"}
runtimes=("${@:4}")
# Validates everything before writing, then stages; prints the executable's name.
executable=$(python3 - "$source_dir" "$2" "$build" "$root/scripts/appx-portable.ini" ${runtimes[@]+"${runtimes[@]}"} <<'STAGE'
import atexit, os, pathlib, shutil, sys, tempfile, urllib.parse, zipfile
import xml.etree.ElementTree as ElementTree
source, destination, build, ini = map(pathlib.Path, sys.argv[1:5])
runtimes = [pathlib.Path(p) for p in sys.argv[5:]]
VCLIBS = {
    'Microsoft.VCLibs.110.00': ('msvcr110.dll', 'msvcp110.dll', 'vccorlib110.dll'),
    'Microsoft.VCLibs.120.00': ('msvcr120_app.dll', 'msvcp120_app.dll', 'vccorlib120_app.dll'),
    'Microsoft.VCLibs.140.00': ('vcruntime140_app.dll', 'msvcp140_app.dll', 'vccorlib140_app.dll'),
}
PROXIES = ('d3dcompiler_47.dll', 'XAudio2_8.dll')
EXCLUDED = {'AppxSignature.p7x', 'AppxBlockMap.xml', '[Content_Types].xml', 'AppxMetadata',
            'microsoft.system.package.metadata', 'SaveData', 'appx-portable.ini', 'appxportable.dll', *PROXIES}
manifest = source / 'AppxManifest.xml'
if not manifest.is_file():
    sys.exit(f'Missing {manifest}. Extract the whole .appx package, not only its files.')
# Elements by local name: Windows 8, 8.1 and 10 manifests use different namespaces.
tree = ElementTree.parse(manifest)
local = lambda element: element.tag.rsplit('}', 1)[-1]
application = next((e for e in tree.iter() if local(e) == 'Application'), None)
executable = application.get('Executable', '') if application is not None else ''
if not executable or os.path.isabs(executable) or '..' in pathlib.PureWindowsPath(executable).parts:
    sys.exit(f'{manifest} has an unusable Executable: {executable!r}')
executable = pathlib.PureWindowsPath(executable).as_posix()
if not (source / executable).is_file():
    sys.exit(f'Missing {source / executable}')
# An .appx is a zip with the VCLibs DLLs at its root. Only those names are
# extracted, so entry paths never reach the file system.
def expand_runtime(package):
    if package.suffix.lower() != '.appx':
        sys.exit(f'Not a folder or an .appx package: {package}')
    names = {name.lower(): name for names in VCLIBS.values() for name in names}
    folder = pathlib.Path(tempfile.mkdtemp(prefix='appx-vclibs.'))
    atexit.register(shutil.rmtree, folder, True)
    try:
        with zipfile.ZipFile(package) as archive:
            for entry in archive.namelist():
                if entry.lower() in names:
                    (folder / names[entry.lower()]).write_bytes(archive.read(entry))
    except zipfile.BadZipFile:
        sys.exit(f'Cannot read {package} as an .appx package.')
    return folder
runtimes = [expand_runtime(path) if path.is_file() else path for path in runtimes]
runtime_files = {}
for dependency in (e.get('Name') for e in tree.iter() if local(e) == 'PackageDependency'):
    for name in VCLIBS.get(dependency, ()):
        found = next((folder / name for folder in [source, *runtimes] if (folder / name).is_file()), None)
        if found is None:
            sys.exit(f'Missing {name} ({dependency}). Pass the x86 {dependency} .appx, or a folder with its DLLs, '
                     "as a runtime. 'Get the Visual C++ runtime' in README.md says where to download it.")
        runtime_files[name] = found
for name in ('appxportable.dll', *PROXIES):
    if not (build / name).is_file():
        sys.exit(f'Build output missing: {build / name}')
destination.mkdir(parents=True, exist_ok=True)
destination = destination.resolve()
for item in source.iterdir():
    if item.name in EXCLUDED or (item.name.startswith('appx-portable') and item.name.endswith('.log')):
        continue
    target = destination / item.name
    if item.is_dir():
        shutil.copytree(item, target, dirs_exist_ok=True)
    else:
        shutil.copy2(item, target)
for name, path in runtime_files.items():
    shutil.copy2(path, destination / name)
# A raw .appx extraction keeps OPC percent-encoded names (%21_Name.xml,
# User%20Controls); games open the decoded names. Saves are left alone.
for parent, directories, files in os.walk(destination, topdown=False):
    if pathlib.Path(parent) == destination:
        directories = [d for d in directories if d != 'SaveData']
    elif (destination / 'SaveData') in pathlib.Path(parent).parents or pathlib.Path(parent) == destination / 'SaveData':
        continue
    for name in files + directories:
        decoded = urllib.parse.unquote(name)
        if decoded == name:
            continue
        if '/' in decoded or '\0' in decoded:
            sys.exit(f'Cannot decode package name: {os.path.join(parent, name)}')
        target = os.path.join(parent, decoded)
        if os.path.isdir(target) and not os.path.islink(target):
            shutil.rmtree(target)
        os.replace(os.path.join(parent, name), target)
# Settings template, copied once so later edits survive re-staging.
if not (destination / 'appx-portable.ini').exists():
    shutil.copy(ini, destination / 'appx-portable.ini')
# Replace DLLs by rename, never in place: a running game maps the old file.
for name in ('appxportable.dll', *PROXIES):
    fd, temporary = tempfile.mkstemp(prefix=name + '.', dir=destination)
    os.close(fd)
    try:
        shutil.copy2(build / name, temporary)
        os.replace(temporary, destination / name)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
print(executable)
STAGE
)
destination=$(cd "$2" && pwd)
cmp -s "$source_dir/$executable" "$destination/$executable"
shasum -a 256 "$source_dir/$executable" "$destination/$executable"
printf 'Prepared %s\n' "$destination"
