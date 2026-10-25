# Managed Store games with XAML Islands

Setup detects the CLR header in the manifest executable and stages `appx-host.exe` for managed games. Use `scripts/play.py --game <folder>`, `scripts/run.ps1`, or the staged host itself. Build on Windows with the normal x86 preset; the managed launcher is compiled with the .NET Framework compiler shipped with Windows.

The launcher reads the executable's entry-point apartment attribute. An `MTAThread` entry point selects managed CoreWindow hosting, using the shim's `IFrameworkView` lifecycle and graphics redirection. Other managed entry points use the STA XAML Islands path. CLR activation interception remains enabled for both paths. System message dialogs in CoreWindow mode receive the desktop game window as their owner.

Gravity Guy 1.0.1.2 (x86, Cocos2D/SharpDX) uses the managed CoreWindow path. Startup, offline Xbox sign-in error handling, and entry into Story gameplay were checked with real mouse input. Longer sessions, other modes, and save restoration remain unverified.

Maximized startup delivers visibility before activation, including when Windows omits `WM_SHOWWINDOW`; Gravity Guy needs that notification to start updating and drawing. Its Xbox SDK 1.1 uses the same `User` interface layout as SDK 1.0. Opening achievements now creates the offline user object instead of failing with class-not-registered; achievement queries still fail offline and no online achievement data is supplied.

The launcher must remain beside the game because system XAML resolves `ms-appx:///` against the process executable. Its manifest enables XAML Islands. A UI synchronization context keeps managed continuations on the island thread.

Managed mode uses real Windows XAML, a desktop window, and a composed `SwapChainPanel` for Windows 8 `SwapChainBackgroundPanel` subclasses. The shim supplies package identity, persistent settings (including named containers), URI file mapping, disconnected Xbox LIVE SDK 1.2 services, splash dismissal, popup XAML roots, and window lifecycle events. The host bridges XAML routed pointer events to CoreWindow subscribers for native game boards, using XAML pointer capture for drags. Keyboard messages are forwarded from the desktop queue. Only the island window is resized; XAML internal windows retain their own bounds. Native games continue using the existing hosting path.

## Bejeweled LIVE

Windows 8 defaulted to dark; modern islands inherit the desktop theme. The host
applies the legacy default to the main visual root and detached popup children,
retaining explicit element themes and constructor theme requests. This fixes
Sudoku's black titles and captions. Bejeweled also captures foreground brushes
in application-wide static styles before those roots exist: apply the optional
[dark theme patch](../patch/bejeweled/README.md) to declare `RequestedTheme="Dark"`
in its `App.xaml` before resources load. Live checks confirmed white settings
headings, toggle labels, and buttons. A further gameplay check with the theme
fix increased the Classic score from 1,500 to 1,550. The application theme
cannot safely be changed after those resources have loaded.

Tested with the x86 4.0.0.10 package and VCLibs 110. The integrated host reaches the main menu and Classic board. Real Windows mouse input toggles the tutorial hint checkbox, dismisses the tutorial with Start Game, and swaps gems to create a match (score 0 to 150). Earlier posted-message tests did not verify the real pointer input path. Stats resource reads and detached popups work without the startup exceptions in the original spike. Live window resize, F11 fullscreen, and normal close with suspension were also checked. Additional live checks confirmed:

- The game produces nonzero audio in its two audio sessions (peak approximately 0.23), without recording other system audio.
- Returning through the in-game **Menu -> Main Menu** creates `classic.sav`; restarting restores the same gem layout.
- Butterflies and Diamond Mine reach their native boards and tutorials, with no uncaught managed exceptions in these runs.
- Normal desktop close now sends the island visibility notification, raises suspension, and closes the real CoreWindow before terminating.
- High-score toast creation uses the disabled-notification fallback. The exact game method that threw `0x80070490` now returns successfully; template XML remains a real Windows document for compatibility with the system toast constructor.

**Settings limitation:** the game's saved preferences are written and reloaded, but some restored off states are not applied to its engine. Its startup code relies on toggle-change handlers that do not run when an already-off control is assigned off again. Volume initialization can also overwrite its saved sound toggle. The host follows Windows XAML event semantics and does not invoke private game handlers or rewrite settings in response to application initialization. No game-specific settings adapter is shipped.

**Deferred settings investigation (2026-10-24):** a diagnostic startup comparison with identical isolated saved preferences produced the same result using WinForms continuations and the installed Framework's `WinRTCoreDispatcherBasedSynchronizationContext`: hint controls restored off while internal flags stayed on, and volume-slider initialization enabled sound. Using CoreDispatcher for those continuations did not resolve the issue. This comparison does not reproduce the full Windows 8 packaged lifecycle. Further work needs a packaged Windows 8/8.1 baseline and a minimal reproduction of any differing storage, event, or startup ordering. No safe game-agnostic correction has been established; settings restoration remains deferred.

**Save before closing:** return through **Menu -> Main Menu** to preserve the current board. Closing an active Classic board directly still exits without writing the latest moves. This was reproduced with a scoring move and an unchanged save-file hash, even after proper visibility, suspension, and CoreWindow-close notifications. The managed app's suspension handler only completes its deferral; it does not save the board. The host does not invoke game-specific menu handlers automatically.

**Sudoku compatibility regression check (2026-10-25):** with the constructor-time XAML startup and CoreWindow changes, Bejeweled still reaches its Classic board. A real mouse drag made a match and increased the score from 1,100 to 1,150. The host retains the island's UI dispatcher so games can obtain it from a retained CoreWindow on a worker thread.

Longer sessions, mode-specific scoring, and touch/pen input still need testing.

## Microsoft Sudoku

Microsoft.MicrosoftSudoku 1.7.10190.0 x86 constructs a Frame inside its Application constructor. The host initializes system XAML when the first control or component is requested after Application composition, deferring OnLaunched until the startup callback has finished. CoreWindow.GetForCurrentThread, Window.Current.CoreWindow, and CoreApplication.MainView.CoreWindow resolve to the same portable game window. Standard Windows 8.1 SwapChainPanel controls bridge real XAML pointer events to CoreWindow subscribers, including Unity; the Windows 8.0 SwapChainBackgroundPanel adapter retains its existing input path.

This game also needs the explicit [video-preloader asset workaround](../patch/mssudoku/README.md). With that patch, the main menu and a Very Easy puzzle were tested with real mouse input: both tutorial overlays dismiss, number entry works, and the timer advances. GUID settings round trips, non-prelaunch activation, safe badge updates, and unpinned secondary-tile queries are supported. Start-menu pin creation, online services, daily challenges, and longer play sessions remain unverified.

The host uses the system WindowsXamlManager and DesktopWindowXamlSource APIs. Microsoft's [hosting guidance](https://learn.microsoft.com/en-us/windows/uwp/xaml-islands/using-the-xaml-hosting-api) and [native samples](https://github.com/microsoft/Xaml-Islands-Samples) are the reference for the island itself. Legacy CoreWindow, package, and Store compatibility still require this repository's bridge; the standard host does not provide an installed Windows 8 app environment.

Requires Windows 10 1903 or later and desktop .NET Framework 4.x. Wine and touch/pen input are unverified. Xbox services remain offline. Managed exceptions and first-chance diagnostics are written to `managed.log`; use `--log` or `--trace` for the native shim log. The synchronization context currently records uncaught posted exceptions rather than routing them through XAML's `Application.UnhandledException` event.
