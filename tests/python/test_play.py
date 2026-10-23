import importlib.util
import pathlib
import sys
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('play', ROOT / 'scripts' / 'play.py')
play = importlib.util.module_from_spec(spec)
spec.loader.exec_module(play)


class PlayTests(unittest.TestCase):
    def test_windows_forwards_trial_override(self):
        with mock.patch.object(sys, 'argv', ['play.py', '--game', str(ROOT), '--unlock-trial', '--log']), \
                mock.patch.object(sys, 'platform', 'win32'), \
                mock.patch.object(pathlib.Path, 'is_file', return_value=True), \
                mock.patch.object(play, 'game_executable', return_value='Example.exe'), \
                mock.patch.object(play.subprocess, 'Popen') as launch:
            self.assertEqual(play.main(), 0)
            self.assertEqual(launch.call_args.args[0], [str(ROOT / 'Example.exe'), '--log', '--unlock-trial'])

    def test_wine_forwards_trial_override(self):
        with mock.patch.object(sys, 'argv', ['play.py', '--game', str(ROOT), '--unlock-trial']), \
                mock.patch.object(sys, 'platform', 'darwin'), \
                mock.patch.object(play.subprocess, 'call', return_value=0) as launch:
            self.assertEqual(play.main(), 0)
            self.assertEqual(launch.call_args.args[0],
                             ['bash', str(ROOT / 'scripts' / 'run.sh'), str(ROOT), '--unlock-trial'])
