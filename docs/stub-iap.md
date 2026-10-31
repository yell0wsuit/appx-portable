# Offline in-app purchases

`--stub-iap` makes Windows 8/8.1 `CurrentApp` and Xbox Marketplace product purchases complete locally. It is off by default.

```powershell
C:\Games\MyGame\Game.exe --stub-iap
```

To keep it on however the game is started, set it in `appx-portable.ini` beside the game executable (`installappx.py` writes this when you choose stub IAP):

```ini
[options]
StubIap=1
```

Rerun `installappx.py` first so the game folder has a runtime that supports it. The environment equivalent is `APPX_PORTABLE_STUB_IAP=1`; remove it or set it to `0` to disable it; a set variable overrides the ini. Options are read once at startup. `--passthrough` uses Windows classes, so the stub has no effect there.

With this option:

- Both product purchase APIs complete with `AsyncStatus.Completed`; callbacks are queued on the game dispatcher so purchase progress dialogs can close normally. The Windows 8 API returns an empty string unless `includeReceipt` is requested, in which case it returns unsigned XML marked `appxportable:Stub="true"`; Windows 8.1 purchase results report `Succeeded`, a fresh transaction ID, and the same marked, unsigned receipt XML, whose `ProductReceipt` names the product.
- Product licenses are inactive initially and become active after a successful stub purchase in the current process.
- Consumable fulfillment completes with `Succeeded`.
- Store listings succeed. The shim discovers bundled XML catalogs by their `CurrentApp/ListingInformation/Product` structure, regardless of filename or location inside the package, and caches their product IDs for the run. License entries are ignored. Product names use their IDs and prices are placeholders (`0.00`), not Store prices. Packages without a catalog receive an empty listing, unless `StubIapProducts` names their products (see below).
- Unfulfilled consumables are an empty list: stub purchases are fulfilled when the game reports them, or forgotten when it exits.
- `NetworkInformation.GetInternetConnectionProfile` returns a synthetic profile reporting `InternetAccess`, allowing games' connectivity preflight checks to reach the purchase stub. It supports the Windows 8.1 profile interface and reports a wired connection, with no WLAN/WWAN details. The shim's HTTP clients remain offline.

## Games without a product catalog

Unity IAP and similar libraries offer only the products the Store listing contains, so a game that ships no catalog finds none and its purchases never start. Name its product IDs in `appx-portable.ini`, separated by commas:

```ini
[options]
StubIap=1
StubIapProducts=com.example.levels,com.example.coins
```

`APPX_PORTABLE_STUB_IAP_PRODUCTS` overrides the file. With `--log`, `appx-portable.log` notes when the listing is empty.

### Finding product IDs

The IDs are the strings the game passes to the Store, so they come from the game itself. Try these in order:

1. **Unity IAP games: Unity's log.** Run the game once with `StubIap=1`. Unity IAP names every product it expected but did not find in the listing in `UnityPlayer.log`, in the game's temporary save folder (`SaveData\Temp` with the default save location):

   ```text
   Unavailable product com.example.levels -com.example.levels
   ```

   Copy each ID into `StubIapProducts`.

2. **A bundled catalog.** If the package ships a CurrentAppSimulator file, often `WindowsStoreProxy.xml`, there is nothing to do: the shim reads its `ProductId` attributes automatically and logs `Store: stub IAP catalog <file> (<n> products)` with `--log`.

3. **Games that ask for products by name: the trace log.** Many Windows 8 games check `ProductLicenses["id"]` before showing a purchase. Run with `--trace` and look in `appx-portable.log` for `StoreMap HasKey id=...` and `Store: product license id=...`.

4. **Strings in the game's files.** IDs are usually string literals, often in reverse-domain form (`com.publisher.item`):
   - Unity IL2CPP games keep every C# string literal in `Data\il2cpp_data\Metadata\global-metadata.dat`; search it for the publisher's prefix.
   - .NET games: open the game's assemblies in a decompiler such as ILSpy and look at the arguments to `RequestProductPurchaseAsync` and `ProductLicenses`.
   - C++ games: search the executable and its DLLs for UTF-16 strings.

   Strings found this way are candidates; confirm them with the first or third method, since a game may contain IDs it no longer sells.

## Xbox Marketplace

`Microsoft.Xbox.Marketplace.MarketplaceService` uses offer GUIDs and numbered
assets rather than `CurrentApp` product IDs. With `StubIap=1`, purchases complete
successfully, create a fresh transaction and a local receipt, and grant the
configured asset quantity. Asset consumption subtracts the requested quantities;
insufficient balances fail without consuming anything. Listings and balance and
receipt queries return local snapshots. Without `StubIap`, these operations fail
offline through their async callbacks.

