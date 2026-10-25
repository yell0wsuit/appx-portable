# appx-portable

Run Windows 8 and 8.1 Store games as ordinary portable desktop games: extract the `.appx` package into a folder, add the appx-portable runtime, and launch the game. No installation, Store account, developer mode, or package registration is needed. Setup preserves the game executable; optional game-specific patches are applied separately.

Store apps expect to run inside an installed package, with an app container, a package identity, and the Store's app model around them. appx-portable supplies the Windows Runtime pieces a game needs from that environment, in-process: the app lifecycle and window, input, save folders and settings, the Store license, live tiles, and offline stand-ins for services that no longer exist.

Neither games nor Microsoft runtime files are included in this repository.

Optional game-specific workarounds live under [`patch/`](patch/README.md), separate from the core runtime.

Managed XAML games such as **Bejeweled LIVE** use `appx-host.exe`, which setup copies beside the original executable. The launch scripts select it automatically. This hosting mode requires Windows 10 version 1903 or later and the desktop .NET Framework 4.x runtime; Wine support is unverified. The original game executable stays unchanged. See [managed game hosting](docs/islands.md) for status and diagnostics.

## Games

| Game | Package | Status |
| --- | --- | --- |
| Rayman Fiesta Run Windows 10 Edition | `Ubisoft.RaymanFiestaRunWindows10Edition` 1.0.23.0 (x64) | Experimental x64 build and [caption bindings patch](patch/raymanwin10/README.md): world map renders and mouse input enters a level; level completion and saves unverified |
| Cut the Rope 2 | `ZeptoLabUKLimited.CuttheRope2` 1.0.0.46 (x86) | Plays: gameplay, saves, resizable window, borderless fullscreen |
| Fruit Ninja | `HalfbrickStudiosPtyLtd.FruitNinja` 1.8.1.66 (x86) | Plays: menus, slicing with the mouse, saves, resizable window. Xbox LIVE shows as disconnected |
| Gravity Guy | `MiniclipSA.GravityGuy` 1.0.1.2 (x86) | Reaches Story gameplay with mouse input through `appx-host.exe`; Xbox LIVE remains offline. Other modes and save restoration unverified |
| Bejeweled LIVE | `PopCap.BejweledLIVE` 4.0.0.10 (x86) | Classic board mouse swaps and scoring checked through `appx-host.exe`; use the [dark theme patch](patch/bejeweled/README.md) for readable settings; see [managed hosting](docs/islands.md) for settings and save limitations |
| Microsoft Sudoku | `Microsoft.MicrosoftSudoku` 1.7.10190.0 (x86) | Requires the [video-preloader asset patch](patch/mssudoku/README.md). Menu, Very Easy tutorial dismissal, and mouse number entry work through `appx-host.exe` |
| Where's My Water? | `Disney.WheresMyWater` 1.13.1.3 (x86) | Plays: menu clicks, mouse dragging, music and sound effects; first level completed with three ducks. Save restoration unverified |
| Where's My Mickey? | `Disney.WheresMyMickey` 1.1.0.30 (x86) | Requires the [AMPS startup EXE patch](patch/wheresmymickey/README.md). Intro and mouse input work; first level completed with three stars. Save restoration unverified |

Online services are offline: Xbox LIVE for Windows 8 and other game services have shut down, so sign-in, leaderboards and similar features report no connection, and nothing is sent. By default there is no Store license, so a game with a trial mode runs as it would without a purchase. An opt-in [`--stub-iap` mode](docs/stub-iap.md) completes in-app purchases locally. An opt-in [trial override](docs/trial-mode.md) reports a full base-app license for games using the Windows 8/8.1 `CurrentApp` API.

Other games built the same way may work, or fail in a way the log explains. Managed C# games use `appx-host.exe`, with CoreWindow or XAML Islands hosting selected automatically. HTML/JavaScript Store apps cannot be supported.

## Set up and play

### 1. Extract the game package

Obtain the game's x86 package. An `.appxbundle` is a ZIP archive holding one `.appx` per architecture; open it and extract the x86 `.appx`. Then extract that `.appx` (also a ZIP archive) into a folder such as `C:\Games\Source\CutTheRope2`. Rename either archive to `.zip` if your extractor requires it. The folder should contain the game's executable and its `AppxManifest.xml`.

### 2. Get the Visual C++ runtime

The experimental x64 Windows 10 path uses `Microsoft.VCLibs.140.00` of the same architecture as the game. If installed, locate it with `Get-AppxPackage Microsoft.VCLibs.140.00 | Select Architecture,InstallLocation` and pass the matching folder to setup. Setup includes available auxiliary app-runtime DLLs such as `concrt140_app.dll` and `vcruntime140_1_app.dll`. Use `-BuildDirectory build/windows-x64` for an x64 game. Otherwise, download the matching package below.

