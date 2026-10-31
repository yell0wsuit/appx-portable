# Game compatibility

| Game                                 | Package ID     | Version     | Arch | Requires `appx-host.exe` | Requires `appx-native-host.exe` | Status         | Notes                                                       |
| ------------------------------------ | -------------- | ----------- | ---- | ------------------------ | ------------------------------- | -------------- | ----------------------------------------------------------- |
| Cut the Rope 2                       | `9NBLGGH0J0DC` | 1.0.0.46    | x86  |                          |                                 | ✅ Playable    |                                                             |
| Fruit Ninja                          | `9WZDNCRFJCDR` | 1.8.1.66    | x86  |                          |                                 | ✅ Playable    |                                                             |
| Where's My Water?                    | `9WZDNCRFJCV2` | 1.13.1.3    | x86  |                          |                                 | ✅ Playable    |                                                             |
| Bejeweled LIVE                       | `9WZDNCRFJ14D` | 4.0.0.10    | x86  | ✓                        |                                 | ✅ Playable    |                                                             |
| Gravity Guy                          | `9WZDNCRFHX46` | 1.0.1.2     | x86  | ✓                        |                                 | ✅ Playable    |                                                             |
| Leo's Fortune                        | `9WZDNCRD240S` | 1.1.0.9     | x86  |                          |                                 | ✅ Playable    |                                                             |
| Where's My Perry?                    | `9WZDNCRDCTFF` | 1.5.1.1     | x86  |                          |                                 | ✅ Playable    |                                                             |
| Lara Croft GO                        | `9NBLGGH3DT1K` | 3.0.90.671  | x86  | ✓                        |                                 | ✅ Playable    |                                                             |
| Hitman GO                            | `9NBLGGGZXZD2` | 1.0.52.0    | x86  | ✓                        |                                 | ✅ Playable    |                                                             |
| Deus Ex GO                           | `9NH49PJ27XW4` | 2.1.9041.0  | x64  |                          | ✓                               | ✅ Playable    |                                                             |
| Angry Birds Space                    | `9WZDNCRFJ51X` | 1.6.1.0     | x86  |                          |                                 | ✅ Playable    |                                                             |
| Angry Birds Star Wars                | `9WZDNCRFJ51W` | 1.5.0.0     | x86  |                          |                                 | ✅ Playable    |                                                             |
| Where's My Mickey? XL                | `9WZDNCRFJCV7` | 1.1.0.30    | x86  |                          |                                 | 🩹 Needs patch | [AMPS startup EXE patch](../patch/wheresmymickey/README.md) |
| Microsoft Sudoku (2018)              | `9WZDNCRFHV60` | 1.7.10190.0 | x86  | ✓                        |                                 | 🩹 Needs patch | [Video-preloader asset patch](../patch/mssudoku/README.md)  |
| Rayman Fiesta Run Windows 10 Edition | `9NBLGGH59M6B` | 1.0.23.0    | x64  |                          |                                 | 🩹 Needs patch | [Caption fallback](../patch/raymanwin10/README.md)          |

A ✓ under **Requires `appx-host.exe`** marks a managed (.NET) game, which starts through `appx-host.exe`; see [managed games](islands.md). Managed games do not run under Wine yet.

A ✓ under **Requires `appx-native-host.exe`** marks a .NET Native XAML game or a native game that imports no proxy DLL (such as a C++/CX Unity player), which starts through `appx-native-host.exe` on Windows; see [installer dependencies](installer.md#without-the-launcher). .NET Native XAML games do not run under Wine yet.

`installappx.py` applies a game's patch automatically when it recognizes the exact package and version.
