import ast
import pathlib
import tempfile
import unittest
from unittest import mock

from installappx_fixtures import ROOT, appx, bundle, game_files, load_installer, manifest_xml, pe

installer = load_installer()


class PackageTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.base = pathlib.Path(folder.name)
        self.dest = self.base / 'dest'

    def raw_package_files(self):
        files = game_files()
        files.update({
            'Assets/%21_Doc.xml': b'design',
            'User%20Controls/How%20To.xaml': b'control',
            'AppxSignature.p7x': b'signature',
            'AppxBlockMap.xml': b'blockmap',
            '[Content_Types].xml': b'types',
            'AppxMetadata/CodeIntegrity.cat': b'catalog',
        })
        return files

    def assert_staged(self, manifest):
        self.assertEqual((self.dest / 'Assets' / '!_Doc.xml').read_bytes(), b'design')
        self.assertEqual((self.dest / 'User Controls' / 'How To.xaml').read_bytes(), b'control')
        self.assertTrue((self.dest / 'AppxManifest.xml').is_file(), 'the runtime reads the manifest')
        for name in ['AppxSignature.p7x', 'AppxBlockMap.xml', '[Content_Types].xml', 'AppxMetadata']:
            self.assertFalse((self.dest / name).exists(), name)
        self.assertEqual(manifest.executable, 'Game.exe')
        self.assertEqual((manifest.name, manifest.version), ('Example.Game', '1.0.0.0'))
        self.assertEqual(manifest.dependencies, ['Microsoft.VCLibs.120.00'])

    def test_appx_extracts_and_decodes_names(self):
        package = appx(self.base / 'game.appx', self.raw_package_files())
        self.assert_staged(installer.extract_game(package, self.dest))

    def test_folder_source_is_copied(self):
        source = self.base / 'source'
        for name, data in self.raw_package_files().items():
            (source / name).parent.mkdir(parents=True, exist_ok=True)
            (source / name).write_bytes(data)
        self.assert_staged(installer.extract_game(source, self.dest))

    def test_refuses_escaping_entries(self):
        for name in ['../evil.txt', '%2E%2E/evil.txt', '/evil.txt', 'C:/evil.txt', 'a\\..\\..\\evil.txt']:
            with self.subTest(name=name):
                files = game_files()
                files[name] = b'evil'
                package = appx(self.base / 'evil.appx', files)
                with self.assertRaises(installer.InstallError):
                    installer.extract_game(package, self.dest)
                self.assertFalse((self.base / 'evil.txt').exists())
                self.assertFalse(self.dest.exists(), 'nothing is written before every name is checked')

    def test_bundle_prefers_x86_and_merges_resources(self):
        x86 = game_files(machine=0x14C)
        x86['resources.pri'] = b'main'
        resource = {'AppxManifest.xml': b'<Package />', 'Assets/Logo.scale-140.png': b'logo', 'resources.pri': b'scale'}
        package = bundle(self.base / 'game.appxbundle', [
            ('application', 'x64', 'Game_x64.appx', game_files(machine=0x8664, arch='x64')),
            ('application', 'x86', 'Game_x86.appx', x86),
            ('resource', '', 'Game_scale-140.appx', resource),
        ])
        installer.extract_game(package, self.dest)
        self.assertEqual((self.dest / 'Game.exe').read_bytes(), x86['Game.exe'])
        self.assertEqual((self.dest / 'Assets' / 'Logo.scale-140.png').read_bytes(), b'logo')
        self.assertEqual((self.dest / 'resources.pri').read_bytes(), b'main')
        self.assertIn(b'Example.Game', (self.dest / 'AppxManifest.xml').read_bytes())

    def test_bundle_neutral_before_x64(self):
        package = bundle(self.base / 'game.appxbundle', [
            ('application', 'x64', 'Game_x64.appx', game_files(machine=0x8664, arch='x64')),
            ('application', 'neutral', 'Game_neutral.appx', game_files(managed=True, arch='neutral')),
        ])
        manifest = installer.extract_game(package, self.dest)
        self.assertEqual(manifest.architecture, 'neutral')
        self.assertEqual((self.dest / 'Game.exe').read_bytes(), game_files(managed=True)['Game.exe'])

    def test_windows10_bundle_prefers_x64(self):
        package = bundle(self.base / 'game.appxbundle', [
            ('application', 'x86', 'Game_x86.appx', game_files(universal=True)),
            ('application', 'x64', 'Game_x64.appx', game_files(machine=0x8664, arch='x64', universal=True)),
        ])
        manifest = installer.extract_game(package, self.dest)
        self.assertTrue(manifest.universal)
        self.assertEqual(manifest.architecture, 'x64')
        self.assertEqual((self.dest / 'Game.exe').read_bytes(), game_files(machine=0x8664)['Game.exe'])

    def test_windows10_bundle_on_32bit_windows_takes_x86(self):
        package = bundle(self.base / 'game.appxbundle', [
            ('application', 'x86', 'Game_x86.appx', game_files(universal=True)),
            ('application', 'x64', 'Game_x64.appx', game_files(machine=0x8664, arch='x64', universal=True)),
        ])
        with mock.patch.object(installer, 'windows_is_32bit', return_value=True):
            self.assertEqual(installer.extract_game(package, self.dest).architecture, 'x86')

    def test_windows10_bundle_without_x64_takes_x86(self):
        package = bundle(self.base / 'game.appxbundle', [
            ('application', 'arm', 'Game_arm.appx', game_files(universal=True)),
            ('application', 'x86', 'Game_x86.appx', game_files(universal=True)),
        ])
        self.assertEqual(installer.extract_game(package, self.dest).architecture, 'x86')

    def test_arm_only_bundle_fails(self):
        package = bundle(self.base / 'game.appxbundle', [
            ('application', 'arm', 'Game_arm.appx', game_files()),
            ('application', 'arm64', 'Game_arm64.appx', game_files()),
        ])
        with self.assertRaisesRegex(installer.InstallError, 'ARM'):
            installer.extract_game(package, self.dest)

    def test_bundle_without_manifest_fails(self):
        package = appx(self.base / 'game.appxbundle', {'Game_x86.appx': b'not really'})
        with self.assertRaises(installer.InstallError):
            installer.extract_game(package, self.dest)

    def test_rerun_keeps_saves_ini_and_logs(self):
        package = appx(self.base / 'game.appx', game_files())
        installer.extract_game(package, self.dest)
        (self.dest / 'SaveData').mkdir()
        (self.dest / 'SaveData' / 'save.dat').write_bytes(b'progress')
        (self.dest / 'appx-portable.ini').write_bytes(b'[storage]\r\nSaveLocation=documents\r\n')
        (self.dest / 'appx-portable.log').write_bytes(b'log')
        files = game_files()
        files.update({'SaveData/save.dat': b'package', 'appx-portable.ini': b'package', 'appx-portable.log': b'package'})
        installer.extract_game(appx(self.base / 'again.appx', files), self.dest)
        self.assertEqual((self.dest / 'SaveData' / 'save.dat').read_bytes(), b'progress')
        self.assertEqual((self.dest / 'appx-portable.ini').read_bytes(), b'[storage]\r\nSaveLocation=documents\r\n')
        self.assertEqual((self.dest / 'appx-portable.log').read_bytes(), b'log')

    def test_corrupt_package_fails(self):
        package = self.base / 'game.appx'
        package.write_bytes(b'not a zip')
        with self.assertRaises(installer.InstallError):
            installer.extract_game(package, self.dest)

    def test_missing_executable_fails(self):
        package = appx(self.base / 'game.appx', {'AppxManifest.xml': manifest_xml()})
        with self.assertRaisesRegex(installer.InstallError, 'Game.exe'):
            installer.extract_game(package, self.dest)

    def test_manifest_rejects_escaping_executable(self):
        for executable in ['..\\Game.exe', 'C:\\Game.exe', '\\Game.exe']:
            with self.subTest(executable=executable):
                with self.assertRaises(installer.InstallError):
                    installer.read_manifest(manifest_xml(executable=executable))

    def test_manifest_executable_in_subfolder(self):
        self.assertEqual(installer.read_manifest(manifest_xml(executable='Sub\\Game.exe')).executable, 'Sub/Game.exe')

    def test_pe_info(self):
        path = self.base / 'image'
        for machine in [0x14C, 0x8664]:
            for managed in [False, True]:
                for dll in [False, True]:
                    path.write_bytes(pe(machine, managed=managed, dll=dll))
                    info = installer.pe_info(path)
                    self.assertEqual((info.machine, info.managed, info.dll), (machine, managed, dll))
        path.write_bytes(b'MZ')
        with self.assertRaises(installer.InstallError):
            installer.pe_info(path)

    def test_python_38_syntax(self):
        ast.parse((ROOT / 'installappx.py').read_text(encoding='utf-8'), feature_version=(3, 8))



class ReviewPackageTests(unittest.TestCase):
    def test_manifest_rejects_unsafe_identity_name(self):
        for name in ['../../escaped', 'C:\\Users\\x', '/tmp/x', '...', 'a b', 'x' * 51]:
            with self.subTest(name=name):
                with self.assertRaises(installer.InstallError):
                    installer.read_manifest(manifest_xml(name=name))
        for name in ['PopCap.BejweledLIVE', 'Ubisoft.RaymanFiestaRunWindows10Edition', 'ZeptoLabUKLimited.CuttheRope2']:
            self.assertEqual(installer.read_manifest(manifest_xml(name=name)).name, name)


if __name__ == '__main__':
    unittest.main()
