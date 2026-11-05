# Minion Rush startup analytics workaround

For **GAMELOFTSA.DespicableMeMinionRush 4.1.4.1 x86**, with the exact
`IGPLib.dll` identified below and the current appx-portable runtime.

With `StubIap=1`, the runtime supplies a simulated connection profile so games
can reach its local purchase stub. Minion Rush's IGPLib also treats that profile
as a reason to send startup analytics. It waits indefinitely for that request's
completion event when HTTP is offline, before the game window opens.

This patch skips the single blocking analytics call in
`IGPBase::TrackApplicationLoading`. It keeps the surrounding initialization,
cleanup, and tracking-state update. The game executable, saves, content assets,
Store interfaces, and purchase-stub behavior stay intact.

Close the game, then run from the repository root:

```powershell
py patch/minionrush/patch.py 'F:\StoreDownloads\minion rush'
```

Set `StubIap=1` in the game folder's `appx-portable.ini`, then launch normally.
The patch does not change that setting automatically.

The original DLL is kept as `IGPLib.dll.analytics-original`. Repeated application
is safe. To identify or restore the DLL:

```powershell
py patch/minionrush/patch.py 'F:\StoreDownloads\minion rush' --check
py patch/minionrush/patch.py 'F:\StoreDownloads\minion rush' --restore
```

Only this exact DLL and patch output are accepted:

| DLL | SHA-256 |
| --- | --- |
| Original | `869e2e942e78877b2f2e58c1edd5bef53efefac7b30a32440d9c8f53fe8361c0` |
| Patched | `ec49581d6c7b90c3ce6f3b8157c7c5d5aa8fd91b9e406d917595aaa2e0f7b3f4` |

At file offset `0x171BD` (RVA `0x17DBD`), the five-byte call
`E8 EE C4 FF FF` to RVA `0x142B0` becomes five NOPs. IDA and a live thread
stack confirmed the skipped helper waits on `Concurrency::event` during startup.

This workaround does not download additional game content or restore online
services. The game's content-download requirement remains a separate blocker.
No game files are distributed with this patch. Installer integration is not yet
provided; apply it manually as shown above.

Validated on 2026-11-05: `StubIap=1` read from the game's saved settings,
`OnLaunched` succeeded, the game window opened, and rendering continued past
1,000 frames. In-game purchases remain unverified because additional game
content is required. Automated patch tests cover hash and instruction guards,
backups, repeated application, failed replacement, and restoration; a round-trip
check on the real DLL confirmed that exactly five bytes change.

If restoring the original DLL, set `StubIap=0` again to avoid its analytics wait.
