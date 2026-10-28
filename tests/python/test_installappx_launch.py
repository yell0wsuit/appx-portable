import os
import pathlib
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from installappx_fixtures import load_installer, pe

installer = load_installer()
FLAGS = 0xDE  # DllCharacteristics in the fixture's PE32 header
# The .sh launcher is for Linux. On Windows, bash on PATH is WSL's or MSYS's,
# neither of which takes a Windows path as is.
BASH = shutil.which("bash") if os.name != "nt" else None


class DesktopCopyTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.game = pathlib.Path(folder.name)

    def test_copy_changes_only_appcontainer_bit(self):
        original = pe(appcontainer=True)
        (self.game / "Game.exe").write_bytes(original)
        self.assertEqual(
            installer.make_desktop_copy(self.game, "Game.exe"), "Game.desktop.exe"
        )
        self.assertEqual((self.game / "Game.exe").read_bytes(), original)
        expected = bytearray(original)
        expected[FLAGS + 1] &= 0xEF
        self.assertEqual((self.game / "Game.desktop.exe").read_bytes(), bytes(expected))

    def test_subfolder_executable(self):
        (self.game / "Sub").mkdir()
        (self.game / "Sub" / "Game_Win8.exe").write_bytes(pe(appcontainer=True))
        self.assertEqual(
            installer.make_desktop_copy(self.game, "Sub/Game_Win8.exe"),
            "Sub/Game_Win8.desktop.exe",
        )
        self.assertTrue((self.game / "Sub" / "Game_Win8.desktop.exe").is_file())

    def test_replaces_an_earlier_copy(self):
        (self.game / "Game.exe").write_bytes(pe(appcontainer=True))
        (self.game / "Game.desktop.exe").write_bytes(b"old")
        installer.make_desktop_copy(self.game, "Game.exe")
        self.assertEqual(
            (self.game / "Game.desktop.exe").read_bytes(), pe(appcontainer=False)
        )

    def test_already_clear_needs_no_copy(self):
        (self.game / "Game.exe").write_bytes(pe(appcontainer=False))
        self.assertEqual(installer.make_desktop_copy(self.game, "Game.exe"), "Game.exe")
        self.assertFalse((self.game / "Game.desktop.exe").exists())

    def test_x64_is_left_alone(self):
        (self.game / "Game.exe").write_bytes(pe(0x8664, appcontainer=True))
        self.assertEqual(installer.make_desktop_copy(self.game, "Game.exe"), "Game.exe")
        self.assertFalse((self.game / "Game.desktop.exe").exists())

    def test_rejects_invalid_and_truncated_headers(self):
        cases = [b"not a PE", pe(appcontainer=True)[:FLAGS]]
        for offset, value in [
            (0, 0),
            (0x80, 0),
            (0x94, 2),
            (0x96, 0x2102),
            (0x3C, 0xFFFF),
        ]:
            data = bytearray(pe(appcontainer=True))
            struct.pack_into("<H", data, offset, value)
            cases.append(bytes(data))
        for data in cases:
            with self.subTest(data=data[:4]):
                (self.game / "Game.exe").write_bytes(data)
                with self.assertRaises(installer.InstallError):
                    installer.make_desktop_copy(self.game, "Game.exe")
                self.assertEqual((self.game / "Game.exe").read_bytes(), data)
                self.assertFalse((self.game / "Game.desktop.exe").exists())

    def test_vclibs_cleared_in_place(self):
        (self.game / "Game.exe").write_bytes(pe(appcontainer=True))
        for name in ["msvcr110.dll", "msvcp110.dll", "vccorlib110.dll"]:
            (self.game / name).write_bytes(pe(dll=True, appcontainer=True))
        (self.game / "appxportable.dll").write_bytes(pe(dll=True, appcontainer=True))
        installer.make_desktop_copy(self.game, "Game.exe")
        for name in ["msvcr110.dll", "msvcp110.dll", "vccorlib110.dll"]:
            self.assertEqual(
                (self.game / name).read_bytes(), pe(dll=True, appcontainer=False), name
            )
        self.assertEqual(
            (self.game / "appxportable.dll").read_bytes(),
            pe(dll=True, appcontainer=True),
        )

    def test_needs_desktop_copy(self):
        for platform, version, expected in [
            ("win32", (6, 2), True),
            ("win32", (6, 3), True),
            ("win32", (10, 0), False),
            ("darwin", (6, 3), False),
        ]:
            with self.subTest(platform=platform, version=version), mock.patch.object(
                sys, "platform", platform
            ), mock.patch.object(
                sys, "getwindowsversion", create=True, return_value=version + (0, 0, "")
            ):
                self.assertEqual(installer.needs_desktop_copy(), expected)


class LauncherTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.game = pathlib.Path(folder.name)

    def test_launch_executable(self):
        self.assertEqual(
            installer.launch_executable("Sub/Game.exe", False), "Sub/Game.exe"
        )
        self.assertEqual(
            installer.launch_executable("Sub/Game.exe", True), "appx-host.exe"
        )

    def test_bat_launcher(self):
        path = installer.write_launcher(self.game, "Sub/Game.exe", windows=True)
        self.assertEqual(path.name, "00_click2startme.bat")
        data = path.read_bytes()
        self.assertNotIn(b"\n", data.replace(b"\r\n", b""))
        self.assertIn(b'start "" "%~dp0Sub\\Game.exe" %*', data)
        self.assertIn(b'cd /d "%~dp0"', data)

    def test_bat_escapes_percent(self):
        data = installer.write_launcher(
            self.game, "100%Game.exe", windows=True
        ).read_bytes()
        self.assertIn(b'"%~dp0100%%Game.exe"', data)

    def test_sh_launcher(self):
        path = installer.write_launcher(self.game, "Sub/My Game.exe", windows=False)
        self.assertEqual(path.name, "00_click2startme.sh")
        text = path.read_bytes().decode()
        self.assertNotIn("\r", text)
        self.assertTrue(os.access(path, os.X_OK))
        self.assertIn(f'"$wine" {shlex.quote("Sub/My Game.exe")} "${{flags[@]}}"', text)
        self.assertIn("CreateDXGIFactory1 begin", text)
        self.assertIn("--log", text)
        self.assertIn("WINEDLLOVERRIDES", text)
        if BASH:
            result = subprocess.run(
                [BASH, "-n", str(path)], capture_output=True, text=True
            )
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_sh_launcher_runs_wine_from_the_game_folder(self):
        if not BASH:
            self.skipTest("needs bash on a POSIX host")
        fake = self.game / "fake-wine"
        fake.write_text(
            '#!/bin/sh\n[ "$1" = reg ] && exit 0\necho "$PWD|$*" > wine-call.txt\n'
        )
        fake.chmod(0o755)
        path = installer.write_launcher(self.game, "Game.exe", windows=False)
        result = subprocess.run(
            [BASH, str(path), "--unlock-trial"],
            capture_output=True,
            text=True,
            cwd="/",
            env={**os.environ, "WINE": str(fake)},
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        folder, arguments = (self.game / "wine-call.txt").read_text().strip().split("|")
        self.assertEqual(pathlib.Path(folder).resolve(), self.game.resolve())
        self.assertEqual(arguments, "Game.exe --log --unlock-trial")

    def test_sh_launcher_loads_the_staged_graphics_stack(self):
        if not BASH:
            self.skipTest("needs bash on a POSIX host")
        fake = self.game / "fake-wine"
        fake.write_text(
            '#!/bin/sh\n[ "$1" = reg ] && exit 0\necho "$WINEDLLOVERRIDES" > overrides.txt\n'
        )
        fake.chmod(0o755)
        path = installer.write_launcher(self.game, "Game.exe", windows=False)

        def overrides():
            result = subprocess.run(
                [BASH, str(path)],
                capture_output=True,
                text=True,
                env={**os.environ, "WINE": str(fake), "WINEDLLOVERRIDES": "mine=b"},
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            return (self.game / "overrides.txt").read_text().strip()

        self.assertNotIn("wined3d", overrides())
        for name in installer.WINE_GRAPHICS:
            (self.game / name).write_bytes(b"")
        text = overrides()
        self.assertIn(";d3d11,dxgi,wined3d=n;", text)
        self.assertTrue(text.endswith(";mine=b"), text)


if __name__ == "__main__":
    unittest.main()
