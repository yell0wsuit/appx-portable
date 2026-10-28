# Trial override

Windows 8/8.1 games ask `Windows.ApplicationModel.Store.CurrentApp.LicenseInformation` whether they are licensed. By default the shim reports no license, so a game with a trial mode runs as it would without a purchase. The `--unlock-trial` switch reports a full base-app license instead. It is off by default, works offline, and does not create a purchase or restore online services.

## Turn it on

Rerun [`installappx.py`](../README.md#quickstart) so the game folder has the current `appxportable.dll`. Its Store question turns this on in `appx-portable.ini`; otherwise, pass the switch to the game executable named in `AppxManifest.xml`:

```powershell
C:\Games\MyGame\Game.exe --unlock-trial
```

On macOS / Wine, `/path/to/game/00_click2startme.command --unlock-trial` (`.sh` on Linux) passes the switch through.

For double-click launches, set it in `appx-portable.ini` beside the game executable (`installappx.py` writes this when you choose to unlock the trial):

```ini
[options]
UnlockTrial=1
```

Alternatively, add `--unlock-trial` after the executable in a shortcut's Target, or set the variable for your user account, which applies to every staged game started afterwards and overrides the ini:

```powershell
setx APPX_PORTABLE_UNLOCK_TRIAL 1
```

To remove it again, run `reg delete HKCU\Environment /v APPX_PORTABLE_UNLOCK_TRIAL /f`. The license is read once at startup, so restart the game after changing either.

The override has no effect with `--passthrough`, which uses Windows' own Store classes.

## What changes

| `LicenseInformation` | Default | `--unlock-trial` |
| --- | --- | --- |
| `IsActive` | `false` | `true` |
| `IsTrial` | `false` | `false` |
| `ExpirationDate` | zero | zero |
| `ProductLicenses` | empty | empty |

Purchases, receipts, listings and consumable fulfillment fail as offline either way unless the separate [`--stub-iap` mode](stub-iap.md) is enabled. Microsoft's [`IsTrial` documentation](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.store.licenseinformation.istrial) describes `IsActive` true with `IsTrial` false as a full license.

## If the game stays in trial mode

Run with `--unlock-trial --log`. Once the game reads its license, `appx-portable.log` contains `Store: trial override enabled (base app only)`. If that line is missing, the game never asked `CurrentApp`; `--trace` shows which classes it requests instead.

If the line is there and the game is still limited, the game decides some other way: an in-app product such as a full-game upgrade, a receipt, a saved trial counter, its own server, or content missing from a demo package. The override does not cover those.
