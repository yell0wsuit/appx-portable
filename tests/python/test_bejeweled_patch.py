import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("bejeweled_patch", ROOT / "patch/bejeweled/patch.py")
PATCHER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCHER)


class BejeweledPatchTests(unittest.TestCase):
    def setUp(self):
        self.original = b'<Application><Application.Resources/></Application>'
        self.patched = b'<Application RequestedTheme="Dark"><Application.Resources/></Application>'
        for name, value in {
            "ORIGINAL_SHA256": PATCHER.sha256(self.original),
            "PATCHED_SHA256": PATCHER.sha256(self.patched),
        }.items():
            replacement = patch.object(PATCHER, name, value)
            replacement.start()
            self.addCleanup(replacement.stop)
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.asset = Path(folder.name) / "App.xaml"
        self.asset.write_bytes(self.original)
        self.backup = self.asset.with_name(self.asset.name + PATCHER.BACKUP_SUFFIX)

    def test_patch_is_repeatable_and_restorable(self):
        self.assertIn("original", PATCHER.patch_file(self.asset, check=True))
        self.assertFalse(self.backup.exists())
        PATCHER.patch_file(self.asset)
        self.assertEqual(self.asset.read_bytes(), self.patched)
        self.assertEqual(self.backup.read_bytes(), self.original)
        self.assertIn("Already patched", PATCHER.patch_file(self.asset))
        PATCHER.patch_file(self.asset, restore=True)
        self.assertEqual(self.asset.read_bytes(), self.original)
        self.assertEqual(self.backup.read_bytes(), self.original)
        PATCHER.patch_file(self.asset)
        self.assertEqual(self.asset.read_bytes(), self.patched)

    def test_refuses_unknown_asset_and_conflicting_backup(self):
        self.asset.write_bytes(self.original + b"modified")
        with self.assertRaisesRegex(ValueError, "Unsupported"):
            PATCHER.patch_file(self.asset)
        self.assertFalse(self.backup.exists())
        self.assertEqual(self.asset.read_bytes(), self.original + b"modified")
        self.asset.write_bytes(self.original)
        self.backup.write_bytes(b"keep")
        with self.assertRaisesRegex(ValueError, "Existing backup differs"):
            PATCHER.patch_file(self.asset)
        self.assertEqual(self.asset.read_bytes(), self.original)
        self.assertEqual(self.backup.read_bytes(), b"keep")

    def test_failed_replace_preserves_asset_and_backup(self):
        with patch.object(PATCHER.os, "replace", side_effect=PermissionError("locked")):
            with self.assertRaises(PermissionError):
                PATCHER.patch_file(self.asset)
        self.assertEqual(self.asset.read_bytes(), self.original)
        self.assertEqual(self.backup.read_bytes(), self.original)
        self.assertFalse(list(self.asset.parent.glob("*.tmp")))

    def test_restore_requires_verified_backup(self):
        self.asset.write_bytes(self.patched)
        with self.assertRaises(FileNotFoundError):
            PATCHER.patch_file(self.asset, restore=True)
        self.backup.write_bytes(b"wrong")
        with self.assertRaisesRegex(ValueError, "Backup hash mismatch"):
            PATCHER.patch_file(self.asset, restore=True)
        self.assertEqual(self.asset.read_bytes(), self.patched)


if __name__ == "__main__":
    unittest.main()
