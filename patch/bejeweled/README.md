# Bejeweled LIVE dark theme

Bejeweled LIVE 4.0.0.10 x86 expects the Windows 8 dark default. Modern XAML
Islands follow the desktop preference, which can resolve its static text
brushes to black on its black settings background.

This patch adds `RequestedTheme="Dark"` to the root of `App.xaml`, before
application resources load. It changes no executable, save, or preference.
Only the known original and patched SHA-256 hashes are accepted; the original
is kept as `App.xaml.islands-original`. No game assets are distributed here.

Close the game, then run from this repository:

```powershell
py patch/bejeweled/patch.py 'D:\Programs\appx\PopCap.BejweledLIVE_4.0.0.10_x86__dw446q17t8sdy'
```

Use `--check` to inspect without writing, or `--restore` to restore the verified
original. Reapplying the patch is safe. Live validation confirmed readable
volume headings, toggle labels, and buttons in the settings panel.
