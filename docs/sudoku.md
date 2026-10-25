# Microsoft Sudoku investigation and fix

Microsoft.MicrosoftSudoku 1.7.10190.0 x86, investigated and tested on 2026-10-25.
The source package was `D:\Programs\appx\MSSudoku2018`; diagnostic execution
used `local/sudoku-investigation/game` so the original saves were preserved.

The initial crash was `RPC_E_WRONG_THREAD` while constructing a Frame inside
`Common.ApplicationModel.ApplicationBase`'s constructor. The host invoked the
startup callback before initializing XAML. A standalone x86 probe confirmed
that composing Application alone leaves Frame construction unavailable;
WindowsXamlManager initialization on the same STA thread makes it succeed.

The runtime now initializes XAML for constructor-time controls and LoadComponent,
while deferring OnLaunched until the startup callback completes. It also supplies
non-prelaunch activation, a consistent CoreWindow identity for native and managed
callers, a retained UI dispatcher usable from worker threads, and routed pointer
input for standard SwapChainPanel controls. Further startup failures required
GUID settings persistence and safe badge/secondary-tile queries.

Concurrent first-chance exceptions exposed a launcher logging race: simultaneous
File.AppendAllText calls could throw a sharing violation from the exception
observer and terminate the CLR. Logging is now serialized and suppresses its own
recursive failures. Regression fixtures exercise concurrent exceptions and a
locked log file, both startup control-order variants, UI layout, launch count,
CoreWindow identity, and worker-thread dispatcher access.

The compiled video-preloader popup has a separate system-XAML Islands parser
incompatibility. The explicit, hash-checked one-byte asset workaround lives at
[patch/mssudoku](../patch/mssudoku/README.md). The game executable stays intact.

Live checks confirmed Very Easy tutorial dismissal, mouse number entry (a 2 in
the upper-left cell), and an advancing timer. Bejeweled LIVE 4.0.0.10 was tested
with the same runtime: a real drag made a Classic match, increasing the score
from 1,100 to 1,150. See [managed hosting](islands.md) for limits and details.

Open the charm command list with **Ctrl+,** or **Alt+Space → Game settings**.
Choose **Game options** for sound/music controls, **About**, or **How to play**.
The original flyouts now use correctly sized island popups. The Help page's
horizontal GridView also has a scroll bar and responds to ordinary wheel input.

Probes, disassembly, screenshots, and diagnostic logs remain gitignored under
`local/sudoku-investigation/`; no game assets are included in the repository.
