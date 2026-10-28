import hashlib
import json
import pathlib
import tempfile
import unittest
import zipfile

from installappx_fixtures import VCLIBS, build_folder, load_installer, manifest_xml, pe, vclibs_folder

installer = load_installer()


class VCLibsTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.base = pathlib.Path(folder.name)
        self.game = self.base / 'game'
        self.game.mkdir()

    def vclibs_appx(self, version, machine=0x14C):
        path = self.base / f'Microsoft.VCLibs.{version}.appx'
        with zipfile.ZipFile(path, 'w') as appx:
            for name in VCLIBS[version]:
                appx.writestr(name, pe(machine, dll=True))
            appx.writestr('AppxManifest.xml', b'<Package />')
            appx.writestr('../escape.dll', pe(machine, dll=True))
        return path

    def test_vclibs_from_appx_and_folder(self):
        for version in VCLIBS:
            dependency = f'Microsoft.VCLibs.{version}.00'
            for source in [self.vclibs_appx(version), vclibs_folder(self.base / f'folder{version}', version)]:
                with self.subTest(version=version, source=source.name):
                    installer.add_vclibs(dependency, source, self.game, 'x86')
                    for name in VCLIBS[version]:
                        self.assertEqual((self.game / name).read_bytes(), pe(0x14C, dll=True))
        self.assertEqual(list(self.base.rglob('escape.dll')), [])
        self.assertFalse((self.game / 'AppxManifest.xml').exists())

    def test_missing_vclibs(self):
        manifest = installer.read_manifest(manifest_xml(vclibs=('110', '140')))
        self.assertEqual(installer.missing_vclibs(manifest, self.game),
                         ['Microsoft.VCLibs.110.00', 'Microsoft.VCLibs.140.00'])
        vclibs_folder(self.game, '110')
        self.assertEqual(installer.missing_vclibs(manifest, self.game), ['Microsoft.VCLibs.140.00'])
        vclibs_folder(self.game, '140')
        self.assertEqual(installer.missing_vclibs(manifest, self.game), [])

    def test_unrelated_dependencies_are_ignored(self):
        manifest = installer.read_manifest(manifest_xml(vclibs=()))
        manifest.dependencies.append('Microsoft.NET.Native.Runtime.1.7')
        self.assertEqual(installer.missing_vclibs(manifest, self.game), [])

    def test_vclibs_architecture_mismatch(self):
        source = vclibs_folder(self.base / 'x86', '140', machine=0x14C)
        with self.assertRaises(installer.InstallError) as raised:
            installer.add_vclibs('Microsoft.VCLibs.140.00', source, self.game, 'x64')
        self.assertIn('x86', str(raised.exception))
        self.assertIn('x64', str(raised.exception))
        self.assertFalse((self.game / 'vcruntime140_app.dll').exists())

    def test_vclibs_missing_files(self):
        source = self.base / 'empty'
        source.mkdir()
        with self.assertRaisesRegex(installer.InstallError, 'msvcr120_app.dll'):
            installer.add_vclibs('Microsoft.VCLibs.120.00', source, self.game, 'x86')

    def test_vclibs140_optional_dlls(self):
        source = vclibs_folder(self.base / 'v140', '140', machine=0x8664,
                               extra=['concrt140_app.dll', 'vcruntime140_1_app.dll'])
        installer.add_vclibs('Microsoft.VCLibs.140.00', source, self.game, 'x64')
        for name in ['concrt140_app.dll', 'vcruntime140_1_app.dll']:
            self.assertTrue((self.game / name).is_file(), name)

    def test_vclibs_downloads_cover_every_dependency(self):
        for version in ['110', '120', '140']:
            self.assertIn((f'Microsoft.VCLibs.{version}.00', 'x86'), installer.VCLIBS_DOWNLOADS)
        self.assertIn(('Microsoft.VCLibs.140.00', 'x64'), installer.VCLIBS_DOWNLOADS)


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.base = pathlib.Path(folder.name)
        self.game = self.base / 'game'
        self.game.mkdir()

    def test_native_game(self):
        build = build_folder(self.base / 'build')
        copied = installer.stage_runtime(build, self.game, 'x86', managed=False)
        self.assertEqual(sorted(copied), sorted(['appxportable.dll', 'd3dcompiler_47.dll', 'XAudio2_8.dll', 'DWrite.dll']))
        self.assertFalse((self.game / 'appx-host.exe').exists())
        self.assertEqual(list(self.game.glob('*.tmp')), [])

    def test_managed_game_gets_host(self):
        build = build_folder(self.base / 'build')
        self.assertIn('appx-host.exe', installer.stage_runtime(build, self.game, 'x86', managed=True))
        self.assertTrue((self.game / 'appx-host.exe').is_file())

    def test_managed_x64_is_refused(self):
        build = build_folder(self.base / 'build', machine=0x8664, host=False)
        with self.assertRaisesRegex(installer.InstallError, 'Managed x64'):
            installer.stage_runtime(build, self.game, 'x64', managed=True)

    def test_xaudio29_only_when_built(self):
        build = build_folder(self.base / 'build', machine=0x8664, xaudio29=True)
        self.assertIn('XAudio2_9.dll', installer.stage_runtime(build, self.game, 'x64', managed=False))

    def test_wrong_machine(self):
        build = build_folder(self.base / 'build', machine=0x8664)
        with self.assertRaisesRegex(installer.InstallError, 'x64'):
            installer.stage_runtime(build, self.game, 'x86', managed=False)

    def test_missing_build_file(self):
        build = build_folder(self.base / 'build')
        (build / 'DWrite.dll').unlink()
        with self.assertRaisesRegex(installer.InstallError, 'DWrite.dll'):
            installer.stage_runtime(build, self.game, 'x86', managed=False)

    def wine_graphics(self, build, machine=0x14C):
        hashes = {}
        for name in installer.WINE_GRAPHICS:
            data = pe(machine, dll=True) + name.encode()
            (build / name).write_bytes(data)
            hashes[name] = hashlib.sha256(data).hexdigest()
        (build / installer.WINE_GRAPHICS_MANIFEST).write_text(json.dumps({'sha256': hashes}))

    def test_wine_graphics_staged_under_wine(self):
        build = build_folder(self.base / 'build', machine=0x8664)
        self.wine_graphics(build, machine=0x8664)
        copied = installer.stage_runtime(build, self.game, 'x64', managed=False, wine=True)
        for name in [*installer.WINE_GRAPHICS, installer.WINE_GRAPHICS_MANIFEST]:
            self.assertIn(name, copied)
            self.assertEqual((self.game / name).read_bytes(), (build / name).read_bytes())

    def test_wine_graphics_not_staged_on_windows(self):
        build = build_folder(self.base / 'build')
        self.wine_graphics(build)
        installer.stage_runtime(build, self.game, 'x86', managed=False, wine=False)
        for name in [*installer.WINE_GRAPHICS, installer.WINE_GRAPHICS_MANIFEST]:
            self.assertFalse((self.game / name).exists(), name)

    def test_wine_graphics_must_match_manifest(self):
        build = build_folder(self.base / 'build')
        self.wine_graphics(build)
        (build / 'dxgi.dll').write_bytes(pe(0x14C, dll=True) + b'rebuilt')
        with self.assertRaisesRegex(installer.InstallError, 'dxgi.dll'):
            installer.stage_runtime(build, self.game, 'x86', managed=False, wine=True)
        self.assertFalse((self.game / 'appxportable.dll').exists())

    def test_wine_graphics_architecture(self):
        build = build_folder(self.base / 'build')
        self.wine_graphics(build, machine=0x8664)
        with self.assertRaisesRegex(installer.InstallError, 'wined3d.dll|d3d11.dll|dxgi.dll'):
            installer.stage_runtime(build, self.game, 'x86', managed=False, wine=True)

    def test_replaces_existing_dlls(self):
        (self.game / 'appxportable.dll').write_bytes(b'old')
        installer.stage_runtime(build_folder(self.base / 'build'), self.game, 'x86', managed=False)
        self.assertEqual((self.game / 'appxportable.dll').read_bytes(), pe(0x14C, dll=True))


class SettingsTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.game = pathlib.Path(folder.name)
        self.ini = self.game / 'appx-portable.ini'

    def values(self):
        text = self.ini.read_text()
        return ('UnlockTrial=1' in text, 'StubIap=1' in text)

    def test_template_and_choices(self):
        installer.write_settings(self.game, None)
        text = self.ini.read_text()
        self.assertIn('SaveLocation=game', text)
        self.assertIn('[options]', text)
        self.assertIn('FrameRate=60', text)
        self.assertEqual(installer.read_store(self.game), 'none')
        expected = {'none': (False, False), 'trial': (True, False), 'iap': (False, True), 'both': (True, True)}
        for store, values in expected.items():
            with self.subTest(store=store):
                installer.write_settings(self.game, store)
                self.assertEqual(self.values(), values)
                self.assertEqual(installer.read_store(self.game), store)

    def test_keep_keeps_choice(self):
        installer.write_settings(self.game, 'trial')
        installer.write_settings(self.game, None)
        self.assertEqual(installer.read_store(self.game), 'trial')

    def test_keep_edits(self):
        self.ini.write_text('; my note\n[storage]\nSaveLocation=documents\n')
        installer.write_settings(self.game, 'both')
        text = self.ini.read_text()
        self.assertIn('; my note', text)
        self.assertIn('SaveLocation=documents', text)
        self.assertEqual(self.values(), (True, True))
        self.assertEqual(text.count('[options]'), 1)
        installer.write_settings(self.game, 'iap')
        self.assertEqual(self.values(), (False, True))
        self.assertEqual(self.ini.read_text().count('StubIap='), 1)

    def test_no_file_reads_as_none(self):
        self.assertEqual(installer.read_store(self.game), 'none')



class ReviewSettingsTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.game = pathlib.Path(folder.name)
        self.ini = self.game / 'appx-portable.ini'

    def test_ansi_ini_is_kept_byte_for_byte(self):
        # Windows 8 Notepad saves ANSI: a non-ASCII save folder is not UTF-8.
        self.ini.write_bytes(b'[storage]\r\nSaveLocation=D:\\Sp\xe9l\r\n')
        installer.write_settings(self.game, 'trial')
        data = self.ini.read_bytes()
        self.assertIn(b'SaveLocation=D:\\Sp\xe9l', data)
        self.assertIn(b'UnlockTrial=1', data)
        self.assertEqual(installer.read_store(self.game), 'trial')

    def test_utf16_ini_stays_utf16(self):
        self.ini.write_bytes('[storage]\r\nSaveLocation=D:\\\u03a0\r\n'.encode('utf-16'))
        installer.write_settings(self.game, 'iap')
        text = self.ini.read_bytes().decode('utf-16')
        self.assertIn('SaveLocation=D:\\\u03a0', text)
        self.assertIn('StubIap=1', text)
        self.assertEqual(installer.read_store(self.game), 'iap')


if __name__ == '__main__':
    unittest.main()
