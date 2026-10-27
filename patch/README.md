# Optional game patches

Game-specific compatibility patches live here, separate from the core runtime.
`installappx.py` applies the patch for a game it recognizes by package name
and version, downloading it at the same release tag as the DLLs. Each patch can
also be run by hand to check, reapply, or restore it. No game executables or
assets are included here.

| Patch | Status |
| --- | --- |
| [Practical English Usage](peu/README.md) | 1.0.6.9953 x86: portable WebView markup; main page and article HTML verified |
| [Rayman Fiesta Run Windows 10](raymanwin10/README.md) | 1.0.23.0 x64: source caption bindings; portable world map and mouse entry into a level verified |
| [Bejeweled LIVE](bejeweled/README.md) | Bejeweled LIVE 4.0.0.10 x86: declares the original dark theme before static brushes load; settings labels verified |
| [Where's My Mickey?](wheresmymickey/README.md) | Where's My Mickey? 1.1.0.30: intro and gameplay work with the current runtime; first level completed with three stars |
| [Microsoft Sudoku](mssudoku/README.md) | Microsoft Sudoku 1.7.10190.0 x86: closes the incompatible video-loading popup; menu, Very Easy tutorial dismissal, and number entry verified |

Each patch documents its supported game files, changes, and restore procedure.
