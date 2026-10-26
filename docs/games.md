# Tested games

| Game                                                                                             | Version     | Arch | `appx-host.exe` | Status         | Notes                                                                           |
| ------------------------------------------------------------------------------------------------ | ----------- | ---- | --------------- | -------------- | ------------------------------------------------------------------------------- |
| **Cut the Rope 2**<br><sub>`ZeptoLabUKLimited.CuttheRope2`</sub>                                 | 1.0.0.46    | x86  |                 | ✅ Playable    |                                                                                 |
| **Fruit Ninja**<br><sub>`HalfbrickStudiosPtyLtd.FruitNinja`</sub>                                | 1.8.1.66    | x86  |                 | ✅ Playable    |                                                                                 |
| **Where's My Water?**<br><sub>`Disney.WheresMyWater`</sub>                                       | 1.13.1.3    | x86  |                 | ✅ Playable    |                                                                                 |
| **Bejeweled LIVE**<br><sub>`PopCap.BejweledLIVE`</sub>                                           | 4.0.0.10    | x86  | Yes             | ✅ Playable    |                                                                                 |
| **Gravity Guy**<br><sub>`MiniclipSA.GravityGuy`</sub>                                            | 1.0.1.2     | x86  | Yes             | ✅ Playable    |                                                                                 |
| **Where's My Mickey?**<br><sub>`Disney.WheresMyMickey`</sub>                                     | 1.1.0.30    | x86  |                 | 🩹 Needs patch | [AMPS startup EXE patch](../patch/wheresmymickey/README.md)                     |
| **Microsoft Sudoku** (2018)<br><sub>`Microsoft.MicrosoftSudoku`</sub>                            | 1.7.10190.0 | x86  | Yes             | 🩹 Needs patch | [Video-preloader asset patch](../patch/mssudoku/README.md). Use `appx-host.exe` |
| **Rayman Fiesta Run Windows 10 Edition**<br><sub>`Ubisoft.RaymanFiestaRunWindows10Edition`</sub> | 1.0.23.0    | x64  |                 | 🩹 Needs patch | [Caption bindings patch](../patch/raymanwin10/README.md)                        |

<sub>✅ Playable · 🟡 Partial: core gameplay checked, some features unverified or limited · 🩹 Needs patch: playable with an optional patch from [`patch/`](../patch/README.md) · 🧪 Experimental: early support</sub>

`installappx.py` applies a game's patch automatically when it recognizes the exact package and version.
