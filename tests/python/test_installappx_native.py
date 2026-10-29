import hashlib
import pathlib
import tempfile
import unittest
from unittest import mock

from installappx_fixtures import appx, build_folder, load_installer, pe

installer = load_installer()
FRAMEWORK = 'Microsoft.NET.Native.Framework.1.3'
RUNTIME = 'Microsoft.NET.Native.Runtime.1.4'


class NativeTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.base = pathlib.Path(temporary.name)
        self.game = self.base / 'game'
        self.game.mkdir()

    def framework(self, name, arch='x64'):
        folder = self.base / (name + arch)
        folder.mkdir()
        folder.joinpath('AppxManifest.xml').write_text(
            f'<Package><Identity Name="{name}" ProcessorArchitecture="{arch}"/></Package>')
        for filename in installer.NATIVE_FRAMEWORKS[name]:
            folder.joinpath(filename).write_bytes(pe(0x8664 if arch == 'x64' else 0x14c, dll=True))
        return folder

    def test_forwarding_executable_detection(self):
        executable = self.game / 'Game.exe'
        executable.write_bytes(pe(0x8664, imports=['game.dll']))
        self.assertFalse(installer.native_compiled_application(executable))
        executable.with_suffix('.dll').write_bytes(pe(0x8664, dll=True, imports=['SharedLibrary.dll']))
        self.assertTrue(installer.native_compiled_application(executable))
        executable.with_suffix('.dll').write_bytes(pe(0x8664, dll=True, imports=['kernel32.dll']))
        self.assertFalse(installer.native_compiled_application(executable))

    def test_framework_folder_and_archive_preserve_game_dll(self):
        runtime = self.framework(RUNTIME)
        original = pe(0x8664, dll=True) + b'game-owned'
        self.game.joinpath('clrcompression.dll').write_bytes(original)
        installer.add_native_framework(RUNTIME, runtime, self.game, 'x64')
        self.assertEqual(self.game.joinpath('clrcompression.dll').read_bytes(), original)
        framework = self.framework(FRAMEWORK)
        package = appx(self.base / 'framework.appx', {p.name: p.read_bytes() for p in framework.iterdir()})
        installer.add_native_framework(FRAMEWORK, package, self.game, 'x64')
        self.assertTrue(self.game.joinpath('SharedLibrary.dll').is_file())

    def test_wrong_identity_or_architecture_cannot_stage(self):
        folder = self.framework(FRAMEWORK, 'x86')
        for dependency in (FRAMEWORK, RUNTIME):
            with self.assertRaises(installer.InstallError):
                installer.add_native_framework(dependency, folder, self.game, 'x64')
        folder = self.framework(FRAMEWORK)
        folder.joinpath('SharedLibrary.dll').write_bytes(pe(0x14c, dll=True))
        with self.assertRaises(installer.InstallError):
            installer.add_native_framework(FRAMEWORK, folder, self.game, 'x64')
        self.assertEqual(list(self.game.iterdir()), [])

    def test_installed_frameworks_and_missing_dependency(self):
        manifest = installer.Manifest('Native.Game', '1.0.0.0', 'x64', 'Game.exe', [FRAMEWORK, RUNTIME])
        folders = [self.framework(FRAMEWORK), self.framework(RUNTIME)]
        with mock.patch.object(installer, 'installed_native_frameworks', return_value=folders):
            installer.add_native_frameworks(manifest, self.game, 'x64', [])
        self.assertTrue(all((self.game / n).is_file() for d in manifest.dependencies for n in installer.NATIVE_FRAMEWORKS[d]))
        self.game.joinpath('mrt100_app.dll').unlink()
        with mock.patch.object(installer, 'installed_native_frameworks', return_value=[]), \
                mock.patch.object(installer, 'download_native_framework', side_effect=installer.InstallError('offline')):
            with self.assertRaisesRegex(installer.InstallError, '--dependencies'):
                installer.add_native_frameworks(manifest, self.game, 'x64', [])

    def test_native_dependency_download_is_pinned_and_hash_checked(self):
        folder = self.framework(RUNTIME)
        package = appx(self.base / 'runtime.appx', {p.name: p.read_bytes() for p in folder.iterdir()})
        payload = package.read_bytes()
        name = 'runtime.appx'
        manifest = installer.Manifest('Native.Game', '1.0.0.0', 'x64', 'Game.exe', [RUNTIME])
        with mock.patch.dict(installer.NATIVE_PACKAGES, {(RUNTIME, 'x64'): (name, hashlib.sha256(payload).hexdigest())}), \
                mock.patch.object(installer, 'installed_native_frameworks', return_value=[]), \
                mock.patch.object(installer, 'fetch', return_value=payload) as fetch:
            installer.add_native_frameworks(manifest, self.game, 'x64', [])
            fetch.assert_called_once_with(installer.NATIVE_RELEASE + name, 'pass it with --dependencies')
        self.assertTrue(self.game.joinpath('mrt100_app.dll').is_file())
        with mock.patch.object(installer, 'fetch', return_value=b'changed release asset'):
            with self.assertRaisesRegex(installer.InstallError, 'SHA-256'):
                installer.download_native_framework(RUNTIME, self.game, 'x64')

    def test_shared_dependency_option_accepts_both_package_types(self):
        args = installer.parse_arguments(['--dependencies', 'vclibs.appx', '--dependencies', 'native.appx', '--yes'])
        self.assertEqual(args.dependencies, ['vclibs.appx', 'native.appx'])

    def test_native_host_is_required_and_selected_without_clr_hosting(self):
        build = build_folder(self.base / 'build', machine=0x8664, host=False)
        with self.assertRaisesRegex(installer.InstallError, 'appx-native-host.exe'):
            installer.stage_runtime(build, self.game, 'x64', managed=False, native_compiled=True)
        build.joinpath('appx-native-host.exe').write_bytes(pe(0x8664))
        installer.stage_runtime(build, self.game, 'x64', managed=False, native_compiled=True)
        self.assertTrue(self.game.joinpath('appx-native-host.exe').is_file())
        self.assertEqual(installer.launch_executable('Game.exe', False, True), 'appx-native-host.exe')
        with self.assertRaisesRegex(installer.InstallError, 'requires Windows'):
            installer.stage_runtime(build, self.game, 'x64', managed=False, wine=True, native_compiled=True)


if __name__ == '__main__':
    unittest.main()
