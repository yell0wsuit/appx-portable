import contextlib
import hashlib
import io
import os
import pathlib
import sys
import tempfile
import unittest
import zipfile
from unittest import mock

from installappx_fixtures import (
    VCLIBS,
    appx,
    build_folder,
    game_files,
    load_installer,
    pe,
    vclibs_folder,
)

installer = load_installer()
LAUNCHER = "00_click2startme.bat" if sys.platform == "win32" else "00_click2startme.sh"


class Answers:
    def __init__(self, *answers):
        self.answers = list(answers)
        self.prompts = []

    def __call__(self, prompt):
        self.prompts.append(prompt)
        if not self.answers:
            raise AssertionError(f"Unexpected prompt: {prompt}")
        return self.answers.pop(0)


class FakeWeb:
    """fetch for the tests: serves pages, and anything else fails like a lost connection."""

    def __init__(self):
        self.pages = {}
        self.requested = []

    def __call__(self, url, instead="pass --build with a local build folder"):
        self.requested.append(url)
        if url not in self.pages:
            raise installer.InstallError(
                f"Could not download {url}: offline. Check the connection, or {instead}."
            )
        return self.pages[url]


def vclibs_appx_bytes(version="120", machine=0x14C):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        for name in VCLIBS[version]:
            archive.writestr(name, pe(machine, dll=True))
    return buffer.getvalue()


class CliTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.base = pathlib.Path(folder.name)
        self.package = appx(self.base / "game.appx", game_files())
        self.build = build_folder(self.base / "build")
        self.vclibs = vclibs_folder(self.base / "vclibs", "120")
        self.dest = self.base / "Installed"
        self.stdout, self.stderr = io.StringIO(), io.StringIO()
        self.web = FakeWeb()
        replacement = mock.patch.object(installer, "fetch", self.web)
        replacement.start()
        self.addCleanup(replacement.stop)

    def serve_vclibs(self, data, sha256=None):
        """Serve data as the release's x86 VCLibs 120, pinned to sha256 (default: its own)."""
        key = ("Microsoft.VCLibs.120.00", "x86")
        name, _ = installer.VCLIBS_PACKAGES[key]
        pinned = mock.patch.dict(
            installer.VCLIBS_PACKAGES,
            {key: (name, sha256 or hashlib.sha256(data).hexdigest())},
        )
        pinned.start()
        self.addCleanup(pinned.stop)
        self.web.pages[installer.VCLIBS_DOWNLOADS[key]] = data
        return installer.VCLIBS_DOWNLOADS[key]

    def main(self, argv, ask=None):
        with contextlib.redirect_stdout(self.stdout), contextlib.redirect_stderr(
            self.stderr
        ):
            return installer.main(argv, ask or Answers())

    def options(self):
        text = (self.dest / "appx-portable.ini").read_text()
        return ("UnlockTrial=1" in text, "StubIap=1" in text)

    def scripted(self, *extra):
        return [
            str(self.package),
            "--dest",
            str(self.dest),
            "--build",
            str(self.build),
            "--yes",
            *extra,
        ]

    def test_noninteractive_install(self):
        code = self.main(
            self.scripted("--vclibs", str(self.vclibs), "--store", "both", "--launcher")
        )
        self.assertEqual(code, 0, self.stderr.getvalue())
        for name in [
            "Game.exe",
            "appxportable.dll",
            "DWrite.dll",
            "msvcr120_app.dll",
            "appx-portable.ini",
            LAUNCHER,
        ]:
            self.assertTrue((self.dest / name).is_file(), name)
        self.assertEqual(self.options(), (True, True))

    def test_long_folder_warns(self):
        self.dest = self.base / ("x" * (installer.LONG_FOLDER - len(str(self.base))))
        code = self.main(self.scripted("--vclibs", str(self.vclibs)))
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertIn("Warning:", self.stdout.getvalue())
        self.assertIn("shorter folder", self.stdout.getvalue())

    def test_short_folder_does_not_warn(self):
        code = self.main(self.scripted("--vclibs", str(self.vclibs)))
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertNotIn("Warning:", self.stdout.getvalue())

    def test_path_warning(self):
        game = pathlib.PureWindowsPath("C:/Games/Example")
        self.assertIsNone(installer.path_warning(game, [str(game / "Game.exe")]))
        deep = str(game / ("x" * 250 + ".png"))
        self.assertIn(deep, installer.path_warning(game, [str(game / "Game.exe"), deep]))
        folder = pathlib.PureWindowsPath("C:/" + "x" * installer.LONG_FOLDER)
        self.assertIn(str(folder), installer.path_warning(folder, [str(folder / "Game.exe")]))

    def test_vclibs_sources_are_tried_in_turn(self):
        wrong = vclibs_folder(self.base / "v140", "140")
        code = self.main(
            self.scripted(
                "--vclibs", str(wrong), "--vclibs", str(self.vclibs), "--no-launcher"
            )
        )
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertTrue((self.dest / "msvcr120_app.dll").is_file())
        self.assertFalse((self.dest / LAUNCHER).exists())

    def test_prompts_accept_quoted_paths(self):
        vclibs_appx = self.base / "Microsoft.VCLibs.120.appx"
        with zipfile.ZipFile(vclibs_appx, "w") as archive:
            for name in VCLIBS["120"]:
                archive.writestr(name, pe(dll=True))
        # Package, destination, VCLibs, Store choice, launcher (Enter is yes),
        # then the closing prompt. --build is not a prompt: it goes on the command line.
        answers = Answers(
            f'"{self.package}"', f' "{self.dest}" ', f'"{vclibs_appx}"', "3", "", ""
        )
        code = self.main(["--build", str(self.build)], answers)
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertEqual(self.options(), (True, True))
        self.assertTrue((self.dest / LAUNCHER).is_file())
        # The download failed first, so the prompt says why and where to get it.
        self.assertIn("Could not download", self.stdout.getvalue())
        self.assertIn(
            installer.VCLIBS_DOWNLOADS[("Microsoft.VCLibs.120.00", "x86")],
            self.stdout.getvalue(),
        )

    def test_double_click_waits_before_closing(self):
        answers = Answers("", "")  # no package, then the closing prompt
        self.assertEqual(self.main([], answers), 1)
        self.assertEqual(len(answers.prompts), 2)
        self.assertIn("Press Enter", answers.prompts[-1])

    def test_store_prompt_choices(self):
        vclibs_folder(self.dest, "120")
        for answer, expected in [
            ("1", (True, False)),
            ("2", (False, True)),
            ("3", (True, True)),
            ("", (True, True)),
            ("0", (False, False)),
        ]:
            with self.subTest(answer=answer):
                answers = Answers(answer, "")
                code = self.main(
                    [
                        str(self.package),
                        "--dest",
                        str(self.dest),
                        "--build",
                        str(self.build),
                        "--no-launcher",
                    ],
                    answers,
                )
                self.assertEqual(code, 0, self.stderr.getvalue())
                self.assertEqual(self.options(), expected)

    def test_invalid_store_answer_asks_again(self):
        vclibs_folder(self.dest, "120")
        answers = Answers("7", "2", "")
        self.main(
            [
                str(self.package),
                "--dest",
                str(self.dest),
                "--build",
                str(self.build),
                "--no-launcher",
            ],
            answers,
        )
        self.assertEqual(len(answers.prompts), 3)
        self.assertEqual(self.options(), (False, True))

    def test_yes_without_vclibs_fails(self):
        self.assertEqual(self.main(self.scripted()), 1)
        message = self.stderr.getvalue()
        self.assertIn("Microsoft.VCLibs.120.00", message)
        self.assertIn(
            installer.VCLIBS_DOWNLOADS[("Microsoft.VCLibs.120.00", "x86")], message
        )
        self.assertIn("--vclibs", message)
        self.assertNotIn("--build", message)
        self.assertNotIn("Traceback", message)

    def test_missing_vclibs_are_downloaded(self):
        url = self.serve_vclibs(vclibs_appx_bytes("120"))
        code = self.main(self.scripted("--no-launcher"))
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertEqual(self.web.requested, [url])
        self.assertIn(
            "Downloading Microsoft.VCLibs.120.00 (x86)", self.stdout.getvalue()
        )
        for name in VCLIBS["120"]:
            self.assertTrue((self.dest / name).is_file(), name)

    def test_downloaded_vclibs_must_match_the_pinned_hash(self):
        self.serve_vclibs(vclibs_appx_bytes("120"), sha256="0" * 64)
        self.assertEqual(self.main(self.scripted("--no-launcher")), 1)
        self.assertIn("SHA-256", self.stderr.getvalue())
        self.assertFalse((self.dest / "msvcr120_app.dll").exists())

    def test_vclibs_option_skips_the_download(self):
        self.serve_vclibs(vclibs_appx_bytes("120"))
        code = self.main(self.scripted("--vclibs", str(self.vclibs), "--no-launcher"))
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertEqual(self.web.requested, [])
        self.assertNotIn("Downloading Microsoft.VCLibs", self.stdout.getvalue())

    def test_every_dependency_has_a_pinned_package(self):
        for dependency in installer.VCLIBS:
            for architecture in installer.ARCHITECTURES.values():
                name, sha256 = installer.VCLIBS_PACKAGES[(dependency, architecture)]
                self.assertTrue(name.startswith(f"{dependency}_"), name)
                self.assertIn(f"_{architecture}__", name)
                self.assertRegex(sha256, "^[0-9a-f]{64}$")

    def test_default_destination(self):
        code = self.main(
            [
                str(self.package),
                "--build",
                str(self.build),
                "--vclibs",
                str(self.vclibs),
                "--yes",
            ]
        )
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertTrue((self.base / "Example.Game" / "Game.exe").is_file())

    def test_default_destination_beside_folder_source(self):
        source = self.base / "Example.Game"
        installer.extract_game(self.package, source)
        code = self.main(
            [
                str(source),
                "--build",
                str(self.build),
                "--vclibs",
                str(self.vclibs),
                "--yes",
            ]
        )
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertTrue((self.base / "Example.Game-portable" / "Game.exe").is_file())

    def test_known_game_is_patched(self):
        with mock.patch.object(
            installer, "apply_known_patch", return_value="patched it"
        ) as apply:
            self.assertEqual(
                self.main(self.scripted("--vclibs", str(self.vclibs))),
                0,
                self.stderr.getvalue(),
            )
        manifest, game, _, local = apply.call_args.args
        self.assertEqual(manifest.name, "Example.Game")
        self.assertEqual(game, self.dest.resolve())
        self.assertEqual(
            local, installer.Path(installer.__file__).resolve().parent / "patch"
        )
        self.assertIn("patched it", self.stdout.getvalue())
        with mock.patch.object(installer, "apply_known_patch") as apply:
            self.main(self.scripted("--vclibs", str(self.vclibs), "--no-patch"))
        apply.assert_not_called()

    def test_downloads_runtime_without_build(self):
        def download(tag, architecture, folder):
            self.assertEqual((tag, architecture), ("v9", "x86"))
            return build_folder(folder)

        with mock.patch.object(
            installer, "resolve_release", return_value="v9"
        ) as resolve, mock.patch.object(
            installer, "download_runtime", side_effect=download
        ):
            code = self.main(
                [
                    str(self.package),
                    "--dest",
                    str(self.dest),
                    "--vclibs",
                    str(self.vclibs),
                    "--release",
                    "v9",
                    "--yes",
                ]
            )
        self.assertEqual(code, 0, self.stderr.getvalue())
        resolve.assert_called_once_with("v9")
        self.assertTrue((self.dest / "appxportable.dll").is_file())

    def test_managed_game_launches_host(self):
        package = appx(self.base / "managed.appx", game_files(managed=True))
        code = self.main(
            [
                str(package),
                "--dest",
                str(self.dest),
                "--build",
                str(self.build),
                "--vclibs",
                str(self.vclibs),
                "--yes",
            ]
        )
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertIn("appx-host.exe", (self.dest / LAUNCHER).read_text())

    def test_desktop_copy_flag(self):
        package = appx(
            self.base / "win8.appx", {**game_files(), "Game.exe": pe(appcontainer=True)}
        )
        code = self.main(
            [
                str(package),
                "--dest",
                str(self.dest),
                "--build",
                str(self.build),
                "--vclibs",
                str(self.vclibs),
                "--yes",
                "--desktop-copy",
            ]
        )
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertTrue((self.dest / "Game.desktop.exe").is_file())
        self.assertIn("Game.desktop.exe", (self.dest / LAUNCHER).read_text())

    def test_error_is_one_line(self):
        corrupt = self.base / "corrupt.appx"
        corrupt.write_bytes(b"not a zip")
        self.assertEqual(
            self.main([str(corrupt), "--build", str(self.build), "--yes"]), 1
        )
        self.assertTrue(self.stderr.getvalue().startswith("Error: "))
        self.assertNotIn("Traceback", self.stderr.getvalue())

    def test_rerun_keeps_store_choice(self):
        self.main(self.scripted("--vclibs", str(self.vclibs), "--store", "trial"))
        self.assertEqual(self.main(self.scripted()), 0, self.stderr.getvalue())
        self.assertEqual(self.options(), (True, False))

    def test_wrong_architecture_vclibs_is_named(self):
        package = appx(self.base / "x64.appx", game_files(machine=0x8664, arch="x64"))
        build = build_folder(self.base / "build64", machine=0x8664, host=False)
        code = self.main(
            [
                str(package),
                "--dest",
                str(self.dest),
                "--build",
                str(build),
                "--vclibs",
                str(self.vclibs),
                "--yes",
            ]
        )
        self.assertEqual(code, 1)
        message = self.stderr.getvalue()
        self.assertIn("is x86, but the game is x64", message)

    def test_unexpected_error_is_shown_before_closing(self):
        answers = Answers("")
        with mock.patch.object(installer, "install", side_effect=ValueError("boom")):
            self.assertEqual(self.main([], answers), 1)
        self.assertIn("boom", self.stderr.getvalue())
        self.assertTrue(self.stderr.getvalue().startswith("Error: "))
        self.assertIn("Press Enter", answers.prompts[-1])

    def test_drag_and_drop_waits_before_closing(self):
        # Dropping a package on the script gives it one argument, no --yes.
        vclibs_folder(self.dest, "120")
        answers = Answers(str(self.dest), "", "", "")
        code = self.main([str(self.package), "--build", str(self.build)], answers)
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertIn("Press Enter", answers.prompts[-1])

    def test_relative_destination_becomes_absolute(self):
        # Python 3.8/3.9 on Windows resolve() a missing path to itself, relative.
        original = installer.Path.resolve

        def old_resolve(path, strict=False):
            return path if not path.exists() else original(path, strict)

        cwd = os.getcwd()
        os.chdir(self.base)
        self.addCleanup(os.chdir, cwd)
        with mock.patch.object(
            installer.Path, "resolve", old_resolve
        ), mock.patch.object(
            installer, "apply_known_patch", return_value=None
        ) as apply:
            code = self.main(
                [
                    str(self.package),
                    "--dest",
                    "relative",
                    "--build",
                    str(self.build),
                    "--vclibs",
                    str(self.vclibs),
                    "--yes",
                ]
            )
        self.assertEqual(code, 0, self.stderr.getvalue())
        self.assertTrue(apply.call_args.args[1].is_absolute())


if __name__ == "__main__":
    unittest.main()
