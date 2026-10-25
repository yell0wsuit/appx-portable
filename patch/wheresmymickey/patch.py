"""Skip unused AMPS test initializers in Where's My Mickey? 1.1.0.30 x86."""

import argparse
import hashlib
import os
from pathlib import Path
import sys
import tempfile

ORIGINAL_SHA256 = "d887400df1b4c3a21aa2669d731288ce23ddd0b28ac50309257ac85fb5ae2424"
PATCHED_SHA256 = "83e1bc9cb836884ccbd65e02c78eae832f927bd14d44c13787471f4867e510b1"
# File offsets for VA 0x80e660 and 0x80e750 in this exact executable.
# Replace `push ebp; mov ebp, esp` with `xor eax, eax; ret`. Neither initializer
# constructs its unused object or registers its corresponding atexit cleanup.
PATCHES = (
    (0x40DA60, bytes.fromhex("55 8b ec"), bytes.fromhex("31 c0 c3")),
    (0x40DB50, bytes.fromhex("55 8b ec"), bytes.fromhex("31 c0 c3")),
)
BACKUP_SUFFIX = ".amps-original"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def identify(data):
    digest = sha256(data)
    if digest == ORIGINAL_SHA256:
        return "original"
    if digest == PATCHED_SHA256:
        return "patched"
    raise ValueError(
        f"Unsupported executable SHA-256: {digest}. "
        "Requires the original Where's My Mickey? 1.1.0.30 x86 EXE "
        "or this patch's exact output."
    )


def patched_data(original):
    if identify(original) != "original":
        raise ValueError("Expected the original executable.")
    result = bytearray(original)
    for offset, before, after in PATCHES:
        if result[offset : offset + len(before)] != before:
            raise ValueError(f"Unexpected instructions at file offset {offset:#x}.")
        result[offset : offset + len(before)] = after
    if sha256(result) != PATCHED_SHA256:
        raise ValueError("Patched executable failed its output hash check.")
    return bytes(result)


def replace_file(path, data):
    # Prepare the complete image beside the target before replacing it. A locked
    # Windows executable fails replacement while leaving the old image intact.
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


def patch_executable(path, *, restore=False, check=False):
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
        "exe",
        type=Path,
        help="Staged WheresMyMickey.Windows8.exe (close the game first)",
    )
    action = parser.add_mutually_exclusive_group()
    action.add_argument(
        "--check",
        action="store_true",
        help="Identify the executable without changing files",
    )
    action.add_argument(
        "--restore", action="store_true", help="Restore the verified original backup"
    )
    args = parser.parse_args(argv)
    try:
        print(patch_executable(args.exe, restore=args.restore, check=args.check))
        return 0
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
