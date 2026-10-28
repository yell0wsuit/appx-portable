import argparse
import contextlib
import hashlib
import io
import pathlib
import tarfile
import tempfile
import unittest
from unittest import mock

from installappx_fixtures import load_installer

installer = load_installer()


def tar_xz(entries):
    """A .tar.xz of entries: (name, data) files, or (name, '->target') symlinks."""
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w:xz") as archive:
        for name, data in entries:
            info = tarfile.TarInfo(name)
            if isinstance(data, str) and data.startswith("->"):
                info.type = tarfile.SYMTYPE
                info.linkname = data[2:]
                archive.addfile(info)
            else:
                info.size = len(data)
                info.mode = 0o755
                archive.addfile(info, io.BytesIO(data))
    return buffer.getvalue()


class MacOSDependencyTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.base = pathlib.Path(folder.name)
        self.app = self.base / "Applications" / "Wine Devel.app"
        for patch in (
            mock.patch.object(installer, "WINE_DEVEL_APP", self.app),
            mock.patch.object(installer, "GSTREAMER_FRAMEWORK", self.base / "GStreamer.framework"),
            mock.patch.object(installer, "needs_rosetta", return_value=False),
            mock.patch.dict(installer.os.environ, {"PATH": str(self.base)}),
        ):
            patch.start()
            self.addCleanup(patch.stop)
        self.wine = tar_xz([
            ("Wine Devel.app/Contents/Resources/wine/bin/wine", b"#!/bin/sh\n"),
            ("Wine Devel.app/Contents/Resources/wine/bin/wine64", "->wine"),
        ])

    def serve(self, data, sha256=None):
        url = "https://example.invalid/wine.tar.xz"
        pinned = mock.patch.object(installer, "WINE_DEVEL", (url, sha256 or hashlib.sha256(data).hexdigest()))
        pinned.start()
        self.addCleanup(pinned.stop)
        fetch = mock.patch.object(installer, "fetch", return_value=data)
        fetch.start()
        self.addCleanup(fetch.stop)

    def run_setup(self, yes=False, answers=()):
        answers = list(answers)
        prompts = []

        def ask(prompt):
            prompts.append(prompt)
            return answers.pop(0)

        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            installer.add_macos_dependencies(argparse.Namespace(yes=yes), ask)
        return prompts, output.getvalue()

    def test_downloads_wine_devel_when_missing(self):
        self.serve(self.wine)
        with mock.patch.object(installer, "install_gstreamer") as gstreamer:
            prompts, _ = self.run_setup(answers=["", "n"])
        wine = self.app / "Contents/Resources/wine/bin/wine"
        self.assertTrue(installer.os.access(wine, installer.os.X_OK))
        self.assertTrue((wine.parent / "wine64").is_symlink())
        self.assertTrue(installer.wine_installed())
        self.assertEqual(len(prompts), 2)
        gstreamer.assert_not_called()
        self.assertEqual([p.name for p in self.app.parent.iterdir()], ["Wine Devel.app"])

    def test_yes_downloads_wine_but_never_asks_for_sudo(self):
        self.serve(self.wine)
        with mock.patch.object(installer, "needs_rosetta", return_value=True), mock.patch.object(
            installer, "install_gstreamer"
        ) as gstreamer, mock.patch.object(installer, "install_rosetta") as rosetta:
            prompts, output = self.run_setup(yes=True)
        self.assertEqual(prompts, [])
        self.assertTrue(self.app.is_dir())
        gstreamer.assert_not_called()
        rosetta.assert_not_called()
        self.assertIn("Rosetta 2", output)
        self.assertIn("GStreamer", output)

    def test_present_dependencies_are_left_alone(self):
        (self.base / "GStreamer.framework").mkdir()
        wine = self.base / "wine"
        wine.write_text("#!/bin/sh\n")
        wine.chmod(0o755)
        with mock.patch.object(installer, "fetch") as fetch:
            prompts, _ = self.run_setup()
        self.assertEqual(prompts, [])
        fetch.assert_not_called()

    def test_wine_hash_mismatch_is_refused(self):
        self.serve(self.wine, sha256="0" * 64)
        with self.assertRaisesRegex(installer.InstallError, "SHA-256"):
            installer.install_wine_devel()
        self.assertFalse(self.app.exists())

    def test_tar_entries_leaving_the_folder_are_refused(self):
        for entries in (
            [("../escape", b"x")],
            [("Wine Devel.app/link", "->../../escape")],
            [("/absolute", b"x")],
        ):
            with self.subTest(entries=entries[0][0]):
                destination = self.base / "out"
                destination.mkdir(exist_ok=True)
                with self.assertRaisesRegex(installer.InstallError, "Refusing"):
                    installer.extract_tar_xz(tar_xz(entries), destination)
        self.assertFalse((self.base / "escape").exists())

    def serve_gstreamer(self, versions, data, sha256=None):
        """Serve a GStreamer release listing, and the newest stable version's pkg and checksum."""
        base = installer.GSTREAMER_RELEASES
        pages = {base: "".join(f'<a href="{v}/">{v}/</a>' for v in versions).encode()}
        pages[base + "1.30.2/gstreamer-1.0-1.30.2-universal.pkg"] = data
        pages[base + "1.30.2/gstreamer-1.0-1.30.2-universal.pkg.sha256sum"] = (
            f"{sha256 or hashlib.sha256(data).hexdigest()}  gstreamer-1.0-1.30.2-universal.pkg\n".encode()
        )
        fetch = mock.patch.object(installer, "fetch", side_effect=lambda url, instead="": pages[url])
        fetch.start()
        self.addCleanup(fetch.stop)

    def test_latest_stable_gstreamer(self):
        # Odd minor versions are development releases; 1.30.10 sorts after 1.30.2.
        self.serve_gstreamer(["1.28.8", "1.30.2", "1.31.1", "1.30.10", "0.99.0"], b"")
        self.assertEqual(installer.latest_gstreamer(), "1.30.10")

    def test_gstreamer_installs_with_sudo_installer(self):
        self.serve_gstreamer(["1.28.8", "1.29.2", "1.30.2"], b"pkg")
        with mock.patch.object(installer.subprocess, "run") as run:
            run.return_value.returncode = 0
            installer.install_gstreamer()
        command = run.call_args.args[0]
        self.assertEqual(command[:3], ["/usr/bin/sudo", "/usr/sbin/installer", "-pkg"])
        self.assertTrue(command[3].endswith("gstreamer-1.0-1.30.2-universal.pkg"))
        self.assertEqual(command[4:], ["-target", "/"])

    def test_gstreamer_checksum_mismatch_is_refused(self):
        self.serve_gstreamer(["1.30.2"], b"pkg", sha256="0" * 64)
        with mock.patch.object(installer.subprocess, "run") as run:
            with self.assertRaisesRegex(installer.InstallError, "SHA-256"):
                installer.install_gstreamer()
        run.assert_not_called()

    def test_failed_gstreamer_install_only_warns(self):
        (self.app / "Contents/Resources/wine/bin").mkdir(parents=True)
        wine = self.app / "Contents/Resources/wine/bin/wine"
        wine.write_text("")
        wine.chmod(0o755)
        with mock.patch.object(installer, "install_gstreamer", side_effect=installer.InstallError("GStreamer was not installed.")):
            _, output = self.run_setup(answers=[""])
        self.assertIn("GStreamer was not installed.", output)


if __name__ == "__main__":
    unittest.main()
