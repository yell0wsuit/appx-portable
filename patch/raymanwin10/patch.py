"""Add portable caption bindings to Rayman Fiesta Run Windows 10 Edition 1.0.23.0."""

import argparse
import os
from pathlib import Path
import sys
import tempfile
from xml.etree import ElementTree

IDENTITY = ("Ubisoft.RaymanFiestaRunWindows10Edition", "1.0.23.0")
# The package embeds this XAML in resources.pri; a loose file beside the game
# supplies the same connection IDs without changing PRI or executable bytes.
ASSET = "CustomTitleBar.xaml"
SOURCE = Path(__file__).resolve().with_name(ASSET)


def check_identity(game):
    manifest = ElementTree.parse(game / "AppxManifest.xml")
    # By local name: Windows 8, 8.1 and 10 manifests use different namespaces.
    identity = next(
        (e for e in manifest.iter() if e.tag.rsplit("}", 1)[-1] == "Identity"), None
    )
    found = (
        (identity.get("Name"), identity.get("Version"))
        if identity is not None
        else None
    )
    if found != IDENTITY:
        raise ValueError(
            "This caption patch supports Rayman Fiesta Run Windows 10 Edition 1.0.23.0."
        )


def identify(path, expected):
    if not path.exists():
        return "original"
    if path.read_bytes() == expected:
        return "patched"
    raise ValueError(f"Unexpected {path.name}; refusing to change it: {path}")


def write_file(path, data):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=path.parent, prefix=path.name + ".", suffix=".tmp", delete=False
        ) as output:
            temporary = Path(output.name)
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def patch_game(game, *, restore=False, check=False):
    game = Path(game).resolve(strict=True)
    check_identity(game)
    path = game / ASSET
    expected = SOURCE.read_bytes()
    state = identify(path, expected)
    if check:
        return f"{game}: {state}"
    if restore:
        if state == "original":
            return f"Already original: {game}"
        path.unlink()
        return f"Removed portable caption bindings: {path}"
    if state == "patched":
        return f"Already patched: {game}"
    write_file(path, expected)
    return (
        f"Prepared portable caption bindings: {path}\nThe game executable is unchanged."
    )


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "game", type=Path, help="Staged Rayman Fiesta Run folder (close the game first)"
    )
    action = parser.add_mutually_exclusive_group()
    action.add_argument(
        "--check", action="store_true", help="Report whether the patch is applied"
    )
    action.add_argument(
        "--restore", action="store_true", help="Remove the portable caption bindings"
    )
    args = parser.parse_args(argv)
    try:
        print(patch_game(args.game, restore=args.restore, check=args.check))
        return 0
    except (OSError, ValueError, ElementTree.ParseError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
