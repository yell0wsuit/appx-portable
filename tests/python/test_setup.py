import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]
# A minimal x86 PE header: setup.ps1 checks MZ, the PE signature, and I386.
X86_PE = b'MZ' + bytes(0x3a) + struct.pack('<I', 0x40) + b'PE' + bytes(2) + struct.pack('<H', 0x14c) + bytes(2)
SHIM_DLLS = ['appxportable.dll', 'd3dcompiler_47.dll', 'XAudio2_8.dll']
VCLIBS = {'110': ['msvcr110.dll', 'msvcp110.dll', 'vccorlib110.dll'],
          '120': ['msvcr120_app.dll', 'msvcp120_app.dll', 'vccorlib120_app.dll']}


def manifest(executable, vclibs):
    return f'''<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/2010/manifest">
  <Identity Name="Example.Game" Publisher="CN=Example" Version="1.0.0.0" ProcessorArchitecture="x86" />
  <Applications><Application Id="App" Executable="{executable}" EntryPoint="Example.App" /></Applications>
  <Dependencies><PackageDependency Name="Microsoft.VCLibs.{vclibs}.00" MinVersion="11.0.0.0" /></Dependencies>
</Package>'''.encode()


def stage_inputs(base, executable='Game.exe', vclibs='120', runtime_beside_game=True):
    source = base/'source'; build = base/'build'; dest = base/'dest'
    maps = source/'Assets'/'assets'/'maps'; maps.mkdir(parents=True); build.mkdir()
    (source/'AppxManifest.xml').write_bytes(manifest(executable, vclibs))
    (source/executable).write_bytes(X86_PE)
    if runtime_beside_game:
        for name in VCLIBS[vclibs]:
            (source/name).write_bytes(X86_PE)
    for name in SHIM_DLLS:
        (build/name).write_bytes(X86_PE + b'shim')
    # Package files the game never reads.
    (source/'AppxSignature.p7x').write_bytes(b'signature')
    (source/'AppxMetadata').mkdir(); (source/'AppxMetadata'/'CodeIntegrity.cat').write_bytes(b'catalog')
    # Raw .appx extraction keeps OPC percent-encoded names.
    (maps/'%21_MasterGameDesignDocument.xml').write_bytes(b'design')
    (source/'User%20Controls').mkdir(); (source/'User%20Controls'/'How%20To.xaml').write_bytes(b'control')
    return source, build, dest


@unittest.skipUnless(shutil.which('pwsh'), 'needs PowerShell 7')
class PowerShellSetupTests(unittest.TestCase):
    def run_setup(self, source, dest, build, *extra):
        return subprocess.run(['pwsh', '-NoProfile', '-NonInteractive', '-File', str(ROOT/'scripts'/'setup.ps1'),
                               '-SourceDirectory', str(source), '-Destination', str(dest), '-BuildDirectory', str(build),
                               *extra], capture_output=True)

    def test_decodes_names_and_restages(self):
        with tempfile.TemporaryDirectory() as temp:
            source, build, dest = stage_inputs(pathlib.Path(temp))
            result = self.run_setup(source, dest, build)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
            self.assertEqual((dest/'Assets'/'assets'/'maps'/'!_MasterGameDesignDocument.xml').read_bytes(), b'design')
            self.assertFalse((dest/'Assets'/'assets'/'maps'/'%21_MasterGameDesignDocument.xml').exists())
            self.assertEqual((dest/'User Controls'/'How To.xaml').read_bytes(), b'control')
            self.assertTrue((dest/'AppxManifest.xml').is_file(), 'the runtime reads the manifest')
            self.assertFalse((dest/'AppxSignature.p7x').exists())
            self.assertFalse((dest/'AppxMetadata').exists())
            for name in SHIM_DLLS + VCLIBS['120']:
                self.assertTrue((dest/name).is_file(), name)
            self.assertIn(b'SaveLocation=game', (dest/'appx-portable.ini').read_bytes())
            (dest/'appx-portable.ini').write_bytes(b'[storage]\r\nSaveLocation=documents\r\n')
            (dest/'SaveData'/'Local').mkdir(parents=True)
            (dest/'SaveData'/'Local'/'slot%201.dat').write_bytes(b'save')
            # Re-staging replaces the shim DLLs in place, decodes again, and keeps settings and saves.
            (build/'appxportable.dll').write_bytes(X86_PE + b'new shim')
            result = self.run_setup(source, dest, build)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
            self.assertEqual((dest/'appxportable.dll').read_bytes(), X86_PE + b'new shim')
            self.assertFalse((dest/'User%20Controls').exists())
            self.assertEqual((dest/'User Controls'/'How To.xaml').read_bytes(), b'control')
            self.assertIn(b'SaveLocation=documents', (dest/'appx-portable.ini').read_bytes())
            self.assertEqual((dest/'SaveData'/'Local'/'slot%201.dat').read_bytes(), b'save')

    def test_vclibs_110_from_runtime_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            base = pathlib.Path(temp)
            source, build, dest = stage_inputs(base, 'Game_Win8.exe', '110', runtime_beside_game=False)
            runtime = base/'vclibs'; runtime.mkdir()
            for name in VCLIBS['110']:
                (runtime/name).write_bytes(X86_PE + b'vclibs')
            result = self.run_setup(source, dest, build, '-RuntimeDirectory', str(runtime))
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
            self.assertEqual((dest/'Game_Win8.exe').read_bytes(), X86_PE)
            for name in VCLIBS['110']:
                self.assertEqual((dest/name).read_bytes(), X86_PE + b'vclibs')

    def test_missing_runtime_fails_before_writing(self):
        with tempfile.TemporaryDirectory() as temp:
            source, build, dest = stage_inputs(pathlib.Path(temp), vclibs='110', runtime_beside_game=False)
            result = self.run_setup(source, dest, build)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('msvcr110.dll', ' '.join(result.stderr.decode(errors='replace').split()))
            self.assertFalse(dest.exists(), 'validation must run before any write')


