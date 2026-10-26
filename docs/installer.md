# Installer

`installappx.py` turns a game's `.appxbundle`, `.appx`, or extracted package folder into a portable game folder. See the [Quickstart](../README.md#quickstart) to get started.

## Python

The installer needs **Python 3.8 or later**. On Windows, install it from [python.org](https://www.python.org/downloads/windows/) (the installer adds the `py` launcher) or with `winget install Python.Python.3.13`. On Windows 8/8.1, use a Python 3 release that still supports your Windows version.

## What it asks

1. The folder to install into. The default is beside the package, named after the game's package name.
2. A VCLibs package, only if downloading it from the [VCLibs release](https://github.com/yell0wsuit/vclibs-appx/releases/latest) fails.
3. Whether to unlock the trial, complete in-app purchases locally, both, or neither. The choice is saved in `appx-portable.ini`; see [trial override](trial-mode.md) and [offline in-app purchases](stub-iap.md).
4. Whether to write the `00_click2startme` launcher.

Along the way it picks the game package out of a bundle (x86 for a Windows 8/8.1 game; x64 for a Windows 10 game, or x86 on 32-bit Windows; ARM-only bundles are not supported), downloads the VCLibs packages the game needs and the matching release, adds `appx-host.exe` for a managed game, applies the patch for a known game, and on Windows 8/8.1 makes the desktop copy the game needs (see [Troubleshooting](troubleshooting.md)).

## Command-line options

Every question has a command-line option, so an install can run without any prompts:

| Option                           | Does                                                       |
| -------------------------------- | ---------------------------------------------------------- |
| `--dest <folder>`                | Install into this folder                                   |
| `--vclibs <.appx or folder>`     | A VCLibs package; repeat it for several                    |
| `--store none\|trial\|iap\|both` | Store overrides to turn on                                 |
| `--launcher`, `--no-launcher`    | Write the launcher or not (the default is to write it)     |
| `--build <folder>`               | Use these DLLs instead of downloading a release            |
| `--release <tag>`                | Download this release instead of the latest                |
| `--no-patch`                     | Skip the known-game patch                                  |
| `--desktop-copy`                 | Make the Windows 8/8.1 desktop copy on any Windows version |
| `--yes`                          | Never ask; fail when something required is missing         |

## Updating

Run it again on the same folder to update after a new release: it replaces the game files and DLLs and reapplies the patch, and keeps saves, `appx-portable.ini` and logs.

## Without the launcher

Double-click the game's executable itself (`appx-host.exe` for managed games; the `.desktop.exe` copy on Windows 8/8.1). The Store options in `appx-portable.ini` apply either way.

## Use your own build

[Build Appx Portable](building.md#windows), then pass the build folder:

```powershell
py installappx.py "C:\Downloads\CutTheRope2.appxbundle" --build build\windows-x86
```

Use `build\windows-x64` for an x64 game. From a repository checkout, the installer applies patches from the local `patch/` folder instead of downloading them.

## macOS / Wine (experimental)

[Cross-compile](building.md#macos-cross-compilation), then install and launch:

```sh
python3 installappx.py '/path/to/game.appxbundle' --build build/macos-x86
'/path/to/Example.Game/00_click2startme.sh'
```

The launcher uses `wine` from your `PATH`, or Wine Devel in `~/Applications`; set `WINE` to use another, and `WINEPREFIX` for a separate prefix. Wine launches always write a log.
