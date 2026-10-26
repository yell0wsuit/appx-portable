# Rayman Fiesta Run Windows 10 Edition

Version 1.0.23.0 is a native x64 UWP game. Use an **x64** appx-portable build and the **x64 Microsoft.VCLibs.140.00** framework, including `concrt140_app.dll` and the runtime's transitive DLLs. The desktop VC++ redistributable is a different runtime.

Download the [Microsoft-signed x64 VCLibs 140 package hosted by VideoLAN](https://download.videolan.org/pub/videolan/vlc-winrt/3.0.3/VLC.Universal_3.0.3.0_ARM_Test/Dependencies/x64/Microsoft.VCLibs.x64.14.00.appx) and pass it to setup with `-RuntimeDirectory`. This is version `14.0.25426.0`, which meets this game's manifest minimum of `14.0.22929.0`. Its SHA-256 is `9f8c11f71e8629413d55f55449d7f875def9492d0c64e9d7bcf13beddf69e872`.

The package embeds its caption XAML in `resources.pri`. Until the portable loader can read embedded XBF, this patch supplies source caption bindings with the same connection IDs. The host's desktop caption and F11/Alt+Enter provide the visible window controls. The custom caption's decorative controls are not rendered. No game executable or PRI bytes are changed.

After staging the package with `scripts\setup.bat`, close the game and run from the repository root:

```powershell
py patch/raymanwin10/patch.py '.\local\rayman-win10'
```

Use `--check` to inspect without writing. The patch accepts only this package's manifest identity and version, and never replaces a different `CustomTitleBar.xaml`. Reapplying it is safe.

Launch `RFRXAML.exe` in the staged folder. Verified: portable startup, world-map rendering, mouse entry into a level, keyboard controls, drag scrolling, and maximize/restore. Keyboard and dragging were also confirmed during interactive testing. Level completion, touch input, and save restoration are unverified. Online services remain unavailable.

To undo the caption patch, run it again with `--restore` while the game is closed; it removes only the file this patch added.
