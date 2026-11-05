import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("minionrush_patch", ROOT / "patch/minionrush/patch.py")
PATCHER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCHER)


class MinionRushPatchTests(unittest.TestCase):
    def setUp(self):
        # Exercise the real patch and backup operations without distributing game data.
        self.original = b"header" + bytes.fromhex("e8 ee c4 ff ff") + b"tail"
        self.patched = b"header" + b"\x90" * 5 + b"tail"
        constants = patch.multiple(
            PATCHER,
            ORIGINAL_SHA256=PATCHER.sha256(self.original),
            PATCHED_SHA256=PATCHER.sha256(self.patched),
            PATCHES=((6, bytes.fromhex("e8 ee c4 ff ff"), b"\x90" * 5),),
        )
        constants.start()
        self.addCleanup(constants.stop)
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.game = Path(folder.name)
        self.library = self.game / "IGPLib.dll"
        self.library.write_bytes(self.original)
        self.backup = self.library.with_name(self.library.name + PATCHER.BACKUP_SUFFIX)

    def test_patch_repeat_restore_and_reapply(self):
        self.assertEqual(PATCHER.main([str(self.game)]), 0)
        self.assertEqual(self.library.read_bytes(), self.patched)
        self.assertEqual(self.backup.read_bytes(), self.original)
        self.assertIn("Already patched", PATCHER.patch_file(self.library))
        self.assertEqual(PATCHER.main([str(self.game), "--restore"]), 0)
        self.assertEqual(self.library.read_bytes(), self.original)
        PATCHER.patch_file(self.library)
        self.assertEqual(self.library.read_bytes(), self.patched)

    def test_check_does_not_write(self):
        self.assertIn("original", PATCHER.patch_file(self.library, check=True))
        self.assertEqual(self.library.read_bytes(), self.original)
        self.assertFalse(self.backup.exists())

    def test_unsupported_library_is_untouched(self):
        self.library.write_bytes(self.original + b"modified")
        for options in ({}, {"restore": True}, {"check": True}):
            with self.assertRaisesRegex(ValueError, "Unsupported library"):
                PATCHER.patch_file(self.library, **options)
        self.assertFalse(self.backup.exists())
        self.assertEqual(self.library.read_bytes(), self.original + b"modified")

    def test_conflicting_or_corrupt_backup_is_preserved(self):
        self.backup.write_bytes(b"keep")
        with self.assertRaisesRegex(ValueError, "Existing backup differs"):
            PATCHER.patch_file(self.library)
        self.assertEqual(self.library.read_bytes(), self.original)
        self.library.write_bytes(self.patched)
        with self.assertRaisesRegex(ValueError, "Backup hash mismatch"):
            PATCHER.patch_file(self.library, restore=True)
        self.assertEqual(self.library.read_bytes(), self.patched)
        self.assertEqual(self.backup.read_bytes(), b"keep")

    def test_failed_replacement_retains_original_and_backup(self):
        with patch.object(PATCHER.os, "replace", side_effect=PermissionError("locked")):
            with self.assertRaises(PermissionError):
                PATCHER.patch_file(self.library)
        self.assertEqual(self.library.read_bytes(), self.original)
        self.assertEqual(self.backup.read_bytes(), self.original)
        self.assertFalse(list(self.game.glob("*.tmp")))

    def test_instruction_and_output_hash_checks(self):
        with patch.object(PATCHER, "PATCHES", ((6, b"wrong", b"\x90" * 5),)):
            with self.assertRaisesRegex(ValueError, "Unexpected instructions"):
                PATCHER.patch_file(self.library)
        with patch.object(PATCHER, "PATCHED_SHA256", "incorrect"):
            with self.assertRaisesRegex(ValueError, "output hash"):
                PATCHER.patch_file(self.library)
        self.assertFalse(self.backup.exists())
        self.assertEqual(self.library.read_bytes(), self.original)


if __name__ == "__main__":
    unittest.main()
