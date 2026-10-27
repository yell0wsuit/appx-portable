"""Use portable WebViews in Practical English Usage 1.0.6.9953's compiled XAML."""

import argparse
import hashlib
import os
from pathlib import Path
import struct
import sys
import tempfile
from xml.etree import ElementTree

IDENTITY = ("OxfordUniversityPress.PracticalEnglishUsage", "1.0.6.9953")
BACKUP_SUFFIX = ".islands-original"
# Only these exact assets are supported. No app content is shipped in the patch.
ASSETS = {
    "View/AdditionalArticlesFlyout.xbf": (
        "131026157ee89f474ba341866aadd059433ff2dd0c9b9ef347637bfc4c6f4ea7",
        "158d99e6c804146629f46200c2b7e93282d2c08feb11e7e6d777c81df683eba6"),
    "View/ArticlePage.xbf": (
        "b15dcae936e6c3007ff5339a14f458e4d92035d19eb5c12a87e73f046ae727c1",
        "00c8860c61d1e5eea0fb442790a5f37f42f2c55c5da5c357e5066a279cb12329"),
    "View/MainPage.xbf": (
        "a0548c322a404d99f183216a874b124981a3e910f8b0a730aa16ca2044f77ec3",
        "ce3671bdd012fc4be64ba76804bce6839951155360f402068b5395269790d756"),
    "WinRTXamlToolkit/Controls/WebBrowser/WebBrowser.xbf": (
        "43e9fef965aa1f37bb79a472ff7ab0b026960eb7022a788fcf49b85882abbf9a",
        "eac2b553bb11916d516f10f95e55c451741c6d2e84ef249c3d76d36ed036696f"),
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def rewrite_xbf(data):
    """Retarget native WebView types in XBF v1; leave nodes and IDs intact.

    Metadata layout is described by Microsoft's MIT-licensed WinUI source:
    dxaml/xcp/core/inc/XamlBinaryMetadata.h and XamlSerializationHelper.h.
    Offsets are relative to the metadata stream (after the 12-byte file header).
    """
    if len(data) < 132 or data[:4] != b"XBF\0":
        raise ValueError("Invalid XBF header")
    metadata_size, node_size, major, minor = struct.unpack_from("<4I", data, 4)
    if (major, minor) != (1, 0):
        raise ValueError("Only XBF v1.0 is supported")
    if 12 + metadata_size + node_size != len(data):
        raise ValueError("Invalid XBF section sizes")
    offsets = list(struct.unpack_from("<6Q", data, 20))
    if offsets[0] != 120 or offsets != sorted(offsets) or offsets[-1] >= metadata_size:
        raise ValueError("Invalid XBF metadata offsets")

    def vector(section, width):
        start = 12 + offsets[section]
        end = 12 + (offsets[section + 1] if section < 5 else metadata_size)
        if start + 4 > end:
            raise ValueError("Truncated XBF metadata vector")
        count = struct.unpack_from("<I", data, start)[0]
        if start + 4 + count * width != end:
            raise ValueError("Invalid XBF metadata vector size")
        return [struct.unpack_from("<" + "I" * (width // 4), data, start + 4 + i * width)
                for i in range(count)]

    position = 12 + offsets[0]
    string_end = 12 + offsets[1]
    if position + 4 > string_end:
        raise ValueError("Truncated XBF string table")
    string_count = struct.unpack_from("<I", data, position)[0]
    position += 4
    if string_count > (string_end - position) // 4:
        raise ValueError("Invalid XBF string count")
    strings = []
    for _ in range(string_count):
        if position + 4 > string_end:
            raise ValueError("Truncated XBF string length")
        length = struct.unpack_from("<I", data, position)[0]
        position += 4
        if length > (string_end - position) // 2:
            raise ValueError("Truncated XBF string")
        strings.append(data[position:position + length * 2].decode("utf-16-le"))
        position += length * 2
    if position != string_end:
        raise ValueError("Invalid XBF string table size")

    assemblies, namespaces, types = vector(1, 8), vector(2, 8), vector(3, 12)
    vector(4, 12)
    vector(5, 4)
    targets = []
    try:
        for index, (flags, namespace, name) in enumerate(types):
            # Directive/unknown types use a different namespace table.
            if flags or strings[name] != "WebView":
                continue
            assembly, namespace_name = namespaces[namespace]
            provider, _ = assemblies[assembly]
            if provider == 1 and strings[namespace_name] == "Windows.UI.Xaml.Controls":
                targets.append(12 + offsets[3] + 4 + index * 12 + 8)
    except IndexError as error:
        raise ValueError("Invalid XBF metadata index") from error
    if not targets:
        return data
    result = bytearray(data)
    for target in targets:
        struct.pack_into("<I", result, target, string_count)
    name = "PortableWebView"
    addition = struct.pack("<I", len(name)) + name.encode("utf-16-le")
    result[string_end:string_end] = addition
    struct.pack_into("<I", result, 12 + offsets[0], string_count + 1)
    struct.pack_into("<I", result, 4, metadata_size + len(addition))
    for index in range(1, 6):
        offsets[index] += len(addition)
    struct.pack_into("<6Q", result, 20, *offsets)
    return bytes(result)


def replace_file(path, data):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + ".", suffix=".tmp", delete=False) as output:
            temporary = Path(output.name)
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, path.stat().st_mode)
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def patch_game(game, *, restore=False, check=False):
    game = Path(game).resolve(strict=True)
    manifest = ElementTree.parse(game / "AppxManifest.xml")
    identity = next((e for e in manifest.iter() if e.tag.rsplit("}", 1)[-1] == "Identity"), None)
    if identity is None or (identity.get("Name"), identity.get("Version")) != IDENTITY:
        raise ValueError("This WebView patch supports Practical English Usage 1.0.6.9953")
    plan, states = [], []
    # Validate every asset and backup before writing anything.
    for name, (original_hash, patched_hash) in ASSETS.items():
        path = game / name
        data = path.read_bytes()
        current_hash = digest(data)
        if current_hash not in (original_hash, patched_hash):
            raise ValueError(f"Unsupported {name} SHA-256: {current_hash}")
        original = current_hash == original_hash
        states.append(f"{name}: {'original' if original else 'patched'}")
        if check:
            continue
        backup = path.with_name(path.name + BACKUP_SUFFIX)
        if backup.exists() and digest(backup.read_bytes()) != original_hash:
            raise ValueError(f"Backup hash mismatch: {backup}")
        if restore and not original:
            saved = backup.read_bytes()
            plan.append((path, saved, None))
        elif not restore and original:
            patched = rewrite_xbf(data)
            if digest(patched) != patched_hash:
                raise ValueError(f"Patched output hash mismatch: {name}")
            plan.append((path, patched, data))
    if check:
        return "\n".join(states)
    for path, output, original in plan:
        if original is not None:
            backup = path.with_name(path.name + BACKUP_SUFFIX)
            try:
                with backup.open("xb") as saved:
                    saved.write(original)
                    saved.flush()
                    os.fsync(saved.fileno())
            except FileExistsError:
                if digest(backup.read_bytes()) != digest(original):
                    raise ValueError(f"Backup changed: {backup}")
        replace_file(path, output)
    return f"{'Restored' if restore else 'Prepared'} portable WebView markup ({len(plan)} changed): {game}"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path, help="Staged PEU folder (close the game first)")
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--check", action="store_true")
    action.add_argument("--restore", action="store_true")
    args = parser.parse_args(argv)
    try:
        print(patch_game(args.game, restore=args.restore, check=args.check))
        return 0
    except (OSError, ValueError, ElementTree.ParseError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
