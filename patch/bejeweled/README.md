# Bejeweled LIVE dark theme

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
