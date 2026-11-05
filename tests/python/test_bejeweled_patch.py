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


class BejeweledWpfPatchTests(unittest.TestCase):
    def test_wpf_style_references_are_opt_in_repeatable_and_restorable(self):
        original = b'<ResourceDictionary><Style x:Key="ButtonBackgroundBrush" TargetType="Button"/><Setter Property="Background" Value="{StaticResource ButtonBackgroundBrush}"/></ResourceDictionary>'
        expected = original.replace(b"{StaticResource ButtonBackgroundBrush}", b"{StaticResource ButtonBackgroundThemeBrush}")
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder)
            (game / "Common").mkdir()
            styles = game / "Common/BejeweledStyles.xaml"
            styles.write_bytes(original)
            app = game / "App.xaml"
            app.write_bytes(b"keep default app")
            with patch.object(PATCHER, "WPF_ORIGINAL_SHA256", PATCHER.sha256(original)), patch.object(PATCHER, "WPF_PATCHED_SHA256", PATCHER.sha256(expected)):
                self.assertEqual(PATCHER.main([str(game), "--wpf", "--check"]), 0)
                self.assertEqual(styles.read_bytes(), original)
                self.assertEqual(PATCHER.main([str(game), "--wpf"]), 0)
                self.assertEqual(styles.read_bytes(), expected)
                self.assertEqual(PATCHER.main([str(game), "--wpf"]), 0)
                self.assertEqual(PATCHER.main([str(game), "--wpf", "--restore"]), 0)
                self.assertEqual(styles.read_bytes(), original)
                self.assertEqual(app.read_bytes(), b"keep default app")
                backup = styles.with_name(styles.name + ".wpf-original")
                self.assertEqual(backup.read_bytes(), original)
                styles.write_bytes(original + b"modified")
                self.assertEqual(PATCHER.main([str(game), "--wpf"]), 1)
                self.assertEqual(styles.read_bytes(), original + b"modified")