Configure consumables in `[options] StubXboxProducts` as comma-separated
`offer-guid:asset-id:quantity` entries. A GUID alone describes a durable offer
with a receipt and no asset grant. `APPX_PORTABLE_STUB_XBOX_PRODUCTS` overrides
the file. Unlisted GUIDs can be purchased and produce receipts, but grant no
assets. The log notes when a purchase has no asset mapping.

iStunt 2 1.0.1.4 uses asset 1 for its four coin packs:

```ini
[options]
StubIap=1
StubXboxProducts=546f0200-0000-4000-8000-0000394e07d3:1:1500,a9c41e00-0000-4000-8000-0000394e07d3:1:3500,ab5b2600-0000-4000-8000-0000394e07d3:1:9000,2b698600-0000-4000-8000-0000394e07d3:1:20000
```

Monster Island 1.0.2.3 also uses asset 1, with different offer GUIDs:

```ini
[options]
StubIap=1
StubXboxProducts=34e44200-0000-4000-8000-0000394e07d4:1:1500,cb716200-0000-4000-8000-0000394e07d4:1:3500,31872a00-0000-4000-8000-0000394e07d4:1:9000,e6ecee00-0000-4000-8000-0000394e07d4:1:20000
```

These mappings come from the game's offer IDs, asset IDs and coin-pack amounts;
they are not inferred from the GUIDs. Other games require their own mappings.
Listings use the GUID as the product name and `0.00` as a placeholder price.
Receipts and asset JSON are unsigned and marked `appxportableStub: true`; they
cannot be verified by Xbox servers. Receipts and unconsumed balances last only
for the current process, like the `CurrentApp` stub's entitlements. The game must
save granted content itself. Xbox sign-in and other Xbox services stay offline.

Base-app licensing is controlled separately by [`--unlock-trial`](trial-mode.md). `CurrentApp` app purchases and its separate receipt retrieval APIs still fail offline. Product entitlements reset when the process exits; the game must save its own granted content. The runtime does not persist entitlements or fabricate signed receipts, download missing content, or restore remote services.

Use `--log` to record the connectivity preflight, listing requests, and successful purchase product IDs in `appx-portable.log`.

## Local game checks

Tested on staged copies of the supplied packages:

| Game                                       | Purchase verified                                                                                                                                                                  |
| ------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Cut the Rope 2 1.0.0.46                    | Store / Candy Coins / 250 coins; balance increased from 57 to 307 and consumable fulfillment succeeded. Requires the Windows 8.1 connection profile and catalog discovery support. |
| Where's My Mickey? 1.1.0.30                | Goofy / Shipwrecked and Glass Half Empty / Menace of Venice episodes; show the thank-you dialog. The staged EXE requires the separate AMPS Python patch.                           |
| Where's My Water? 1.13.1.3                 | Allie's Story; Try/Buy changes to Play.                                                                                                                                            |
| Fruit Ninja 1.8.1.66                       | Dojo ? Gutsu?s Cart ? Starfruit ? Buy; user verified Item Purchased after the receipt fix. Requires `--unlock-trial --stub-iap`.                                                   |
| Where's My Perry? manifest version 1.5.1.1 | Chapter unlocks (`LP_TWO`, `LP_THREE`, `LP_FOUR`); queued completion fixes the stuck Processing dialog.                                                                            |
| Human Anatomy Atlas 2019 2019.1.26.0 (x64) | All animations, advanced animations and 3D dental anatomy; Unity IAP initializes and the game validates each receipt and unlocks. Requires `StubIapProducts` (above).              |
| iStunt 2 1.0.1.4 (x86) | Store / Coins / 20,000 pack; balance increased from 129,000 to 149,000 in a staged save. Requires the `StubXboxProducts` mapping above. All four packs, receipts and consumption also passed a CLR check using the game's Xbox metadata. |
| Monster Island 1.0.2.3 (x86) | Store / Coins / 20,000 pack; balance increased from 40,750 to 60,750 in a staged save. Requires its `StubXboxProducts` mapping above. All four packs, receipts and consumption also passed the CLR check. |

These checks cover local purchase handling and menu unlocks. Content downloads and restore across a new save are unverified.

Fruit Ninja 1.8.1.66 needs both `--unlock-trial --stub-iap`. Its Starfruit purchase requests `includeReceipt=true`; an empty receipt is treated as a failed purchase, so the legacy purchase stub supplies marked, unsigned local XML. This is not a receipt that Store servers can verify.
