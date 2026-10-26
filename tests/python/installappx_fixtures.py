"""Fake packages, executables and build folders for the installappx tests."""

import importlib.util
import pathlib
import struct
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]


def load_installer():
    spec = importlib.util.spec_from_file_location('installappx', ROOT / 'installappx.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def pe(machine=0x14C, managed=False, dll=False, appcontainer=False):
    """A PE32 (x86) or PE32+ (x64) image header, as the loader and setup read it."""
    data = bytearray(512)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3C, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<H', data, 0x84, machine)
    plus = machine == 0x8664
    struct.pack_into('<H', data, 0x94, 240 if plus else 224)
    struct.pack_into('<H', data, 0x96, 0x2102 if dll else 0x102)
    struct.pack_into('<H', data, 0x98, 0x20B if plus else 0x10B)
    struct.pack_into('<H', data, 0x98 + 70, 0x9140 if appcontainer else 0x8140)
    if managed:
        # The CLR runtime header is data directory 14.
        struct.pack_into('<I', data, 0x98 + (112 if plus else 96) + 14 * 8, 0x2000)
    return bytes(data)


def manifest_xml(name='Example.Game', version='1.0.0.0', arch='x86', executable='Game.exe', vclibs=('120',)):
    dependencies = ''.join(
        f'<PackageDependency Name="Microsoft.VCLibs.{v}.00" MinVersion="11.0.0.0" />' for v in vclibs)
    return f'''<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/2010/manifest">
  <Identity Name="{name}" Publisher="CN=Example" Version="{version}" ProcessorArchitecture="{arch}" />
  <Applications><Application Id="App" Executable="{executable}" EntryPoint="Example.App" /></Applications>
  <Dependencies>{dependencies}</Dependencies>
</Package>'''.encode()


def game_files(machine=0x14C, managed=False, **manifest):
    return {'AppxManifest.xml': manifest_xml(**manifest), manifest.get('executable', 'Game.exe'): pe(machine, managed)}


def appx(path, files):
    path = pathlib.Path(path)
    with zipfile.ZipFile(path, 'w') as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    return path


def bundle(path, packages):
    """packages: (type, architecture, file name, files) for each inner package."""
    path = pathlib.Path(path)
    entries = ''.join(
        f'<Package Type="{kind}" Architecture="{arch}" FileName="{name}" Version="1.0.0.0" />'
        for kind, arch, name, _ in packages)
    with zipfile.ZipFile(path, 'w') as archive:
        archive.writestr('AppxMetadata/AppxBundleManifest.xml',
                         f'<Bundle xmlns="http://schemas.microsoft.com/appx/2013/bundle"><Packages>{entries}'
                         '</Packages></Bundle>')
        for _, _, name, files in packages:
            inner = pathlib.Path(str(path) + '.' + name)
            appx(inner, files)
            archive.write(inner, name)
            inner.unlink()
    return path


VCLIBS = {'110': ['msvcr110.dll', 'msvcp110.dll', 'vccorlib110.dll'],
          '120': ['msvcr120_app.dll', 'msvcp120_app.dll', 'vccorlib120_app.dll'],
          '140': ['vcruntime140_app.dll', 'msvcp140_app.dll', 'vccorlib140_app.dll']}
SHIM_DLLS = ['appxportable.dll', 'd3dcompiler_47.dll', 'XAudio2_8.dll', 'DWrite.dll']


def build_folder(path, machine=0x14C, host=True, xaudio29=False):
    path = pathlib.Path(path)
    path.mkdir(parents=True, exist_ok=True)
    for name in SHIM_DLLS + (['XAudio2_9.dll'] if xaudio29 else []):
        (path / name).write_bytes(pe(machine, dll=True))
    if host:
        (path / 'appx-host.exe').write_bytes(pe(machine, managed=True))
    return path


def vclibs_folder(path, version='120', machine=0x14C, extra=()):
    path = pathlib.Path(path)
    path.mkdir(parents=True, exist_ok=True)
    for name in VCLIBS[version] + list(extra):
        (path / name).write_bytes(pe(machine, dll=True))
    return path
