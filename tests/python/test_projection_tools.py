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


PREBUILT = ["AppxPortable.WinRT.Interop.dll", "System.Runtime.WindowsRuntime.dll", "Windows.dll"]


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


class ToolsArchiveTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.prebuilt = self.root / "prebuilt"
        self.prebuilt.mkdir()
        for name in PREBUILT:
            (self.prebuilt / name).write_bytes(name.encode())

    def test_prebuilt_assemblies_ship_as_schema_three(self):
        archive = tools.package(self.root / "tools.zip", self.prebuilt)
        with zipfile.ZipFile(archive) as bundle:
            manifest = json.loads(bundle.read("tools.json"))
            self.assertEqual(manifest["schema"], 3)
            self.assertEqual(manifest["prebuilt"], PREBUILT)
            for name in PREBUILT:
                self.assertEqual(manifest["sha256"][name], hashlib.sha256(bundle.read(name)).hexdigest())

    def test_without_prebuilt_assemblies_the_archive_stays_schema_two(self):
        with zipfile.ZipFile(tools.package(self.root / "tools.zip")) as bundle:
            manifest = json.loads(bundle.read("tools.json"))
        self.assertEqual(manifest["schema"], 2)
        self.assertNotIn("prebuilt", manifest)

    def test_missing_prebuilt_assembly_is_refused(self):
        (self.prebuilt / "Windows.dll").unlink()
        with self.assertRaisesRegex(ValueError, "Windows.dll"):
            tools.package(self.root / "tools.zip", self.prebuilt)
