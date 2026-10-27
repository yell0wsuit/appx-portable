# Tested games

| Game                                 | Package ID     | Version     | Arch | `appx-host.exe` | Status         | Notes                                                       |
| ------------------------------------ | -------------- | ----------- | ---- | --------------- | -------------- | ----------------------------------------------------------- |
| Cut the Rope 2                       | `9NBLGGH0J0DC` | 1.0.0.46    | x86  |                 | ✅ Playable    |                                                             |
| Fruit Ninja                          | `9WZDNCRFJCDR` | 1.8.1.66    | x86  |                 | ✅ Playable    |                                                             |
| Where's My Water?                    | `9WZDNCRFJCV2` | 1.13.1.3    | x86  |                 | ✅ Playable    |                                                             |
| Bejeweled LIVE                       | `9WZDNCRFJ14D` | 4.0.0.10    | x86  | Yes             | ✅ Playable    |                                                             |
| Gravity Guy                          | `9WZDNCRFHX46` | 1.0.1.2     | x86  | Yes             | ✅ Playable    |                                                             |
| Leo's Fortune                        | `9WZDNCRD240S` | 1.1.0.9     | x86  |                 | ✅ Playable    |                                                             |
| Where's My Perry?                    | `9WZDNCRDCTFF` | 1.5.1.1     | x86  |                 | ✅ Playable    |                                                             |
| Where's My Mickey? XL                | `9WZDNCRFJCV7` | 1.1.0.30    | x86  |                 | 🩹 Needs patch | [AMPS startup EXE patch](../patch/wheresmymickey/README.md) |
| Microsoft Sudoku (2018)              | `9WZDNCRFHV60` | 1.7.10190.0 | x86  | Yes             | 🩹 Needs patch | [Video-preloader asset patch](../patch/mssudoku/README.md)  |
| Rayman Fiesta Run Windows 10 Edition | `9NBLGGH59M6B` | 1.0.23.0    | x64  |                 | ✅ Playable    | [Original embedded caption](xbf.md)    |

`installappx.py` applies a game's patch automatically when it recognizes the exact package and version.
