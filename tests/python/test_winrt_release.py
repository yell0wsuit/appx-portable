"""Release WinRT assemblies: build commands and the tools archive that ships them."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


release = load("build_winrt_release", "build-winrt-release.py")


class ReleaseBuildTests(unittest.TestCase):
    def steps(self, toolchain):
        paths = Path("/metadata"), Path("/work/Mono.Cecil.dll"), Path("/out"), Path("/work")
        return dict(release.commands(toolchain, *paths))

    def test_native_build_uses_dotnet_framework_csc(self):
        steps = self.steps(release.Toolchain(framework=Path("/framework")))
        self.assertEqual(steps["compile-generator"][0], str(Path("/framework/csc.exe")))
        self.assertIn("/langversion:5", steps["compile-generator"])
        self.assertEqual(steps["generate-bootstrap"][0], str(Path("/work/generator.exe")))

    def test_surface_is_completed_and_tasks_rebound_to_it(self):
        steps = self.steps(release.Toolchain(framework=Path("/framework")))
        self.assertEqual(steps["complete-surface"][1], "--all")
        self.assertIn("--interop", steps["generate-bootstrap"])
        self.assertEqual(steps["rebind-tasks"][1:3], ["--rebind", str(Path("/out/Windows.dll"))])
        self.assertTrue(steps["rebind-tasks"][-1].endswith("System.Runtime.WindowsRuntime.dll"))
        self.assertIn("/define:APPX_PORTABLE_INTEROP", steps["compile-bootstrap"])

    def test_release_artifact_is_verified_by_the_strict_load_test(self):
        names = [name for name, _ in release.commands(release.Toolchain(framework=Path("/framework")), Path("/metadata"),
                                                      Path("/work/Mono.Cecil.dll"), Path("/out"), Path("/work"))]
        self.assertEqual(names[-2:], ["compile-surface-check", "verify-surface"])
        steps = self.steps(release.Toolchain(framework=Path("/framework")))
        self.assertTrue(steps["compile-surface-check"][-1].endswith("surface_load.cs"))
        self.assertEqual(steps["verify-surface"], [str(Path("/work/surface-load.exe")), str(Path("/out/Windows.dll"))])

    def test_no_contract_metadata_reaches_the_output(self):
        steps = self.steps(release.Toolchain(framework=Path("/framework")))
        outputs = [arg for command in steps.values() for arg in command if arg.startswith(("/out:", str(Path("/out"))))]
        self.assertFalse([arg for arg in outputs if arg.endswith(".winmd")])


if __name__ == "__main__":
    unittest.main()
