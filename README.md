# Appx Portable

**Run Windows 8, 8.1 and UWP Store games as ordinary portable desktop games.**

No installation · No Store account · No developer mode · No package registration

## Overview

Store games expect to run inside an installed package, with an app container, a package identity and the Store's app model around them. Appx Portable supplies those Windows Runtime pieces _in-process_, so the game runs from a plain folder:

1. Run `installappx.py` on the game's `.appxbundle` or `.appx`.
2. It extracts the game and adds the Visual C++ runtime and the Appx Portable DLLs.
3. Launch the game.

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

| Game                                                                                             | Version     | Arch | Status          | Notes                                                                                                                                                                                    |
| ------------------------------------------------------------------------------------------------ | ----------- | ---- | --------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Cut the Rope 2**<br><sub>`ZeptoLabUKLimited.CuttheRope2`</sub>                                 | 1.0.0.46    | x86  | ✅ Playable     | Gameplay, saves, resizable window and borderless fullscreen                                                                                                                              |
| **Fruit Ninja**<br><sub>`HalfbrickStudiosPtyLtd.FruitNinja`</sub>                                | 1.8.1.66    | x86  | ✅ Playable     | Menus, mouse slicing, saves, resizable window. Xbox LIVE shows as disconnected                                                                                                           |
| **Where's My Water?**<br><sub>`Disney.WheresMyWater`</sub>                                       | 1.13.1.3    | x86  | ✅ Playable     | Menus, mouse dragging, music and sound; first level completed. Save restoration unverified                                                                                               |
| **Where's My Mickey?**<br><sub>`Disney.WheresMyMickey`</sub>                                     | 1.1.0.30    | x86  | 🩹 Needs patch  | [AMPS startup EXE patch](patch/wheresmymickey/README.md). Intro and mouse input; first level completed. Save restoration unverified                                                      |
| **Microsoft Sudoku**<br><sub>`Microsoft.MicrosoftSudoku`</sub>                                   | 1.7.10190.0 | x86  | 🩹 Needs patch  | [Video-preloader asset patch](patch/mssudoku/README.md). Menu, tutorial and mouse number entry, through `appx-host.exe`                                                                  |
| **Bejeweled LIVE**<br><sub>`PopCap.BejweledLIVE`</sub>                                           | 4.0.0.10    | x86  | 🟡 Partial      | Classic board swaps and scoring, through `appx-host.exe`. [Dark theme patch](patch/bejeweled/README.md) recommended; see [managed hosting](docs/islands.md) for settings and save limits |
| **Gravity Guy**<br><sub>`MiniclipSA.GravityGuy`</sub>                                            | 1.0.1.2     | x86  | 🟡 Partial      | Story gameplay with mouse input, through `appx-host.exe`. Other modes and save restoration unverified                                                                                    |
| **Rayman Fiesta Run Windows 10 Edition**<br><sub>`Ubisoft.RaymanFiestaRunWindows10Edition`</sub> | 1.0.23.0    | x64  | 🧪 Experimental | [Caption bindings patch](patch/raymanwin10/README.md). World map renders and mouse input enters a level; level completion and saves unverified                                           |

<sub>✅ Playable · 🟡 Partial: core gameplay checked, some features unverified or limited · 🩹 Needs patch: playable with an optional patch from [`patch/`](patch/README.md) · 🧪 Experimental: early support</sub>

## Requirements