@unittest.skipIf(sys.platform == 'win32', 'setup.sh stages the game for Wine on macOS')
class SetupTests(unittest.TestCase):
    def run_setup(self, source, dest, build, *runtimes):
        return subprocess.run(['bash', str(ROOT/'scripts/setup.sh'), str(source), str(dest), str(build),
                               *map(str, runtimes)], capture_output=True)

    def test_decodes_names(self):
        with tempfile.TemporaryDirectory() as temp:
            source, build, dest = stage_inputs(pathlib.Path(temp))
            result = self.run_setup(source, dest, build)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            self.assertEqual((dest/'Assets'/'assets'/'maps'/'!_MasterGameDesignDocument.xml').read_bytes(), b'design')
            self.assertFalse((dest/'Assets'/'assets'/'maps'/'%21_MasterGameDesignDocument.xml').exists())
            self.assertEqual((dest/'User Controls'/'How To.xaml').read_bytes(), b'control')
            self.assertFalse((dest/'AppxSignature.p7x').exists())
            for name in SHIM_DLLS + VCLIBS['120']:
                self.assertTrue((dest/name).is_file(), name)

    def test_vclibs_110_from_runtime_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            base = pathlib.Path(temp)
            source, build, dest = stage_inputs(base, 'Game_Win8.exe', '110', runtime_beside_game=False)
            runtime = base/'vclibs'; runtime.mkdir()
            for name in VCLIBS['110']:
                (runtime/name).write_bytes(b'vclibs')
            result = self.run_setup(source, dest, build, runtime)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            for name in VCLIBS['110']:
                self.assertEqual((dest/name).read_bytes(), b'vclibs')

    def test_missing_runtime_fails_before_writing(self):
        with tempfile.TemporaryDirectory() as temp:
            source, build, dest = stage_inputs(pathlib.Path(temp), vclibs='110', runtime_beside_game=False)
            result = self.run_setup(source, dest, build)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(dest.exists(), 'validation must run before any write')

    def test_rejects_destination_inside_source_before_writing(self):
        with tempfile.TemporaryDirectory() as temp:
            source, build, _ = stage_inputs(pathlib.Path(temp))
            destination = source/'nested'
            result = self.run_setup(source, destination, build)
            self.assertNotEqual(result.returncode, 0, 'overlapping stage must be rejected')
            self.assertFalse(destination.exists(), 'validation must run before any write into source')

    def test_rejects_destination_symlink(self):
        with tempfile.TemporaryDirectory() as temp:
            base = pathlib.Path(temp); source, build, dest = stage_inputs(base); dest.mkdir()
            unrelated = base/'unrelated'; unrelated.write_bytes(b'preserve'); (dest/'Game.exe').symlink_to(unrelated)
            result = self.run_setup(source, dest, build)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(unrelated.read_bytes(), b'preserve')

    def test_replaces_dll_without_mutating_existing_mapped_inode(self):
        with tempfile.TemporaryDirectory() as temp:
            base = pathlib.Path(temp); source, build, dest = stage_inputs(base); dest.mkdir()
            for name in SHIM_DLLS:
                (build/name).write_bytes(b'new shim')
                (dest/name).write_bytes(b'old mapped shim')
            with (dest/'appxportable.dll').open('rb') as mapped:
                result = self.run_setup(source, dest, build)
                self.assertEqual(result.returncode, 0, result.stderr.decode())
                self.assertEqual(mapped.read(), b'old mapped shim')
            self.assertEqual((dest/'appxportable.dll').read_bytes(), b'new shim')
            self.assertEqual((source/'Game.exe').read_bytes(), (dest/'Game.exe').read_bytes())
