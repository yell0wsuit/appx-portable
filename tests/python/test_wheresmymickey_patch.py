import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("mickey_patch", ROOT / "patch/wheresmymickey/patch.py")
PATCHER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCHER)


class MickeyPatchTests(unittest.TestCase):
    def setUp(self):
        # Synthetic input exercises the actual patch/backup code without game data.
        self.original = b"header" + bytes.fromhex("55 8b ec") + b"body" + bytes.fromhex("55 8b ec")
        self.patched = b"header" + bytes.fromhex("31 c0 c3") + b"body" + bytes.fromhex("31 c0 c3")
        for name, value in {
            "ORIGINAL_SHA256": PATCHER.sha256(self.original),
            "PATCHED_SHA256": PATCHER.sha256(self.patched),
            "PATCHES": ((6, bytes.fromhex("55 8b ec"), bytes.fromhex("31 c0 c3")),
                        (13, bytes.fromhex("55 8b ec"), bytes.fromhex("31 c0 c3"))),
        }.items():
            replacement = patch.object(PATCHER, name, value)
            replacement.start()
            self.addCleanup(replacement.stop)
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.exe = Path(folder.name) / "Game.exe"
        self.exe.write_bytes(self.original)
        self.backup = self.exe.with_name(self.exe.name + PATCHER.BACKUP_SUFFIX)

    def test_patch_backup_repeat_restore_and_reapply(self):
        PATCHER.patch_executable(self.exe)
        self.assertEqual(self.exe.read_bytes(), self.patched)
        self.assertEqual(self.backup.read_bytes(), self.original)
        self.assertIn("Already patched", PATCHER.patch_executable(self.exe))
        PATCHER.patch_executable(self.exe, restore=True)
        self.assertEqual(self.exe.read_bytes(), self.original)
        self.assertEqual(self.backup.read_bytes(), self.original)
        self.assertIn("Already original", PATCHER.patch_executable(self.exe, restore=True))
        PATCHER.patch_executable(self.exe)
        self.assertEqual(self.exe.read_bytes(), self.patched)

    def test_check_does_not_write(self):
        self.assertIn("original", PATCHER.patch_executable(self.exe, check=True))
        self.assertEqual(self.exe.read_bytes(), self.original)
        self.assertFalse(self.backup.exists())

    def test_unsupported_or_modified_executable_is_untouched(self):
        self.exe.write_bytes(self.original + b"modified")
        for options in ({}, {"restore": True}, {"check": True}):
            with self.assertRaisesRegex(ValueError, "Unsupported executable"):
                PATCHER.patch_executable(self.exe, **options)
        self.assertEqual(self.exe.read_bytes(), self.original + b"modified")
        self.assertFalse(self.backup.exists())

    def test_preserves_conflicting_backup(self):
        self.backup.write_bytes(b"keep")
        with self.assertRaisesRegex(ValueError, "Existing backup differs"):
            PATCHER.patch_executable(self.exe)
        self.assertEqual(self.exe.read_bytes(), self.original)
        self.assertEqual(self.backup.read_bytes(), b"keep")

    def test_restore_refuses_missing_or_corrupt_backup(self):
        self.exe.write_bytes(self.patched)
        with self.assertRaises(FileNotFoundError):
            PATCHER.patch_executable(self.exe, restore=True)
        self.backup.write_bytes(b"wrong")
        with self.assertRaisesRegex(ValueError, "Backup hash mismatch"):
            PATCHER.patch_executable(self.exe, restore=True)
        self.assertEqual(self.exe.read_bytes(), self.patched)

    def test_failed_replace_preserves_executable_and_backup(self):
        with patch.object(PATCHER.os, "replace", side_effect=PermissionError("locked")):
            with self.assertRaises(PermissionError):
                PATCHER.patch_executable(self.exe)
        self.assertEqual(self.exe.read_bytes(), self.original)
        self.assertEqual(self.backup.read_bytes(), self.original)
        self.assertFalse(list(self.exe.parent.glob("*.tmp")))

    def test_instruction_and_output_hash_checks(self):
        with patch.object(PATCHER, "PATCHES", ((6, b"bad", b"new"),)):
            with self.assertRaisesRegex(ValueError, "Unexpected instructions"):
                PATCHER.patch_executable(self.exe)
        with patch.object(PATCHER, "PATCHED_SHA256", "incorrect"):
            with self.assertRaisesRegex(ValueError, "output hash"):
                PATCHER.patch_executable(self.exe)
        self.assertFalse(self.backup.exists())
        self.assertEqual(self.exe.read_bytes(), self.original)


if __name__ == "__main__":
    unittest.main()
