#!/usr/bin/env python3
"""Start the staged game.

On Windows this runs the game executable directly. Elsewhere it runs
scripts/run.sh, which starts the game under Wine (and always logs, for its
crash relaunch).
"""
import argparse
import pathlib
import subprocess
import sys
import xml.etree.ElementTree as ElementTree

ROOT = pathlib.Path(__file__).resolve().parents[1]
SWITCHES = {
    'log': 'write failures and notes to appx-portable.log beside the game',
    'trace': 'log every traced call as well (implies --log)',
    'capture': 'save the back buffer to appx-frame.bmp on the first and every 120th frame',
    'diagnostics': 'log access violations with stack details (implies --log)',
    'passthrough': "activate the operating system's classes and only log",
}


def game_executable(game):
    """The executable AppxManifest.xml names; Game.exe without a manifest."""
    manifest = game / 'AppxManifest.xml'
    if manifest.is_file():
        # By local name: Windows 8, 8.1 and 10 manifests use different namespaces.
        for element in ElementTree.parse(manifest).iter():
            if element.tag.rsplit('}', 1)[-1] == 'Application' and element.get('Executable'):
                return pathlib.PureWindowsPath(element.get('Executable')).as_posix()
    return 'Game.exe'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--game', type=pathlib.Path, default=ROOT / 'local' / 'game',
                        help='staged game folder (default: local/game)')
    parser.add_argument('--wait', action='store_true', help='wait for the game to exit and return its exit code')
    for name, text in SWITCHES.items():
        parser.add_argument(f'--{name}', action='store_true', help=text)
    args = parser.parse_args()
    game = args.game.resolve()
    flags = [f'--{name}' for name in SWITCHES if getattr(args, name)]
    if sys.platform == 'win32':
        exe = game / game_executable(game)
        if not (exe.is_file() and (game / 'appxportable.dll').is_file()):
            sys.exit(f'{exe.name} and appxportable.dll not found in {game}. Stage the game with scripts/setup.ps1 first.')
        process = subprocess.Popen([str(exe), *flags], cwd=game)
        return process.wait() if args.wait else 0
    return subprocess.call(['bash', str(ROOT / 'scripts' / 'run.sh'), str(game), *flags])


if __name__ == '__main__':
    sys.exit(main())