class BejeweledWineTexturePatchTests(unittest.TestCase):
    def setUp(self):
        # Instruction-sized synthetic data at the shipped DLL's file offsets;
        # no game bytes outside those two instructions are included.
        original = bytearray(0xEAA00)
        original[0xEA96A:0xEA971] = bytes.fromhex("c745f400000200")
        original[0xEA9CE:0xEA9D0] = bytes.fromhex("6a01")
        self.original = bytes(original)
        original[0xEA96F] = 3
        original[0xEA9CF] = 3
        self.patched = bytes(original)
        for name, value in {
            "WINE_TEXTURES_ORIGINAL_SHA256": PATCHER.sha256(self.original),
            "WINE_TEXTURES_PATCHED_SHA256": PATCHER.sha256(self.patched),
        }.items():
            replacement = patch.object(PATCHER, name, value, create=True)
            replacement.start()
            self.addCleanup(replacement.stop)
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.game = Path(folder.name)
        self.asset = self.game / "Bej3Win8Component.dll"
        self.asset.write_bytes(self.original)
        self.backup = self.asset.with_name(self.asset.name + ".wine-textures-original")
        (self.game / "App.xaml").write_bytes(b"preserve theme")
        (self.game / "Common").mkdir()
        (self.game / "Common/BejeweledStyles.xaml").write_bytes(b"preserve styles")

    def test_opt_in_changes_only_two_bytes_and_supports_check_repeat_restore(self):
        self.assertEqual(PATCHER.main([str(self.game), "--wine-textures", "--check"]), 0)
        self.assertEqual(self.asset.read_bytes(), self.original)
        self.assertFalse(self.backup.exists())
        self.assertEqual(PATCHER.main([str(self.game), "--wine-textures"]), 0)
        actual = self.asset.read_bytes()
        self.assertEqual(actual, self.patched)
        self.assertEqual([i for i, pair in enumerate(zip(self.original, actual)) if pair[0] != pair[1]],
                         [0xEA96F, 0xEA9CF])
        self.assertEqual(self.backup.read_bytes(), self.original)
        self.assertEqual(PATCHER.main([str(self.game), "--wine-textures"]), 0)
        self.assertEqual(PATCHER.main([str(self.game), "--wine-textures", "--restore"]), 0)
        self.assertEqual(self.asset.read_bytes(), self.original)
        self.assertEqual(self.backup.read_bytes(), self.original)
        self.assertEqual((self.game / "App.xaml").read_bytes(), b"preserve theme")
        self.assertEqual((self.game / "Common/BejeweledStyles.xaml").read_bytes(), b"preserve styles")

    def test_unknown_binary_and_conflicting_backup_are_preserved(self):
        changed = self.original + b"modified"
        self.asset.write_bytes(changed)
        self.assertEqual(PATCHER.main([str(self.game), "--wine-textures"]), 1)
        self.assertEqual(self.asset.read_bytes(), changed)
        self.assertFalse(self.backup.exists())
        self.asset.write_bytes(self.original)
        self.backup.write_bytes(b"keep")
        self.assertEqual(PATCHER.main([str(self.game), "--wine-textures"]), 1)
        self.assertEqual(self.asset.read_bytes(), self.original)
        self.assertEqual(self.backup.read_bytes(), b"keep")

    def test_restore_requires_verified_backup(self):
        self.asset.write_bytes(self.patched)
        self.assertEqual(PATCHER.main([str(self.game), "--wine-textures", "--restore"]), 1)
        self.backup.write_bytes(b"tampered")
        self.assertEqual(PATCHER.main([str(self.game), "--wine-textures", "--restore"]), 1)
        self.assertEqual(self.asset.read_bytes(), self.patched)

    def test_wrong_instructions_are_rejected_before_backup(self):
        changed = bytearray(self.original)
        changed[0xEA96A] = 0
        self.asset.write_bytes(changed)
        with patch.object(PATCHER, "WINE_TEXTURES_ORIGINAL_SHA256", PATCHER.sha256(changed)):
            with self.assertRaisesRegex(ValueError, "instruction"):
                PATCHER.patch_file(self.asset, wine_textures=True)
        self.assertFalse(self.backup.exists())
        self.assertEqual(self.asset.read_bytes(), changed)

    def test_output_hash_mismatch_is_rejected_before_backup(self):
        with patch.object(PATCHER, "WINE_TEXTURES_PATCHED_SHA256", "0" * 64):
            with self.assertRaisesRegex(ValueError, "output hash"):
                PATCHER.patch_file(self.asset, wine_textures=True)
        self.assertFalse(self.backup.exists())
        self.assertEqual(self.asset.read_bytes(), self.original)

    def test_failed_replace_preserves_binary_and_backup(self):
        with patch.object(PATCHER.os, "replace", side_effect=PermissionError("locked")):
            self.assertEqual(PATCHER.main([str(self.game), "--wine-textures"]), 1)
        self.assertEqual(self.asset.read_bytes(), self.original)
        self.assertEqual(self.backup.read_bytes(), self.original)
        self.assertFalse(list(self.game.glob("*.tmp")))

    def test_modes_are_mutually_exclusive_and_external_target_is_rejected(self):
        with self.assertRaises(SystemExit) as error:
            PATCHER.main([str(self.game), "--wpf", "--wine-textures"])
        self.assertEqual(error.exception.code, 2)
        with tempfile.TemporaryDirectory() as folder:
            external = Path(folder) / self.asset.name
            external.write_bytes(self.original)
            self.asset.unlink()
            self.asset.symlink_to(external)
            self.assertEqual(PATCHER.main([str(self.game), "--wine-textures"]), 1)
            self.assertEqual(external.read_bytes(), self.original)
            self.assertFalse(list(Path(folder).glob("*.wine-textures-original")))


if __name__ == "__main__":
    unittest.main()
