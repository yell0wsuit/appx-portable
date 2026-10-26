import contextlib
import io
import pathlib
import sys
import tempfile
import unittest
import zipfile
from unittest import mock

from installappx_fixtures import VCLIBS, appx, build_folder, game_files, load_installer, pe, vclibs_folder

installer = load_installer()
LAUNCHER = '00_click2startme.bat' if sys.platform == 'win32' else '00_click2startme.sh'


class Answers:
    def __init__(self, *answers):
        self.answers = list(answers)
        self.prompts = []

    def __call__(self, prompt):
        self.prompts.append(prompt)
        if not self.answers:
            raise AssertionError(f'Unexpected prompt: {prompt}')
        return self.answers.pop(0)


class CliTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.base = pathlib.Path(folder.name)
        self.package = appx(self.base / 'game.appx', game_files())
        self.build = build_folder(self.base / 'build')
        self.vclibs = vclibs_folder(self.base / 'vclibs', '120')
        self.dest = self.base / 'Installed'
        self.stdout, self.stderr = io.StringIO(), io.StringIO()

    def main(self, argv, ask=None):
        with contextlib.redirect_stdout(self.stdout), contextlib.redirect_stderr(self.stderr):
            return installer.main(argv, ask or Answers())

    def options(self):
        text = (self.dest / 'appx-portable.ini').read_text()
        return ('UnlockTrial=1' in text, 'StubIap=1' in text)

    def scripted(self, *extra):
        return [str(self.package), '--dest', str(self.dest), '--build', str(self.build), '--yes', *extra]

    def test_noninteractive_install(self):
        code = self.main(self.scripted('--vclibs', str(self.vclibs), '--store', 'both', '--launcher'))
        self.assertEqual(code, 0, self.stderr.getvalue())
        for name in ['Game.exe', 'appxportable.dll', 'DWrite.dll', 'msvcr120_app.dll', 'appx-portable.ini', LAUNCHER]:
            self.assertTrue((self.dest / name).is_file(), name)
        self.assertEqual(self.options(), (True, True))

    def test_vclibs_sources_are_tried_in_turn(self):
        wrong = vclibs_folder(self.base / 'v140', '140')
        code = self.main(self.scripted('--vclibs', str(wrong), '--vclibs', str(self.vclibs), '--no-launcher'))
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertTrue((self.dest / 'msvcr120_app.dll').is_file())
        self.assertFalse((self.dest / LAUNCHER).exists())

    def test_prompts_accept_quoted_paths(self):
        vclibs_appx = self.base / 'Microsoft.VCLibs.120.appx'
        with zipfile.ZipFile(vclibs_appx, 'w') as archive:
            for name in VCLIBS['120']:
                archive.writestr(name, pe(dll=True))
        # Package, destination, VCLibs, Store choice, launcher (Enter is yes).
        # --build is not a prompt, so it goes on the command line.
        answers = Answers(f'"{self.package}"', f' "{self.dest}" ', f'"{vclibs_appx}"', '3', '')
        code = self.main(['--build', str(self.build)], answers)
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertEqual(self.options(), (True, True))
        self.assertTrue((self.dest / LAUNCHER).is_file())
        self.assertIn(installer.VCLIBS_DOWNLOADS[('Microsoft.VCLibs.120.00', 'x86')], self.stdout.getvalue())

    def test_double_click_waits_before_closing(self):
        answers = Answers('', '')  # no package, then the closing prompt
        self.assertEqual(self.main([], answers), 1)
        self.assertEqual(len(answers.prompts), 2)
        self.assertIn('Press Enter', answers.prompts[-1])

    def test_store_prompt_choices(self):
        vclibs_folder(self.dest, '120')
        for answer, expected in [('1', (True, False)), ('2', (False, True)), ('3', (True, True)),
                                 ('', (True, True)), ('0', (False, False))]:
            with self.subTest(answer=answer):
                answers = Answers(answer)
                code = self.main([str(self.package), '--dest', str(self.dest), '--build', str(self.build),
                                  '--no-launcher'], answers)
                self.assertEqual(code, 0, self.stderr.getvalue())
                self.assertEqual(self.options(), expected)

    def test_invalid_store_answer_asks_again(self):
        vclibs_folder(self.dest, '120')
        answers = Answers('7', '2')
        self.main([str(self.package), '--dest', str(self.dest), '--build', str(self.build), '--no-launcher'], answers)
        self.assertEqual(len(answers.prompts), 2)
        self.assertEqual(self.options(), (False, True))

    def test_yes_without_vclibs_fails(self):
        self.assertEqual(self.main(self.scripted()), 1)
        message = self.stderr.getvalue()
        self.assertIn('Microsoft.VCLibs.120.00', message)
        self.assertIn(installer.VCLIBS_DOWNLOADS[('Microsoft.VCLibs.120.00', 'x86')].split()[0], message)
        self.assertNotIn('Traceback', message)

    def test_default_destination(self):
        code = self.main([str(self.package), '--build', str(self.build), '--vclibs', str(self.vclibs), '--yes'])
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertTrue((self.base / 'Example.Game' / 'Game.exe').is_file())

    def test_default_destination_beside_folder_source(self):
        source = self.base / 'Example.Game'
        installer.extract_game(self.package, source)
        code = self.main([str(source), '--build', str(self.build), '--vclibs', str(self.vclibs), '--yes'])
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertTrue((self.base / 'Example.Game-portable' / 'Game.exe').is_file())

    def test_known_game_is_patched(self):
        with mock.patch.object(installer, 'apply_known_patch', return_value='patched it') as apply:
            self.assertEqual(self.main(self.scripted('--vclibs', str(self.vclibs))), 0, self.stderr.getvalue())
        manifest, game, _, local = apply.call_args.args
        self.assertEqual(manifest.name, 'Example.Game')
        self.assertEqual(game, self.dest.resolve())
        self.assertEqual(local, installer.Path(installer.__file__).resolve().parent / 'patch')
        self.assertIn('patched it', self.stdout.getvalue())
        with mock.patch.object(installer, 'apply_known_patch') as apply:
            self.main(self.scripted('--vclibs', str(self.vclibs), '--no-patch'))
        apply.assert_not_called()

    def test_downloads_runtime_without_build(self):
        def download(tag, architecture, folder):
            self.assertEqual((tag, architecture), ('v9', 'x86'))
            return build_folder(folder)
        with mock.patch.object(installer, 'resolve_release', return_value='v9') as resolve, \
                mock.patch.object(installer, 'download_runtime', side_effect=download):
            code = self.main([str(self.package), '--dest', str(self.dest), '--vclibs', str(self.vclibs),
                              '--release', 'v9', '--yes'])
        self.assertEqual(code, 0, self.stderr.getvalue())
        resolve.assert_called_once_with('v9')
        self.assertTrue((self.dest / 'appxportable.dll').is_file())

    def test_managed_game_launches_host(self):
        package = appx(self.base / 'managed.appx', game_files(managed=True))
        code = self.main([str(package), '--dest', str(self.dest), '--build', str(self.build),
                          '--vclibs', str(self.vclibs), '--yes'])
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertIn('appx-host.exe', (self.dest / LAUNCHER).read_text())

    def test_desktop_copy_flag(self):
        package = appx(self.base / 'win8.appx', {**game_files(), 'Game.exe': pe(appcontainer=True)})
        code = self.main([str(package), '--dest', str(self.dest), '--build', str(self.build),
                          '--vclibs', str(self.vclibs), '--yes', '--desktop-copy'])
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertTrue((self.dest / 'Game.desktop.exe').is_file())
        self.assertIn('Game.desktop.exe', (self.dest / LAUNCHER).read_text())

    def test_error_is_one_line(self):
        corrupt = self.base / 'corrupt.appx'
        corrupt.write_bytes(b'not a zip')
        self.assertEqual(self.main([str(corrupt), '--build', str(self.build), '--yes']), 1)
        self.assertTrue(self.stderr.getvalue().startswith('Error: '))
        self.assertNotIn('Traceback', self.stderr.getvalue())

    def test_rerun_keeps_store_choice(self):
        self.main(self.scripted('--vclibs', str(self.vclibs), '--store', 'trial'))
        self.assertEqual(self.main(self.scripted()), 0, self.stderr.getvalue())
        self.assertEqual(self.options(), (True, False))


if __name__ == '__main__':
    unittest.main()
