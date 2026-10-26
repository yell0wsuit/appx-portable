import pathlib
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('powershell') or shutil.which('pwsh'),
                     'needs Windows PowerShell or PowerShell Core')
class GameExeTests(unittest.TestCase):
    def game_exe(self, game):
        shell = shutil.which('powershell') or shutil.which('pwsh')
        result = subprocess.run([shell, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
                                 '-File', str(ROOT / 'scripts' / 'game_exe.ps1'), '-GameDirectory', str(game)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.strip()

    def test_manifest_names_the_executable(self):
        with tempfile.TemporaryDirectory() as temp:
            game = pathlib.Path(temp)
            (game / 'AppxManifest.xml').write_text(
                '<Package xmlns="http://schemas.microsoft.com/appx/2010/manifest"><Applications>'
                '<Application Id="App" Executable="Example.exe" /></Applications></Package>')
            self.assertEqual(self.game_exe(game), 'Example.exe')

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
            self.assertEqual(self.game_exe(game), 'appx-host.exe')
            struct.pack_into('<I', data, 0x80 + 232, 0)
            (game / 'Game.exe').write_bytes(data)
            self.assertEqual(self.game_exe(game), 'Game.exe')

    def test_truncated_pe_uses_original(self):
        with tempfile.TemporaryDirectory() as temp:
            game = pathlib.Path(temp)
            (game / 'Game.exe').write_bytes(b'MZ')
            self.assertEqual(self.game_exe(game), 'Game.exe')


if __name__ == '__main__':
    unittest.main()
