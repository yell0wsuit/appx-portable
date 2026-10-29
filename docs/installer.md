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
| `--dependencies <.appx or folder>` | A VCLibs or .NET Native dependency; repeat it for several |
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

Double-click the game's executable itself (`appx-host.exe` for managed games, `appx-native-host.exe` for native XAML games; the `.desktop.exe` copy on Windows 8/8.1). The Store options in `appx-portable.ini` apply either way.

.NET Native XAML games such as Deus Ex GO start through `appx-native-host.exe` on Windows. The installer detects the forwarding executable and stages this launcher automatically. It uses dependencies supplied with `--dependencies`, matching installed Windows packages, or the [.NET Native dependency release](https://github.com/yell0wsuit/vclibs-appx/releases/tag/v2), in that order. Downloads are checked against pinned SHA-256 hashes. The supported packages are .NET Native Framework 1.3 and Runtime 1.4.

Native C++/CX XAML games, such as Unity games built with IL2CPP for UWP, also start through `appx-native-host.exe` on Windows when nothing they import from their folder is one of the proxy DLLs. The installer detects them by their compiled XAML (`.xbf` files, loose or in `resources.pri`). It writes `Game.exe.manifest` beside the executable, because Windows allows XAML Islands only in a process whose manifest declares Windows 10 version 1903 as tested, and updates the executable's timestamp so Windows reads the new manifest. Windows ignores that file if the executable embeds a manifest of its own; the installer then warns.

## Use your own build

[Build Appx Portable](building.md#windows), then pass the build folder:

```powershell
py installappx.py "C:\Downloads\CutTheRope2.appxbundle" --build build\windows-x86
```

Use `build\windows-x64` for an x64 game. From a repository checkout, the installer applies patches from the local `patch/` folder instead of downloading them.

## macOS / Wine (experimental)

Run the installer with the package:

```sh
python3 installappx.py '/path/to/game.appxbundle'
```

Then double-click `00_click2startme.command` in the game folder, which starts the game from Terminal. On Linux the launcher is `00_click2startme.sh`.

On macOS the installer also sets up what Wine needs, when it is missing:

- **Wine Devel** (Gcenx's 11.18 build, about 190 MB), downloaded into `~/Applications` after checking its SHA-256. This needs no administrator password, so `--yes` installs it too.
- **Rosetta 2**, on Apple silicon, to run Wine's Intel code.
- **GStreamer**, which Wine needs for games that play audio or video through Media Foundation. The installer downloads the newest stable runtime `.pkg` from [gstreamer.freedesktop.org](https://gstreamer.freedesktop.org/download/), checks it against the checksum published beside it, and installs it into `/Library/Frameworks` with `sudo installer`. After GStreamer is installed or updated, it rebuilds its plugin cache, which takes about a minute on Apple silicon. For a game that plays audio or video (its executable uses Media Foundation or DirectShow, or its pages use a `MediaElement`), the launcher builds the cache before the game starts rather than stalling it the first time it plays something; once the cache is current, the check takes a moment.

Rosetta 2 and GStreamer ask for your administrator password, so the installer asks before either, and `--yes` only prints how to install them.

The release includes a [patched Wine graphics stack](building.md#wine-graphics-macos) beside the DLLs, which the installer adds under Wine; without it most games render black on macOS. With it, the launcher draws through Vulkan on Metal; set `WINE_D3D_CONFIG=renderer=gl` to use OpenGL instead. With a local build, [cross-compile](building.md#macos-cross-compilation) with the preset matching the game's architecture, run `scripts/build-wined3d.sh`, and pass `--build build/macos-x86` or `--build build/macos-x64`.

Managed (.NET) games, which run through `appx-host.exe`, do not work under Wine yet; see [managed games under Wine](islands.md#wine).

The launcher uses `wine` from your `PATH`, or Wine Devel in `~/Applications`; set `WINE` to use another, and `WINEPREFIX` for a separate prefix. Wine launches always write a log.
