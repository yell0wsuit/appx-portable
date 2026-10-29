# Saves and window settings

## Save location

Saves go to `SaveData` beside the game executable. To change that, edit `appx-portable.ini` in the game folder:

```ini
[storage]
SaveLocation=game
```

| Value            | Save folder                                       |
| ---------------- | ------------------------------------------------- |
| `game` (default) | `SaveData` beside the game executable             |
| `documents`      | `Documents\appx-portable\<package>`               |
| `appdata`        | `%LOCALAPPDATA%\appx-portable\<package>`          |
| A folder path    | That folder; relative paths start beside the game |

`<package>` is the identity name from the game's manifest, so games never share saves. If the folder cannot be written, saves go to `documents`, then `appdata`. Changing the location does not move existing saves.

## Window

Press **F11** or **Alt+Enter** to toggle borderless fullscreen. Window size, position, and mode are remembered in `Local\window.ini` inside the save folder; delete it with the game closed to reset them. The game keeps running when it loses focus and pauses when minimized. Closing the window suspends the game, which saves, and then ends it.

## Store options

## Frame rate

`appx-portable.ini` caps the game at 60 frames a second by default:

```ini
[options]
FrameRate=60
```

Most Store games advance one fixed step per frame and expect 60. Without the cap, they run too fast on a faster display, and under Wine on macOS, which does not wait for the display. Set `FrameRate=0` to remove the cap for a game that keeps its speed at any frame rate, or another number for a different cap. The `APPX_PORTABLE_FRAME_RATE` environment variable overrides the file. Restart the game after a change.

`appx-portable.ini` also holds the Store overrides; see [trial override](trial-mode.md) and [offline in-app purchases](stub-iap.md).
