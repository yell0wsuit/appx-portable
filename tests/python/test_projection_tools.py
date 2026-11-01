import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

path = Path(__file__).resolve().parents[2] / "scripts/package-winrt-tools.py"
spec = importlib.util.spec_from_file_location("projection_tools", path)
tools = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tools)


class ProjectionToolsTests(unittest.TestCase):
    def test_source_bundle_is_complete_verified_and_deterministic(self):
        with tempfile.TemporaryDirectory() as temporary:
            first = tools.package(Path(temporary) / "one.zip")
            second = tools.package(Path(temporary) / "two.zip")
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(first) as archive:
                manifest = json.loads(archive.read("tools.json"))
                self.assertEqual(manifest["schema"], 2)
                self.assertEqual(manifest["interop"], ["interop.cs"])
                self.assertEqual(set(archive.namelist()), set(manifest["generator"] + manifest["runtime"] + manifest["interop"] + ["tools.json", "LICENSE"]))
                self.assertTrue(all(not name.endswith((".dll", ".exe", ".winmd")) for name in archive.namelist()))
                for name, digest in manifest["sha256"].items():
                    self.assertEqual(hashlib.sha256(archive.read(name)).hexdigest(), digest)
