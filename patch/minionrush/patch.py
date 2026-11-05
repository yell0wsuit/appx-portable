"""Skip the blocking startup analytics request in Minion Rush 4.1.4.1 x86."""

import argparse
import hashlib
import os
from pathlib import Path
import sys
import tempfile

ORIGINAL_SHA256 = "869e2e942e78877b2f2e58c1edd5bef53efefac7b30a32440d9c8f53fe8361c0"
PATCHED_SHA256 = "ec49581d6c7b90c3ce6f3b8157c7c5d5aa8fd91b9e406d917595aaa2e0f7b3f4"
# TrackApplicationLoading calls the blocking analytics helper at RVA 0x17DBD
# (VA 0x10017DBD). Skip this cdecl call; its caller still cleans up the stack
# and updates its local tracking state. The game executable and content assets
# stay intact.
PATCHES = ((0x171BD, bytes.fromhex("e8 ee c4 ff ff"), b"\x90" * 5),)
BACKUP_SUFFIX = ".analytics-original"

def sha256(data):
    return hashlib.sha256(data).hexdigest()


def identify(data):
    digest = sha256(data)
    if digest == ORIGINAL_SHA256:
        return "original"
    if digest == PATCHED_SHA256:
        return "patched"
    raise ValueError(
        f"Unsupported library SHA-256: {digest}. "
        "Requires the original Minion Rush 4.1.4.1 x86 IGPLib.dll "
        "or this patch's exact output."
    )


def patched_data(original):
    if identify(original) != "original":
        raise ValueError("Expected the original library.")
    result = bytearray(original)
    for offset, before, after in PATCHES:
        if result[offset : offset + len(before)] != before:
            raise ValueError(f"Unexpected instructions at file offset {offset:#x}.")
        result[offset : offset + len(before)] = after
    if sha256(result) != PATCHED_SHA256:
        raise ValueError("Patched library failed its output hash check.")
    return bytes(result)


def replace_file(path, data):
    # Prepare the complete image beside the target before replacing it. A locked
    # Windows library fails replacement while leaving the old image intact.
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
    original = path.read_bytes()
    state = identify(original)
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
    result = patched_data(original)
    try:
        with backup.open("xb") as output:
            output.write(original)
            output.flush()
            os.fsync(output.fileno())
    except FileExistsError as exc:
        if sha256(backup.read_bytes()) != ORIGINAL_SHA256:
            raise ValueError(
                f"Existing backup differs; refusing to overwrite it: {backup}"
            ) from exc
    replace_file(path, result)
    return f"Patched: {path}\nOriginal backup: {backup}"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "game",
        type=Path,
        help="Minion Rush 4.1.4.1 x86 game folder (close the game first)",
    )
    action = parser.add_mutually_exclusive_group()
    action.add_argument(
        "--check",
        action="store_true",
        help="Identify the library without changing files",
    )
    action.add_argument(
        "--restore", action="store_true", help="Restore the verified original backup"
    )
    args = parser.parse_args(argv)
    try:
        print(patch_file(args.game / "IGPLib.dll", restore=args.restore, check=args.check))
        return 0
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
