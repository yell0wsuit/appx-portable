# Offline in-app purchases

`--stub-iap` makes Windows 8/8.1 `CurrentApp` product purchases complete locally. It is off by default.

```powershell
python scripts/play.py --game 'C:\Games\MyGame' --stub-iap --log
.\scripts\run.ps1 -GameDirectory 'C:\Games\MyGame' -StubIap -Log
C:\Games\MyGame\Game.exe --stub-iap
```

Rebuild and rerun setup first to stage the updated runtime. The environment equivalent is `APPX_PORTABLE_STUB_IAP=1`; remove it or set it to `0` to disable it. Options are read once at startup. `--passthrough` uses Windows classes, so the stub has no effect there.

With this option:

- Both product purchase APIs complete with `AsyncStatus.Completed`; callbacks are queued on the game dispatcher so purchase progress dialogs can close normally. The Windows 8 API returns an empty string unless `includeReceipt` is requested, in which case it returns unsigned XML marked `appxportable:Stub="true"`; Windows 8.1 purchase results report `Succeeded` and a fresh transaction ID.
- Product licenses are inactive initially and become active after a successful stub purchase in the current process.
- Consumable fulfillment completes with `Succeeded`.
- Store listings succeed. Product IDs come from `Assets/in-app-purchase.xml` beside the game, if present. Product names use their IDs and prices are placeholders (`0.00`), not Store prices. Packages without this catalog receive an empty listing.
- `NetworkInformation.GetInternetConnectionProfile` returns a synthetic profile reporting `InternetAccess`, allowing games' connectivity preflight checks to reach the purchase stub. The shim's HTTP clients remain offline.

Base-app licensing is controlled separately by [`--unlock-trial`](trial-mode.md). App purchases and the separate receipt retrieval APIs still fail offline. Product entitlements reset when the process exits; the game must save its own granted content. The runtime does not persist entitlements or fabricate signed receipts, download missing content, or restore remote services.

Use `--log` to record the connectivity preflight, listing requests, and successful purchase product IDs in `appx-portable.log`.

## Local game checks

Tested on staged copies of the supplied packages:

| Game | Purchase verified |
| --- | --- |
| Where's My Mickey? 1.1.0.30 | Goofy / Shipwrecked and Glass Half Empty / Menace of Venice episodes; show the thank-you dialog. The staged EXE requires the separate AMPS Python patch. |
| Where's My Water? 1.13.1.3 | Allie's Story; Try/Buy changes to Play. |
| Fruit Ninja 1.8.1.66 | Dojo ? Gutsu?s Cart ? Starfruit ? Buy; user verified Item Purchased after the receipt fix. Requires `--unlock-trial --stub-iap`. |
| Where's My Perry? manifest version 1.5.1.1 | Chapter unlocks (`LP_TWO`, `LP_THREE`, `LP_FOUR`); queued completion fixes the stuck Processing dialog. |

These checks cover local purchase handling and menu unlocks. Content downloads and restore across a new save are unverified.

Fruit Ninja 1.8.1.66 needs both `--unlock-trial --stub-iap`. Its Starfruit purchase requests `includeReceipt=true`; an empty receipt is treated as a failed purchase, so the legacy purchase stub supplies marked, unsigned local XML. This is not a receipt that Store servers can verify.
