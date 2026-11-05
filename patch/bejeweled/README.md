# Bejeweled LIVE patches

The default patch restores the dark theme. The Wine style and native texture
patches below are separate opt-in modes; run each mode separately.

## Dark theme

Bejeweled LIVE 4.0.0.10 x86 expects the Windows 8 dark default. Modern XAML
Islands follow the desktop preference, which can resolve its static text
brushes to black on its black settings background.

This patch adds `RequestedTheme="Dark"` to the root of `App.xaml`, before
application resources load. It changes no executable, save, or preference.
Only the known original and patched SHA-256 hashes are accepted; the original
is kept as `App.xaml.islands-original`. No game assets are distributed here.

`pyappxportable` applies this patch automatically for this exact version. The commands below check, reapply, or restore it by hand.

Close the game, then run from this repository:

```powershell
py patch/bejeweled/patch.py 'D:\Programs\appx\PopCap.BejweledLIVE_4.0.0.10_x86__dw446q17t8sdy'
```

Use `--check` to inspect without writing, or `--restore` to restore the verified
original. Reapplying the patch is safe. Live validation confirmed readable
volume headings, toggle labels, and buttons in the settings panel.

## Wine WPF style references

For the managed Wine WPF adapter, use the separate opt-in style-reference patch:

```sh
python3 patch/bejeweled/patch.py /path/to/staged/game --wpf
```

The shipped `Common/BejeweledStyles.xaml` defines seven empty Button styles where
snapped-mode setters expect brushes, thickness, a font family, and a size. WPF
seals application styles eagerly and rejects their types before any page loads.
The Wine patch redirects only those StaticResource references to the adapter's
corresponding dark theme keys. It preserves the original as
`BejeweledStyles.xaml.wpf-original`, verifies both input and output hashes, and
supports `--check` and `--restore`. It does not run with the default Windows theme
patch. The local `game.py` WPF launch harness applies it explicitly.

## Wine native texture writes

With the game closed, apply the native texture patch to the staged game:

```sh
python3 patch/bejeweled/patch.py /path/to/staged/game --wine-textures
```

Bejeweled LIVE 4.0.0.10 x86 writes its image pixels through staging textures
created with `D3D11_CPU_ACCESS_READ` and mapped with `D3D11_MAP_READ`. Wine does
not preserve those writes when copying the staging texture back. Its GPU
artwork textures stay transparent, leaving a nearly black board with a faint
procedural checkerboard. The patch changes the texture-lock helper to request
CPU read/write access and `D3D11_MAP_READ_WRITE`. It changes two bytes in
`Bej3Win8Component.dll`; the separate readback helper remains unchanged.

This belongs in the game patch because the game writes through a read-only
mapping. A general shim workaround would change other applications' texture
access semantics. The narrow correction restored the tutorial and full board
in a staged Wine run with the normal shim. Native Windows behavior after this
patch has not been verified, and reaching the board is not a completed-round
compatibility claim.

The patch accepts only these complete DLL hashes:

- Original: `a82fdb1c7e9021877cdf8874f64f2a671ea968e26ea29445691359bf87c105d3`
- Patched: `0bc37b7839abda93f52fb527f5c6553679f8b05baee9f04647889bacd16d27e8`

It also checks the original instruction bytes before editing and verifies the
output hash. The original is retained as
`Bej3Win8Component.dll.wine-textures-original`. Reapplying is safe; use
`--wine-textures --check` to inspect without writing or
`--wine-textures --restore` to restore the verified backup. The default theme
and `--wpf` modes do not modify the DLL. This mode is not applied automatically
by the installer or the WPF launch harness. No game DLL or other game assets
are distributed here.