- **Windows:** managed XAML games need Windows 10 version 1903 or later and the .NET Framework 4.x.
- **Python 3**, for the [Quickstart](#quickstart) installer and the game patches. On Windows, install it from [python.org](https://www.python.org/downloads/windows/) (the installer adds the `py` launcher) or with `winget install Python.Python.3.13`. On Windows 8/8.1, use a Python 3 release that still supports your Windows version; the installer needs 3.8 or later. [Manual installation](#manual-installation) needs no Python.
- **macOS / Wine (experimental):** Wine and `python3`.
- **The Appx Portable DLLs** come from the latest [release](https://github.com/yell0wsuit/appx-portable/releases). To use your own build, see the [building guide](docs/building.md) and [Use your own build](#use-your-own-build).

## Quickstart

1. Get the game's `.appxbundle` or `.appx`; there is no need to extract it. An already-extracted package folder works too.
2. Get the [Visual C++ runtime](#2-get-the-visual-c-runtime) packages the game needs. The installer says which, with their download links.
3. Download `installappx.py` from the latest [release](https://github.com/yell0wsuit/appx-portable/releases), and double-click it or run it with the package:

   ```powershell
   py installappx.py "C:\Downloads\CutTheRope2.appxbundle"
   ```

4. Double-click `00_click2startme.bat` in the game folder to play.

The installer asks for anything it needs:

1. The folder to install into. The default is beside the package, named after the game's package name.
2. A VCLibs package, only if downloading it from the [VCLibs release](https://github.com/yell0wsuit/vclibs-appx/releases/latest) fails.
3. Whether to unlock the trial, complete in-app purchases locally, both, or neither. The choice is saved in `appx-portable.ini`; see [trial override](docs/trial-mode.md) and [offline in-app purchases](docs/stub-iap.md).
4. Whether to write the `00_click2startme` launcher.

Along the way it picks the game package out of a bundle (x86 for a Windows 8/8.1 game; x64 for a Windows 10 game, or x86 on 32-bit Windows; ARM-only bundles are not supported), downloads the VCLibs packages the game needs and the matching release, adds `appx-host.exe` for a managed game, applies the patch for a known game, and on Windows 8/8.1 makes the desktop copy the game needs (see [Troubleshooting](#troubleshooting)).

Every question has a command-line option, so an install can run without any prompts:

| Option | Does |
| --- | --- |
| `--dest <folder>` | Install into this folder |
| `--vclibs <.appx or folder>` | A VCLibs package; repeat it for several |
| `--store none\|trial\|iap\|both` | Store overrides to turn on |
| `--launcher`, `--no-launcher` | Write the launcher or not (the default is to write it) |
| `--build <folder>` | Use these DLLs instead of downloading a release |
| `--release <tag>` | Download this release instead of the latest |
| `--no-patch` | Skip the known-game patch |
| `--desktop-copy` | Make the Windows 8/8.1 desktop copy on any Windows version |
| `--yes` | Never ask; fail when something required is missing |

Run it again on the same folder to update after a new release: it replaces the game files and DLLs and reapplies the patch, and keeps saves, `appx-portable.ini` and logs.

Without the launcher, double-click the game's executable itself (`appx-host.exe` for managed games; the `.desktop.exe` copy on Windows 8/8.1). The Store options in `appx-portable.ini` apply either way.

### Use your own build

[Build Appx Portable](docs/building.md#windows), then pass the build folder:

```powershell
py installappx.py "C:\Downloads\CutTheRope2.appxbundle" --build build\windows-x86
```

Use `build\windows-x64` for an x64 game. From a repository checkout, the installer applies patches from the local `patch/` folder instead of downloading them.

### macOS / Wine (experimental)

[Cross-compile](docs/building.md#macos-cross-compilation), then install and launch:

```sh
python3 installappx.py '/path/to/game.appxbundle' --build build/macos-x86
'/path/to/Example.Game/00_click2startme.sh'
```

The launcher uses `wine` from your `PATH`, or Wine Devel in `~/Applications`; set `WINE` to use another, and `WINEPREFIX` for a separate prefix. Wine launches always write a log.

## Manual installation

The same steps by hand, without Python.

### 1. Extract the game package

Obtain the game's x86 or x64 package. An `.appxbundle` is a ZIP archive holding one `.appx` per architecture; open it and extract the x86 `.appx` for a Windows 8/8.1 game, or the x64 one for a Windows 10 game (x86 on 32-bit Windows). Then extract that `.appx` (also a ZIP archive) into a folder such as `C:\Games\CutTheRope2`. Rename either archive to `.zip` if your extractor requires it. The folder should contain the game's executable and its `AppxManifest.xml`; `AppxSignature.p7x`, `AppxBlockMap.xml`, `[Content_Types].xml` and `AppxMetadata` are not needed.

### 2. Get the Visual C++ runtime

Store games use the Store's own Visual C++ runtime (VCLibs), which a plain desktop has no copy of. The game's `AppxManifest.xml` names the versions it needs:

```powershell
Select-String -Path 'C:\Games\CutTheRope2\AppxManifest.xml' -Pattern 'Microsoft\.VCLibs\.\d+\.00' | ForEach-Object { $_.Matches.Value }
```

| Dependency                                       | DLLs                                                                                   |
| ------------------------------------------------ | -------------------------------------------------------------------------------------- |
| `Microsoft.VCLibs.110.00` (Windows 8.0 games)    | `msvcr110.dll`, `msvcp110.dll`, `vccorlib110.dll`                                      |
| `Microsoft.VCLibs.120.00` (Windows 8.1 games)    | `msvcr120_app.dll`, `msvcp120_app.dll`, `vccorlib120_app.dll`                          |
| `Microsoft.VCLibs.140.00` (Windows 10 UWP games) | `vcruntime140_app.dll`, `msvcp140_app.dll`, `vccorlib140_app.dll`, `concrt140_app.dll` |

Download the packages with the game's architecture from the [VCLibs release](https://github.com/yell0wsuit/vclibs-appx/releases/latest). The installer uses `11.0.51106.1`, `12.0.21005.1` and `14.0.30704.0`; check the game's manifest for its minimum version. These provide the UWP `_app.dll` files; `Microsoft.VCLibs.140.00.UWPDesktop` provides a different runtime. An x64 game needs the x64 package.

The release notes list each package's SHA-256. For a package from anywhere other than Microsoft, check that Microsoft signed it; `Status` should be `Valid` and the signer `CN=Microsoft Corporation, …`:

```powershell
Get-AuthenticodeSignature .\Microsoft.VCLibs.110.00_11.0.51106.1_x86__8wekyb3d8bbwe.Appx | Format-List Status, SignerCertificate
```

If the framework is installed on your PC (any Store app that uses it installs it), its DLLs are also in `C:\Program Files\WindowsApps\Microsoft.VCLibs.110.00_<version>_x86__8wekyb3d8bbwe` (or `120.00`/`140.00`, with the matching architecture); `Get-AppxPackage Microsoft.VCLibs.140.00 | Select Architecture,InstallLocation` lists them. The installer accepts that folder instead of an `.appx`.

Each VCLibs `.appx` is a ZIP archive with its DLLs at the root. Copy the DLLs listed above, with the same architecture as the game, beside the game's executable. For VCLibs 140, also copy any other `*140*_app.dll` the package has.

### 3. Add the Appx Portable DLLs

Download `appx-portable-<version>-x86.zip` (or `-x64.zip` for an x64 game) from the latest [release](https://github.com/yell0wsuit/appx-portable/releases) and extract its DLLs beside the game's executable. For a managed (C#) game, also copy `appx-host.exe` from the x86 zip; the game starts through it.

### 4. Settings and patches

Saves go to `SaveData` beside the game, and the Store overrides are off. To change either, create `appx-portable.ini` beside the game; see [Saves and window settings](#saves-and-window-settings), [trial override](docs/trial-mode.md) and [offline in-app purchases](docs/stub-iap.md).

Games marked 🩹 above need their patch from [`patch/`](patch/README.md), run with Python. On Windows 8/8.1, the game needs a desktop copy of its executable; `installappx.py --desktop-copy` makes it (see [Troubleshooting](#troubleshooting)).

### 5. Play

Double-click the game's executable (`Game.exe` for Cut the Rope 2; the manifest's `Executable` in general), or `appx-host.exe` for a managed game.

## Saves and window settings

Saves go to `SaveData` beside the game executable. To change that, edit `appx-portable.ini` in the game folder:

```ini
[storage]
SaveLocation=game
```

| Value            | Save folder                                       |
| ---------------- | ------------------------------------------------- |
| `game` (default) | `SaveData` beside the game executable             |
| `documents`      | `Documents\appx-portable\<package>`               |
| `appdata`        | `%LOCALAPPDATA%\appx-portable\<package>`          |
| A folder path    | That folder; relative paths start beside the game |

`<package>` is the identity name from the game's manifest, so games never share saves. If the folder cannot be written, saves go to `documents`, then `appdata`. Changing the location does not move existing saves.

Press **F11** or **Alt+Enter** to toggle borderless fullscreen. Window size, position, and mode are remembered in `Local\window.ini` inside the save folder; delete it with the game closed to reset them. The game keeps running when it loses focus and pauses when minimized. Closing the window suspends the game, which saves, and then ends it.

## Troubleshooting

- **Need a log:** start the game with `--log` (for example `.\Game.exe --log` in PowerShell, from the game folder). The log is `appx-portable.log` beside the executable, and the two previous runs are kept as `appx-portable.1.log` and `appx-portable.2.log`. Its first lines name the appx-portable version and the package it found. `--trace` logs every Windows Runtime class the game activates; `--diagnostics` adds crash details and checks the heap for damage as the game runs (slower). Both imply `--log`.
- **The installer reports a missing file:** check that the package is the complete `.appx` or `.appxbundle` (or a complete extraction, with `AppxManifest.xml`), and that each VCLibs `.appx` or folder you give has the DLLs listed in step 2.
- **The game fails to start or crashes after loading:** check the log. appx-portable decodes percent-encoded file names itself at startup, so the game folder must be writable; installing with `installappx.py` avoids the problem.
- **Windows 8/8.1 says the app can only run in an app container:** Windows 8/8.1 refuse to start a Store executable or VCLibs DLL marked for an app container outside a package. On those versions the installer creates `Game.desktop.exe` beside the original with only that flag cleared, clears it in the game folder's VCLibs DLLs, and points the launcher at the desktop copy. Pass `--desktop-copy` to do the same on another Windows version. Gameplay on Windows 8/8.1 itself remains unverified.
- **Check which version is installed:** right-click `appxportable.dll` and open **Properties → Details**. **Product version** names the release and the commit it was built from. All appx-portable DLLs in a game folder should show the same version.

## How it works

The proxy DLLs load `appxportable.dll` while the game starts. It hooks Windows Runtime activation (`RoGetActivationFactory`) and, for calls from the game's own modules, answers with its own classes where Windows cannot serve an unpackaged process: the app lifecycle (`CoreApplication`, both for XAML games and for games that run their own `IFrameworkView` loop), a `CoreWindow` hosted on a normal window with its mouse and keyboard events, the XAML objects games draw their swap chain into, `ApplicationData`, `Package`, `CurrentApp`, the Share and Settings charms, live tiles, and offline stand-ins for the Xbox LIVE SDK and similar service libraries. Everything else goes to Windows unchanged. Classes a package registers for itself in its manifest are loaded from their DLLs in the game folder, as an installed package would. Swap chains made for a XAML panel or a `CoreWindow` are redirected to a child window of the game's desktop window.

Closing the window ends a game the way Windows 8 ends a Store app: the game is suspended, so it saves, and the process then ends. Store games never ran their own teardown on Windows, and some (Fruit Ninja, Cut the Rope 2) crash in it.

See the [building guide](docs/building.md) for compilation, tests, and code generation.

## License

appx-portable is licensed under the [MIT License](LICENSE). Games and Microsoft runtime files retain their own licenses.
