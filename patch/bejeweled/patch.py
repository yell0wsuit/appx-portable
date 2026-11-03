"""Restore Bejeweled LIVE 4.0.0.10's dark theme before static resources load."""

import argparse
import hashlib
import os
from pathlib import Path
import sys
import tempfile

ASSET = Path("App.xaml")
ORIGINAL_SHA256 = "f1c4c0f91d60e6c9af70ea0422fe6a80f697a2690e4e43e51f31ea4eb3f09ea7"
PATCHED_SHA256 = "0efd317b23d328be99c5281fd0809470cb050ff25d91f48529cbfd1e71a58d84"
BACKUP_SUFFIX = ".islands-original"

WPF_ASSET = Path("Common/BejeweledStyles.xaml")
WPF_ORIGINAL_SHA256 = "fbc3127abd04572a9f0e2768463a9d2aed775e860cd7555f1ced9a2878c05b26"
WPF_PATCHED_SHA256 = "f2ec4e3b056dde0a413baa6e3d5028d818b83db9fe540df8e3a24d4e04a4e8ca"
WPF_REFERENCES = {'ButtonBackgroundBrush': 'ButtonBackgroundThemeBrush', 'ControlForegroundBrush': 'ApplicationForegroundThemeBrush', 'ControlNormalStrokeBrush': 'ButtonBorderThemeBrush', 'ControlBorderThickness': 'ButtonBorderThemeThickness', 'ContentFontFamily': 'ContentControlThemeFontFamily', 'ContentFontSize': 'ControlContentThemeFontSize', 'ControlPressedTextBrush': 'ApplicationPressedForegroundThemeBrush'}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def identify(data, *, wpf=False):
    original = WPF_ORIGINAL_SHA256 if wpf else ORIGINAL_SHA256
    patched = WPF_PATCHED_SHA256 if wpf else PATCHED_SHA256
    digest = sha256(data)
    if digest == original:
        return "original"
    if digest == patched:
        return "patched"
    raise ValueError(f"Unsupported Bejeweled {WPF_ASSET if wpf else ASSET} SHA-256: {digest}")


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


def patch_file(path, *, restore=False, check=False, wpf=False):
    original = WPF_ORIGINAL_SHA256 if wpf else ORIGINAL_SHA256
    patched = WPF_PATCHED_SHA256 if wpf else PATCHED_SHA256
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    state = identify(data, wpf=wpf)
    if check:
        return f"{path}: {state}"
    backup = path.with_name(path.name + (".wpf-original" if wpf else BACKUP_SUFFIX))
    if restore:
        if state == "original":
            return f"Already original: {path}"
        saved = backup.read_bytes()
        if sha256(saved) != original:
            raise ValueError(f"Backup hash mismatch; refusing to restore: {backup}")
        replace_file(path, saved)
        return f"Restored: {path}\nBackup retained: {backup}"
    if state == "patched":
        return f"Already patched: {path}"
    if wpf:
        result = data
        for source, target in WPF_REFERENCES.items():
            result = result.replace(("{StaticResource " + source + "}").encode(), ("{StaticResource " + target + "}").encode())
    else:
        result = data.replace(b"<Application", b'<Application RequestedTheme="Dark"', 1)
    if sha256(result) != patched:
        raise ValueError("Patched asset failed its output hash check")
    try:
        with backup.open("xb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
    except FileExistsError as error:
        if sha256(backup.read_bytes()) != original:
            raise ValueError(
                f"Existing backup differs; refusing to overwrite: {backup}"
            ) from error
    replace_file(path, result)
    return f"Patched: {path}\nOriginal backup: {backup}"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "game", type=Path, help="Staged Bejeweled LIVE folder (close the game first)"
    )
    parser.add_argument("--wpf", action="store_true", help="Fix invalid style resource references for the Wine WPF adapter only")
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
        asset = (game / (WPF_ASSET if args.wpf else ASSET)).resolve(strict=True)
        asset.relative_to(game)
        print(patch_file(asset, restore=args.restore, check=args.check, wpf=args.wpf))
        return 0
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
