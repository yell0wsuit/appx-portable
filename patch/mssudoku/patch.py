"""Keep Microsoft Sudoku 1.7.10190.0's video-loading popup closed during XAML parsing."""

import argparse
import hashlib
import os
from pathlib import Path
import sys
import tempfile

ASSET = Path("Arkadium.Advertisement/Video/Controls/VideoAdsPreloader.xbf")
ORIGINAL_SHA256 = "ec0b514054475dad3412c368bb4efeb9cc00f9a9c9d4597cd44311ec9320f751"
PATCHED_SHA256 = "5295665cbf6bda6443b7bbdaec1e2b4a55acd21a03fb00c41b9f4d974724c82c"
# XBF v1: Popup.IsOpen's BooleanTrue value becomes BooleanFalse. The node
# stream and metadata retain their lengths, offsets, and connection IDs.
OFFSET = 0x73D
BACKUP_SUFFIX = ".islands-original"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def identify(data):
    digest = sha256(data)
    if digest == ORIGINAL_SHA256:
        return "original"
    if digest == PATCHED_SHA256:
        return "patched"
    raise ValueError(f"Unsupported Sudoku popup asset SHA-256: {digest}")


def replace_file(path, data):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=path.parent, prefix=path.name + ".", suffix=".tmp", delete=False
        ) as output:
            temporary = Path(output.name)
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, path.stat().st_mode)
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def patch_file(path, *, restore=False, check=False):
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    state = identify(data)
    if check:
        return f"{path}: {state}"
    backup = path.with_name(path.name + BACKUP_SUFFIX)
    if restore:
        if state == "original":
            return f"Already original: {path}"
        saved = backup.read_bytes()
        if sha256(saved) != ORIGINAL_SHA256:
            raise ValueError(f"Backup hash mismatch; refusing to restore: {backup}")
        replace_file(path, saved)
        return f"Restored: {path}\nBackup retained: {backup}"
    if state == "patched":
        return f"Already patched: {path}"
    result = bytearray(data)
    if result[OFFSET] != 2:
        raise ValueError("Unexpected XBF BooleanTrue node")
    result[OFFSET] = 1
    if sha256(result) != PATCHED_SHA256:
        raise ValueError("Patched popup failed its output hash check")
    try:
        with backup.open("xb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
    except FileExistsError as error:
        if sha256(backup.read_bytes()) != ORIGINAL_SHA256:
            raise ValueError(
                f"Existing backup differs; refusing to overwrite: {backup}"
            ) from error
    replace_file(path, result)
    return f"Patched: {path}\nOriginal backup: {backup}"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "game", type=Path, help="Staged Microsoft Sudoku folder (close the game first)"
    )
    action = parser.add_mutually_exclusive_group()
    action.add_argument(
        "--check", action="store_true", help="Identify the asset without changing files"
    )
    action.add_argument(
        "--restore", action="store_true", help="Restore the verified original asset"
    )
    args = parser.parse_args(argv)
    try:
        game = args.game.resolve(strict=True)
        asset = (game / ASSET).resolve(strict=True)
        asset.relative_to(game)
        print(patch_file(asset, restore=args.restore, check=args.check))
        return 0
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