Store games use the Store's own Visual C++ runtime (VCLibs), which a plain desktop has no copy of. The game's `AppxManifest.xml` names the version it needs. Setup reads it and reports a missing runtime, or check before you start:

```powershell
Select-String -Path 'C:\Games\Source\CutTheRope2\AppxManifest.xml' -Pattern 'Microsoft\.VCLibs\.\d+\.00' | ForEach-Object { $_.Matches.Value }
```

| Dependency | DLLs | Packages |
| --- | --- | --- |
| `Microsoft.VCLibs.110.00` (Windows 8.0 games) | `msvcr110.dll`, `msvcp110.dll`, `vccorlib110.dll` | [Internet Archive copy](https://archive.org/download/vclibs-appxs/Microsoft.VCLibs.APPXs.zip/Microsoft.VCLibs.110.00_11.0.51106.1_x86__8wekyb3d8bbwe.Appx) |
| `Microsoft.VCLibs.120.00` (Windows 8.1 games) | `msvcr120_app.dll`, `msvcp120_app.dll`, `vccorlib120_app.dll` | In Microsoft's [`vclibs_redist_packages.zip`](https://download.microsoft.com/download/5/f/0/5f0f8404-9329-44a9-8176-ed6f7f746f25/vclibs_redist_packages.zip) |
| `Microsoft.VCLibs.140.00` (Windows 10 UWP games) | `vcruntime140_app.dll`, `msvcp140_app.dll`, `vccorlib140_app.dll`, `concrt140_app.dll` | Microsoft-signed packages hosted by VideoLAN: [x86](https://download.videolan.org/pub/videolan/vlc-winrt/3.0.3/VLC.Universal_3.0.3.0_ARM_Test/Dependencies/x86/Microsoft.VCLibs.x86.14.00.appx), [x64](https://download.videolan.org/pub/videolan/vlc-winrt/3.0.3/VLC.Universal_3.0.3.0_ARM_Test/Dependencies/x64/Microsoft.VCLibs.x64.14.00.appx) |

The VCLibs 140 downloads above are version `14.0.25426.0`; check the game's manifest for its minimum version. These provide the UWP `_app.dll` files; `Microsoft.VCLibs.140.00.UWPDesktop` provides a different runtime. Pass the downloaded `.appx` directly to setup with `-RuntimeDirectory`.

Microsoft has no standalone download of VCLibs 110. The Internet Archive copy, `Microsoft.VCLibs.110.00_11.0.51106.1_x86__8wekyb3d8bbwe.appx`, has SHA-256 `22d49fa295486e0955a305bea65cfa4ff5d154d296878a1c577ec5507b602c9c`. For a package from anywhere other than Microsoft, check that Microsoft signed it; `Status` should be `Valid` and the signer `CN=Microsoft Corporation, …`:

```powershell
Get-AuthenticodeSignature .\Microsoft.VCLibs.110.00_11.0.51106.1_x86__8wekyb3d8bbwe.appx | Format-List Status, SignerCertificate
```

If the framework is installed on your PC (any Store app that uses it installs it), its DLLs are also in `C:\Program Files\WindowsApps\Microsoft.VCLibs.110.00_<version>_x86__8wekyb3d8bbwe` (or `120.00`/`140.00`, with the matching architecture). Pass setup the `.appx` itself or a folder with its runtime DLLs, with `-RuntimeDirectory`, or copy the DLLs beside the game.

### 3. Build the DLLs

[Build appx-portable](docs/building.md#windows). The build produces `appx-host.exe`, `appxportable.dll` and the proxy DLLs (`d3dcompiler_47.dll`, `XAudio2_8.dll`, `DWrite.dll`) in `build\windows-x86`. A proxy stands in for a system DLL the game imports from its own folder, loads appx-portable, and forwards every call to the real DLL.

### 4. Stage the game

From the repository root, stage the extracted package into a separate folder:

```powershell
.\scripts\setup.ps1 -SourceDirectory 'C:\Games\Source\CutTheRope2' -Destination 'C:\Games\CutTheRope2'
```

Add `-RuntimeDirectory <.appx or folder>` if the VCLibs DLLs are not beside the game, and `-BuildDirectory <folder>` to use DLLs built elsewhere. Setup copies the package (leaving out its signature and package metadata), adds the runtime and appx-portable DLLs, decodes the percent-encoded file names a raw `.appx` extraction keeps (`%21_Name.xml`, `User%20Controls`), and checks that the game executable is byte-for-byte unchanged. Rerun it after rebuilding; saves and `appx-portable.ini` are kept.

### 5. Play

Double-click the game's executable in the staged folder (`Game.exe` for Cut the Rope 2; the manifest's `Executable` in general). With Python 3, `python scripts/play.py --game C:\Games\CutTheRope2` does the same; `scripts\play.bat` and `scripts\run.ps1` are alternatives.

### macOS / Wine (experimental)

With the same extracted package and runtime DLLs, [cross-compile](docs/building.md#macos-cross-compilation) and stage with Python 3 installed:

```sh
bash scripts/setup.sh '/path/to/extracted-package' local/game build/macos-x86 '/path/to/Microsoft.VCLibs.appx'
python3 scripts/play.py
```

The runtime argument, an `.appx` or a folder with its DLLs, is optional when the DLLs are beside the game. The launcher defaults to Wine Devel in `~/Applications` and keeps its Wine prefix in `local/wine-test`; set `WINE` and `WINEPREFIX` to use others. Wine launches always write a log.

## Saves and window settings

Saves go to `SaveData` beside the game executable. To change that, edit `appx-portable.ini` in the staged folder:

```ini
[storage]
SaveLocation=game
```

| Value | Save folder |
| --- | --- |
| `game` (default) | `SaveData` beside the game executable |
| `documents` | `Documents\appx-portable\<package>` |
| `appdata` | `%LOCALAPPDATA%\appx-portable\<package>` |
| A folder path | That folder; relative paths start beside the game |

`<package>` is the identity name from the game's manifest, so games never share saves. If the folder cannot be written, saves go to `documents`, then `appdata`. Changing the location does not move existing saves.

Press **F11** or **Alt+Enter** to toggle borderless fullscreen. Window size, position, and mode are remembered in `Local\window.ini` inside the save folder; delete it with the game closed to reset them. The game keeps running when it loses focus and pauses when minimized. Closing the window suspends the game, which saves, and then ends it.

## Troubleshooting

- **Need a log:** start the game with `--log` (for example `.\Game.exe --log` in PowerShell, from the game folder). The log is `appx-portable.log` beside the executable, and the two previous runs are kept as `appx-portable.1.log` and `appx-portable.2.log`. Its first lines name the appx-portable version and the package it found. `--trace` logs every Windows Runtime class the game activates; `--diagnostics` adds crash details and checks the heap for damage as the game runs (slower). Both imply `--log`.
- **Setup reports a missing file:** check that the source folder is a complete package extraction (with `AppxManifest.xml`), and that the VCLibs DLLs the manifest depends on are beside it or in the `-RuntimeDirectory` package or folder.
- **The game fails to start or crashes after loading:** check the log. appx-portable decodes percent-encoded file names itself at startup, so the game folder must be writable; staging with setup avoids the problem.
- **Windows 8/8.1 says the app can only run in an app container:** run `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\patch_exe.ps1 -Exe "C:\Games\CutTheRope2\Game.exe"`. It creates `Game.desktop.exe` beside the original with only the executable's AppContainer flag cleared, and never overwrites anything. If the desktop copy then fails with `0xC000A200`, the VCLibs DLLs carry the flag too: add `-IncludeRuntime` to create a `desktop` folder with patched copies of the executable and runtime DLLs. Gameplay on Windows 8/8.1 itself remains unverified.
- **Check which version is installed:** right-click `appxportable.dll` and open **Properties → Details**. **Product version** names the release and the commit it was built from. All appx-portable DLLs in a game folder should show the same version.

## How it works

The proxy DLLs load `appxportable.dll` while the game starts. It hooks Windows Runtime activation (`RoGetActivationFactory`) and, for calls from the game's own modules, answers with its own classes where Windows cannot serve an unpackaged process: the app lifecycle (`CoreApplication`, both for XAML games and for games that run their own `IFrameworkView` loop), a `CoreWindow` hosted on a normal window with its mouse and keyboard events, the XAML objects games draw their swap chain into, `ApplicationData`, `Package`, `CurrentApp`, the Share and Settings charms, live tiles, and offline stand-ins for the Xbox LIVE SDK and similar service libraries. Everything else goes to Windows unchanged. Classes a package registers for itself in its manifest are loaded from their DLLs in the game folder, as an installed package would. Swap chains made for a XAML panel or a `CoreWindow` are redirected to a child window of the game's desktop window.

Closing the window ends a game the way Windows 8 ends a Store app: the game is suspended, so it saves, and the process then ends. Store games never ran their own teardown on Windows, and some (Fruit Ninja, Cut the Rope 2) crash in it.

See the [building guide](docs/building.md) for compilation, tests, and code generation.

## License

appx-portable is licensed under the [MIT License](LICENSE). Games and Microsoft runtime files retain their own licenses.
