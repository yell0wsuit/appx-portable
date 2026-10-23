# Managed Store games with XAML Islands

Setup detects the CLR header in the manifest executable and stages `appx-host.exe` for managed games. Use `scripts/play.py --game <folder>`, `scripts/run.ps1`, or the staged host itself. Build on Windows with the normal x86 preset; the managed launcher is compiled with the .NET Framework compiler shipped with Windows.

The launcher must remain beside the game because system XAML resolves `ms-appx:///` against the process executable. Its manifest enables XAML Islands. A UI synchronization context keeps managed continuations on the island thread.

Managed mode uses real Windows XAML, a desktop window, and a composed `SwapChainPanel` for Windows 8 `SwapChainBackgroundPanel` subclasses. The shim supplies package identity, persistent settings (including named containers), URI file mapping, disconnected Xbox LIVE SDK 1.2 services, splash dismissal, popup XAML roots, and window lifecycle events. The host bridges XAML routed pointer events to CoreWindow subscribers for native game boards, using XAML pointer capture for drags. Keyboard messages are forwarded from the desktop queue. Only the island window is resized; XAML internal windows retain their own bounds. Native games continue using the existing hosting path.

## Bejeweled LIVE

Tested with the x86 4.0.0.10 package and VCLibs 110. The integrated host reaches the main menu and Classic board. Real Windows mouse input toggles the tutorial hint checkbox, dismisses the tutorial with Start Game, and swaps gems to create a match (score 0 to 150). Earlier posted-message tests did not verify the real pointer input path. Stats resource reads and detached popups work without the startup exceptions in the original spike. Live window resize, F11 fullscreen, and normal close with suspension were also checked. Additional live checks confirmed:

- The game produces nonzero audio in its two audio sessions (peak approximately 0.23), without recording other system audio.
- Returning through the in-game **Menu -> Main Menu** creates `classic.sav`; restarting restores the same gem layout.
- Butterflies and Diamond Mine reach their native boards and tutorials, with no uncaught managed exceptions in these runs.
- Normal desktop close now sends the island visibility notification, raises suspension, and closes the real CoreWindow before terminating.
- High-score toast creation uses the disabled-notification fallback. The exact game method that threw `0x80070490` now returns successfully; template XML remains a real Windows document for compatibility with the system toast constructor.

**Save before closing:** return through **Menu -> Main Menu** to preserve the current board. Closing an active Classic board directly still exits without writing the latest moves. This was reproduced with a scoring move and an unchanged save-file hash, even after proper visibility, suspension, and CoreWindow-close notifications. The managed app's suspension handler only completes its deferral; it does not save the board. The host does not invoke game-specific menu handlers automatically.

Longer sessions, mode-specific scoring, and touch/pen input still need testing.

Requires Windows 10 1903 or later and desktop .NET Framework 4.x. Wine and touch/pen input are unverified. Xbox services remain offline. Managed exceptions and first-chance diagnostics are written to `managed.log`; use `--log` or `--trace` for the native shim log. The synchronization context currently records uncaught posted exceptions rather than routing them through XAML's `Application.UnhandledException` event.
