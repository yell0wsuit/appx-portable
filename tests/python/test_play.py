import importlib.util
import pathlib
import struct
import tempfile
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
                mock.patch.object(play, 'launch_executable', return_value='Example.exe'), \
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


class ManagedLaunchTests(unittest.TestCase):
    def test_pe_clr_header_selects_host(self):
        with tempfile.TemporaryDirectory() as temp:
            game = pathlib.Path(temp)
            data = bytearray(512)
            data[:2] = b'MZ'
            struct.pack_into('<I', data, 0x3c, 0x80)
            data[0x80:0x84] = b'PE\0\0'
            struct.pack_into('<H', data, 0x80 + 24, 0x10b)
            struct.pack_into('<I', data, 0x80 + 232, 0x2000)
            (game / 'Game.exe').write_bytes(data)
            self.assertEqual(play.launch_executable(game), 'appx-host.exe')
            struct.pack_into('<I', data, 0x80 + 232, 0)
            (game / 'Game.exe').write_bytes(data)
            self.assertEqual(play.launch_executable(game), 'Game.exe')

    def test_truncated_pe_uses_original(self):
        with tempfile.TemporaryDirectory() as temp:
            game = pathlib.Path(temp)
            (game / 'Game.exe').write_bytes(b'MZ')
            self.assertEqual(play.launch_executable(game), 'Game.exe')
