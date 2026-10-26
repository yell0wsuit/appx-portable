# Manual installation

The same steps as [`installappx.py`](installer.md), by hand, without Python.

## 1. Extract the game package

Obtain the game's x86 or x64 package. An `.appxbundle` is a ZIP archive holding one `.appx` per architecture; open it and extract the x86 `.appx` for a Windows 8/8.1 game, or the x64 one for a Windows 10 game (x86 on 32-bit Windows). Then extract that `.appx` (also a ZIP archive) into a folder such as `C:\Games\CutTheRope2`. Rename either archive to `.zip` if your extractor requires it. The folder should contain the game's executable and its `AppxManifest.xml`; `AppxSignature.p7x`, `AppxBlockMap.xml`, `[Content_Types].xml` and `AppxMetadata` are not needed.

## 2. Get the Visual C++ runtime

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

## 3. Add the Appx Portable DLLs

Download `appx-portable-<version>-x86.zip` (or `-x64.zip` for an x64 game) from the latest [release](https://github.com/yell0wsuit/appx-portable/releases) and extract its DLLs beside the game's executable. For a managed (C#) game, also copy `appx-host.exe` from the x86 zip; the game starts through it.

## 4. Settings and patches

Saves go to `SaveData` beside the game, and the Store overrides are off. To change either, create `appx-portable.ini` beside the game; see [saves and window settings](configuration.md), [trial override](trial-mode.md) and [offline in-app purchases](stub-iap.md).

Games marked 🩹 in [tested games](games.md) need their patch from [`patch/`](../patch/README.md), run with Python. On Windows 8/8.1, the game needs a desktop copy of its executable; `installappx.py --desktop-copy` makes it (see [Troubleshooting](troubleshooting.md)).

## 5. Play

Double-click the game's executable (`Game.exe` for Cut the Rope 2; the manifest's `Executable` in general), or `appx-host.exe` for a managed game.
