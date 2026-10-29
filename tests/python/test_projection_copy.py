import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "scripts/probe-winrt-projection.py"
spec = importlib.util.spec_from_file_location("projection_probe", SCRIPT)
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class ProjectionCopyTests(unittest.TestCase):
    def test_replacing_copied_dll_links_preserves_original_targets(self):
        for name in ("Microsoft.Xbox.dll", "Windows.dll", "appxportable.dll"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                source = root / "source"
                source.mkdir()
                original = root / "original.dll"
                original.write_bytes(b"original runtime")
                (source / name).symlink_to(original)
                owned = root / "owned"
                shutil.copytree(source, owned, symlinks=True)
                replacement = root / "replacement.dll"
                replacement.write_bytes(b"generated projection")
                probe.copy_owned_file(replacement, owned / name)
                self.assertEqual(b"original runtime", original.read_bytes())
                self.assertTrue((source / name).is_symlink())
                self.assertFalse((owned / name).is_symlink())
                self.assertEqual(b"generated projection", (owned / name).read_bytes())
