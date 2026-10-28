import importlib.util
import pathlib
import struct
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('peu_patch', ROOT / 'patch/peu/patch.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


def vector(records):
    return struct.pack('<I', len(records)) + b''.join(struct.pack('<' + 'I' * len(row), *row) for row in records)


def fixture():
    # The shared WebView string is also referenced by an unrelated custom type
    # and by opaque node data. Retargeting must change only the native type.
    strings = ['Windows', 'Windows.UI.Xaml.Controls', 'Example', 'WebView', 'Grid']
    tables = [struct.pack('<I', len(strings)) + b''.join(
        struct.pack('<I', len(s)) + s.encode('utf-16-le') for s in strings),
        vector([(1, 0)]), vector([(0, 1), (0, 2)]),
        vector([(0, 0, 3), (0, 1, 3), (0, 0, 4)]), vector([]), vector([])]
    offsets, size = [], 120
    for table in tables:
        offsets.append(size)
        size += len(table)
    nodes = b'opaque node stream with shared string ID ' + struct.pack('<I', 3)
    metadata = struct.pack('<II6Q', 1, 0, *offsets) + b'H' * 64 + b''.join(tables)
    return b'XBF\0' + struct.pack('<II', size, len(nodes)) + metadata + nodes


class XbfTests(unittest.TestCase):
    def test_retargets_only_native_type_and_preserves_nodes(self):
        original = fixture()
        result = patch.rewrite_xbf(original)
        old_offsets = struct.unpack_from('<6Q', original, 20)
        offsets = struct.unpack_from('<6Q', result, 20)
        growth = len(result) - len(original)
        self.assertEqual(growth, 34)
        self.assertEqual(offsets, (old_offsets[0], *(n + growth for n in old_offsets[1:])))
        metadata, node_size = struct.unpack_from('<II', result, 4)
        self.assertEqual(result[12 + metadata:], original[-node_size:])
        self.assertEqual(result[68:132], original[68:132])  # source hash
        # Original strings are retained, including their indexes.
        self.assertEqual(result[136:12 + old_offsets[1]], original[136:12 + old_offsets[1]])
        self.assertEqual(struct.unpack_from('<III', result, 16 + offsets[3]), (0, 0, 5))
        self.assertEqual(struct.unpack_from('<III', result, 28 + offsets[3]), (0, 1, 3))
        self.assertEqual(struct.unpack_from('<III', result, 40 + offsets[3]), (0, 0, 4))
        self.assertEqual(patch.rewrite_xbf(result), result)

    def test_rejects_invalid_headers_sizes_offsets_and_indexes(self):
        source = fixture()
        cases = [b'', source[:-1]]
        for offset, fmt, value in [(12, '<I', 2), (20, '<Q', 0), (136, '<I', 0xFFFFFFFF)]:
            broken = bytearray(source)
            struct.pack_into(fmt, broken, offset, value)
            cases.append(broken)
        type_offset = struct.unpack_from('<Q', source, 44)[0]
        broken = bytearray(source)
        struct.pack_into('<I', broken, 16 + type_offset + 8, 99999)
        cases.append(broken)
        for data in cases:
            with self.subTest(data=bytes(data[:24])):
                with self.assertRaises(ValueError):
                    patch.rewrite_xbf(data)


class PatchTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.game = pathlib.Path(temporary.name)
        self.original = fixture()
        self.patched = patch.rewrite_xbf(self.original)
        self.assets = {'View/MainPage.xbf': (patch.digest(self.original), patch.digest(self.patched)),
                       'View/ArticlePage.xbf': (patch.digest(self.original), patch.digest(self.patched))}
        replacement = mock.patch.object(patch, 'ASSETS', self.assets)
        replacement.start()
        self.addCleanup(replacement.stop)
        (self.game / 'AppxManifest.xml').write_text(
            '<Package xmlns="urn:test"><Identity Name="OxfordUniversityPress.PracticalEnglishUsage" '
            'Version="1.0.6.9953"/></Package>')
        (self.game / 'View').mkdir()
        for name in self.assets:
            (self.game / name).write_bytes(self.original)

    def test_patch_check_idempotence_and_restore(self):
        self.assertIn('original', patch.patch_game(self.game, check=True))
        self.assertIn('2 changed', patch.patch_game(self.game))
        for name in self.assets:
            path = self.game / name
            self.assertEqual(path.read_bytes(), self.patched)
            self.assertEqual(path.with_name(path.name + patch.BACKUP_SUFFIX).read_bytes(), self.original)
        self.assertNotIn(': original', patch.patch_game(self.game, check=True))
        self.assertIn('0 changed', patch.patch_game(self.game))
        self.assertIn('2 changed', patch.patch_game(self.game, restore=True))
        for name in self.assets:
            self.assertEqual((self.game / name).read_bytes(), self.original)

    def test_unknown_asset_aborts_before_any_write(self):
        (self.game / 'View/ArticlePage.xbf').write_bytes(b'unknown version')
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            patch.patch_game(self.game)
        self.assertEqual((self.game / 'View/MainPage.xbf').read_bytes(), self.original)
        self.assertEqual(list(self.game.rglob('*' + patch.BACKUP_SUFFIX)), [])

    def test_wrong_manifest_and_bad_backup_are_rejected(self):
        (self.game / 'View/MainPage.xbf.islands-original').write_bytes(b'wrong backup')
        with self.assertRaisesRegex(ValueError, 'Backup hash'):
            patch.patch_game(self.game)
        self.assertEqual((self.game / 'View/MainPage.xbf').read_bytes(), self.original)
        (self.game / 'AppxManifest.xml').write_text('<Package><Identity Name="Other" Version="1"/></Package>')
        with self.assertRaisesRegex(ValueError, 'supports Practical English Usage'):
            patch.patch_game(self.game)

    def test_output_hash_is_verified_before_writing(self):
        self.assets['View/MainPage.xbf'] = (patch.digest(self.original), '0' * 64)
        with self.assertRaisesRegex(ValueError, 'output hash'):
            patch.patch_game(self.game)
        self.assertEqual((self.game / 'View/MainPage.xbf').read_bytes(), self.original)

    def test_restore_validates_all_backups_before_writing(self):
        patch.patch_game(self.game)
        (self.game / 'View/ArticlePage.xbf.islands-original').write_bytes(b'wrong backup')
        with self.assertRaisesRegex(ValueError, 'Backup hash'):
            patch.patch_game(self.game, restore=True)
        self.assertEqual((self.game / 'View/MainPage.xbf').read_bytes(), self.patched)


if __name__ == '__main__':
    unittest.main()
