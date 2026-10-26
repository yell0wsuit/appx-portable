# Microsoft Sudoku video-preloader workaround

For Microsoft.MicrosoftSudoku **1.7.10190.0 x86** (the November 2018 package).
Use the current appx-portable runtime and launch through `appx-host.exe`.

The game's compiled video-loading popup sets `IsOpen=True` before loading its
child. System XAML Islands rejects this with `XamlParseException` at
`Popup.Child`, and a partially initialized popup can subsequently crash the
game. A standalone system-XAML reproduction confirmed this parser limitation.

This patch changes one byte in
`Arkadium.Advertisement/Video/Controls/VideoAdsPreloader.xbf`, keeping that
loading overlay closed. It preserves the executable, game logic, license,
puzzle assets, and saves. This does not enable online advertising services or
change premium status. The runtime and setup do not apply it automatically.

`installappx.py` applies this patch automatically for this exact version. The commands below check, reapply, or restore it by hand.

With the game closed, run from the repository root:

```powershell
py patch/mssudoku/patch.py 'D:\Programs\appx\MSSudoku2018'
```

The patch accepts only the exact supported asset, verifies the output, and
keeps `VideoAdsPreloader.xbf.islands-original` beside it. Repeated application
is safe. To inspect or restore:

```powershell
py patch/mssudoku/patch.py 'D:\Programs\appx\MSSudoku2018' --check
py patch/mssudoku/patch.py 'D:\Programs\appx\MSSudoku2018' --restore
```

| Asset | SHA-256 |
| --- | --- |
| Original | `ec0b514054475dad3412c368bb4efeb9cc00f9a9c9d4597cd44311ec9320f751` |
| Patched | `5295665cbf6bda6443b7bbdaec1e2b4a55acd21a03fb00c41b9f4d974724c82c` |

At file offset `0x73D`, the XBF v1 `Popup.IsOpen` value changes from
BooleanTrue (`02`) to BooleanFalse (`01`). File length, metadata offsets,
connection IDs, and other properties remain intact. No game assets are
distributed with the patch.

Validated on 2026-10-25: menu startup, opening a Very Easy puzzle, dismissing
both help overlays, mouse selection and number entry, and an advancing timer.
Online challenges, Xbox sign-in, and longer sessions remain unverified.
