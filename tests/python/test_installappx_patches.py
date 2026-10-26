import io
import json
import os
import pathlib
import tempfile
import unittest
import urllib.error
import zipfile
from unittest import mock

from installappx_fixtures import load_installer, manifest_xml

installer = load_installer()
API = 'https://api.github.com/repos/yell0wsuit/appx-portable/releases'
RAW = 'https://raw.githubusercontent.com/yell0wsuit/appx-portable'
# A stand-in patch: records its argument where the test says.
RECORDING_PATCH = b'import os, sys\nopen(os.environ["RECORD"], "w").write(sys.argv[1])\nprint("patched")\n'


def zip_bytes(files):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w') as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    return buffer.getvalue()


class FakeWeb:
    def __init__(self, pages):
        self.pages = pages
        self.requested = []

    def __call__(self, url):
        self.requested.append(url)
        if url not in self.pages:
            raise installer.InstallError(f'Could not download {url}: 404')
        return self.pages[url]


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.base = pathlib.Path(folder.name)

    def web(self, pages):
        web = FakeWeb(pages)
        replacement = mock.patch.object(installer, 'fetch', web)
        replacement.start()
        self.addCleanup(replacement.stop)
        return web

    def release(self, tag, names):
        return json.dumps({'tag_name': tag, 'assets': [
            {'name': name, 'browser_download_url': f'https://example.test/{name}'} for name in names]}).encode()

    def test_resolve_latest_and_explicit(self):
        web = self.web({f'{API}/latest': json.dumps({'tag_name': 'v2026.10.1'}).encode()})
        self.assertEqual(installer.resolve_release(None), 'v2026.10.1')
        self.assertEqual(installer.resolve_release('v1'), 'v1')
        self.assertEqual(web.requested, [f'{API}/latest'])

    def test_download_runtime_picks_architecture(self):
        self.web({
            f'{API}/tags/v1': self.release('v1', ['appx-portable-v1-x86.zip', 'appx-portable-v1-x64.zip']),
            'https://example.test/appx-portable-v1-x64.zip': zip_bytes({
                'appxportable.dll': b'x64 shim', 'sub/../../evil.dll': b'evil', 'sub/other.dll': b'nested'}),
        })
        folder = self.base / 'runtime'
        self.assertEqual(installer.download_runtime('v1', 'x64', folder), folder)
        self.assertEqual((folder / 'appxportable.dll').read_bytes(), b'x64 shim')
        self.assertEqual(sorted(p.name for p in self.base.rglob('*') if p.is_file()), ['appxportable.dll'])

    def test_missing_asset(self):
        self.web({f'{API}/tags/v1': self.release('v1', ['appx-portable-v1-x64.zip'])})
        with self.assertRaisesRegex(installer.InstallError, 'appx-portable-v1-x86.zip'):
            installer.download_runtime('v1', 'x86', self.base / 'runtime')

    def test_fetch_failure_message(self):
        url = f'{API}/latest'
        with mock.patch.object(installer.urllib.request, 'urlopen', side_effect=urllib.error.URLError('offline')):
            with self.assertRaises(installer.InstallError) as raised:
                installer.fetch(url)
        self.assertIn(url, str(raised.exception))
        self.assertIn('--build', str(raised.exception))

    def test_fetch_sends_user_agent(self):
        response = mock.MagicMock()
        response.__enter__.return_value.read.return_value = b'body'
        with mock.patch.object(installer.urllib.request, 'urlopen', return_value=response) as urlopen:
            self.assertEqual(installer.fetch('https://example.test/'), b'body')
        self.assertTrue(urlopen.call_args.args[0].get_header('User-agent'))


class PatchTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.base = pathlib.Path(folder.name)
        self.game = self.base / 'game'
        self.game.mkdir()
        self.record = self.base / 'argument.txt'
        environment = mock.patch.dict(os.environ, {'RECORD': str(self.record)})
        environment.start()
        self.addCleanup(environment.stop)
        self.web = FakeWeb({})
        replacement = mock.patch.object(installer, 'fetch', self.web)
        replacement.start()
        self.addCleanup(replacement.stop)

    def manifest(self, name, version):
        return installer.read_manifest(manifest_xml(name=name, version=version))

    def test_unknown_game_has_no_patch(self):
        self.assertIsNone(installer.apply_known_patch(self.manifest('Example.Game', '1.0.0.0'), self.game, 'v1', None))
        self.assertEqual(self.web.requested, [])

    def test_known_versions_only(self):
        self.assertIsNone(installer.apply_known_patch(self.manifest('PopCap.BejweledLIVE', '4.0.0.11'), self.game, 'v1', None))

    def test_local_patch_runs_against_game(self):
        local = self.base / 'patch'
        (local / 'bejeweled').mkdir(parents=True)
        (local / 'bejeweled' / 'patch.py').write_bytes(RECORDING_PATCH)
        output = installer.apply_known_patch(self.manifest('PopCap.BejweledLIVE', '4.0.0.10'), self.game, None, local)
        self.assertIn('patched', output)
        self.assertEqual(pathlib.Path(self.record.read_text()), self.game)
        self.assertEqual(self.web.requested, [])

    def test_downloaded_patch_uses_tag(self):
        folder = f'{RAW}/v1/patch/raymanwin10'
        self.web.pages.update({f'{folder}/patch.py': RECORDING_PATCH, f'{folder}/CustomTitleBar.xaml': b'<UserControl />'})
        manifest = self.manifest('Ubisoft.RaymanFiestaRunWindows10Edition', '1.0.23.0')
        installer.apply_known_patch(manifest, self.game, 'v1', None)
        self.assertEqual(sorted(self.web.requested), sorted([f'{folder}/patch.py', f'{folder}/CustomTitleBar.xaml']))
        self.assertEqual(pathlib.Path(self.record.read_text()), self.game)

    def test_mickey_targets_executable(self):
        self.web.pages[f'{RAW}/v1/patch/wheresmymickey/patch.py'] = RECORDING_PATCH
        installer.apply_known_patch(self.manifest('Disney.WheresMyMickey', '1.1.0.30'), self.game, 'v1', None)
        self.assertEqual(pathlib.Path(self.record.read_text()), self.game / 'WheresMyMickey.Windows8.exe')

    def test_failing_patch_raises(self):
        self.web.pages[f'{RAW}/v1/patch/mssudoku/patch.py'] = b'import sys\nsys.exit("Unsupported asset")\n'
        with self.assertRaisesRegex(installer.InstallError, 'Unsupported'):
            installer.apply_known_patch(self.manifest('Microsoft.MicrosoftSudoku', '1.7.10190.0'), self.game, 'v1', None)

    def test_every_known_patch_exists_in_the_repo(self):
        for patch in installer.KNOWN_PATCHES.values():
            for name in patch.files:
                self.assertTrue((installer.Path(__file__).parents[2] / 'patch' / patch.folder / name).is_file(),
                                f'{patch.folder}/{name}')


if __name__ == '__main__':
    unittest.main()
