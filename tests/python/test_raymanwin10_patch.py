import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("rayman_patch", ROOT / "patch/raymanwin10/patch.py")
PATCHER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCHER)


def manifest(name="Ubisoft.RaymanFiestaRunWindows10Edition", version="1.0.23.0"):
    return f'''<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10">
  <Identity Name="{name}" Publisher="CN=Example" Version="{version}" ProcessorArchitecture="x64" />
</Package>'''.encode()


class RaymanPatchTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.game = Path(folder.name)
        (self.game / "AppxManifest.xml").write_bytes(manifest())
        self.asset = self.game / PATCHER.ASSET

    def test_patch_is_repeatable_and_restorable(self):
        self.assertIn("original", PATCHER.patch_game(self.game, check=True))
        PATCHER.patch_game(self.game)
        self.assertEqual(self.asset.read_bytes(), PATCHER.SOURCE.read_bytes())
        self.assertIn("patched", PATCHER.patch_game(self.game, check=True))
        self.assertIn("Already patched", PATCHER.patch_game(self.game))
        PATCHER.patch_game(self.game, restore=True)
        self.assertFalse(self.asset.exists())
        self.assertIn("Already original", PATCHER.patch_game(self.game, restore=True))

    def test_refuses_other_packages(self):
        for name, version in [("Example.Game", "1.0.23.0"),
                              ("Ubisoft.RaymanFiestaRunWindows10Edition", "1.0.24.0")]:
            (self.game / "AppxManifest.xml").write_bytes(manifest(name, version))
            with self.assertRaisesRegex(ValueError, "supports"):
                PATCHER.patch_game(self.game)
            self.assertFalse(self.asset.exists())

    def test_never_overwrites_or_removes_a_different_file(self):
        self.asset.write_bytes(b"keep")
        for options in [{}, {"restore": True}]:
            with self.assertRaisesRegex(ValueError, "Unexpected"):
                PATCHER.patch_game(self.game, **options)
            self.assertEqual(self.asset.read_bytes(), b"keep")

    def test_failed_replace_leaves_no_file(self):
        with patch.object(PATCHER.os, "replace", side_effect=PermissionError("locked")):
            with self.assertRaises(PermissionError):
                PATCHER.patch_game(self.game)
        self.assertFalse(self.asset.exists())
        self.assertFalse(list(self.game.glob("*.tmp")))


if __name__ == "__main__":
    unittest.main()
