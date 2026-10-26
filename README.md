# Appx Portable

**Run Windows 8, 8.1 and UWP Store games as ordinary portable desktop games.**

No installation · No Store account · No developer mode · No package registration

## Overview

Store games expect to run inside an installed package, with an app container, a package identity and the Store's app model around them. Appx Portable supplies those Windows Runtime pieces _in-process_, so the game runs from a plain folder.

The installer changes no game file except through a known game's patch. Game-specific workarounds live under [`patch/`](patch/README.md), and the installer applies the one for the exact version it supports.

> [!NOTE]
> This repository contains no games and no Microsoft runtime files. You supply your own game packages.

## Features

- 🪟 **Desktop window hosting.** The app lifecycle, `CoreWindow` and XAML swap chains run inside a normal, resizable desktop window, with borderless fullscreen.
- 🖱️ **Input.** Mouse and keyboard events are delivered as the game expects.
- 💾 **Portable saves.** Saves and settings live beside the game by default, or in a folder you choose.
- 🧩 **Store and package APIs.** `ApplicationData`, `Package`, `CurrentApp`, the Share and Settings charms, and live tiles are provided locally.
- 📴 **Offline service stand-ins.** The Xbox LIVE SDK and similar shut-down services report "no connection" cleanly instead of crashing. Nothing is sent over the network.
- 🧠 **Managed (C#) games.** `appx-host.exe` hosts .NET games through CoreWindow or XAML Islands, selected automatically.
- 🏗️ **x86 and x64.** Windows 8/8.1 x86 games, plus experimental support for x64 Windows 10 UWP games.
- 🍷 **Wine (experimental).** Cross-compile and run on macOS through Wine.

## Tested games

| Game                                 | Version     | Arch | Status          |
| ------------------------------------ | ----------- | ---- | --------------- |
| Cut the Rope 2                       | 1.0.0.46    | x86  | ✅ Playable     |
| Fruit Ninja                          | 1.8.1.66    | x86  | ✅ Playable     |
| Where's My Water?                    | 1.13.1.3    | x86  | ✅ Playable     |
| Bejeweled LIVE                       | 4.0.0.10    | x86  | ✅ Playable     |
| Gravity Guy                          | 1.0.1.2     | x86  | ✅ Playable     |
| Where's My Mickey?                   | 1.1.0.30    | x86  | 🩹 Needs patch  |
| Microsoft Sudoku (2018)              | 1.7.10190.0 | x86  | 🩹 Needs patch  |
| Rayman Fiesta Run Windows 10 Edition | 1.0.23.0    | x64  | 🩹 Needs patch  |

See [tested games](docs/games.md) for package names, patches, and what was verified.

## Requirements

- **Windows:** managed XAML games need Windows 10 version 1903 or later and the .NET Framework 4.x.
- **Python 3.8 or later**, for the installer and the game patches; see [installing Python](docs/installer.md#python). [Manual installation](docs/manual-install.md) needs no Python.
- **macOS / Wine (experimental):** Wine and `python3`.

## Quickstart

1. Get the game's `.appxbundle` or `.appx`; there is no need to extract it. An already-extracted package folder works too.
2. Download `installappx.py` from the latest [release](https://github.com/yell0wsuit/appx-portable/releases), and double-click it or run it with the package:

   ```powershell
   py installappx.py "C:\Downloads\CutTheRope2.appxbundle"
   ```

3. Double-click `00_click2startme.bat` in the game folder to play.

The installer downloads the Visual C++ runtime and the Appx Portable DLLs the game needs, and asks where to install, which Store options to turn on, and whether to write the launcher. Run it again on the same folder to update; saves and settings are kept. See the [installer guide](docs/installer.md) for its command-line options, using your own build, and macOS/Wine.

## Documentation

- [Tested games](docs/games.md)
- [Installer guide](docs/installer.md)
- [Manual installation](docs/manual-install.md)
- [Saves and window settings](docs/configuration.md)
- [Trial override](docs/trial-mode.md) and [offline in-app purchases](docs/stub-iap.md)
- [Managed games and XAML Islands](docs/islands.md)
- [Game patches](patch/README.md)
- [Troubleshooting](docs/troubleshooting.md)
- [How it works](docs/how-it-works.md)
- [Building](docs/building.md) and [compatibility structure](docs/compatibility.md)

## License

appx-portable is licensed under the [MIT License](LICENSE). Games and Microsoft runtime files retain their own licenses.
