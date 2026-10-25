# Where's My Mickey? AMPS startup patch

For `Disney.WheresMyMickey` 1.1.0.30 x86. This standalone Python 3 script patches
the staged game executable so it no longer needs the Mickey-specific plugin DLL.
The usual appx-portable runtime and VCLibs DLLs are still required.

The game unconditionally constructs two unused AMPS test objects during static
initialization. Their source URL names Avengers Initiative Android game data,
with destination `AppX` and user info `test`. In the inspected executable, those
globals are referenced only during initialization and cleanup. No game data is
added, removed, or downloaded by this patch.

The patch replaces the first three bytes of each initializer at image addresses
`0x80E660` and `0x80E750` (file offsets `0x40DA60` and `0x40DB50`) with
`xor eax, eax; ret`. This skips construction and registration of their matching
exit cleanup. Only six bytes change. Shared XAML fixes, including the former
post-intro freeze, remain in the core runtime.

Close the game and run these commands from the repository root, using Python 3
and the executable in your staged game folder:

```powershell
python patch/wheresmymickey/patch.py 'C:\Games\Mickey\WheresMyMickey.Windows8.exe' --check
python patch/wheresmymickey/patch.py 'C:\Games\Mickey\WheresMyMickey.Windows8.exe'
```

The script checks the complete input hash and instruction bytes, creates
`WheresMyMickey.Windows8.exe.amps-original` beside the EXE, verifies the patched
output hash, and replaces the EXE. It refuses unsupported or modified inputs
and conflicting backups. Repeating the patch is harmless. Leave the original
extracted package untouched and patch a staged copy.

Launch the game normally afterward. `wheresmymickey.dll` and
`APPX_PORTABLE_PLUGIN` are no longer needed. Rerunning setup from the original
package restores its EXE, so reapply this patch after doing so.

To restore the verified backup (the backup is retained):

```powershell
python patch/wheresmymickey/patch.py 'C:\Games\Mickey\WheresMyMickey.Windows8.exe' --restore
```

| Executable | SHA-256 |
| --- | --- |
| Original | `d887400df1b4c3a21aa2669d731288ce23ddd0b28ac50309257ac85fb5ae2424` |
| Patched | `83e1bc9cb836884ccbd65e02c78eae832f927bd14d44c13787471f4867e510b1` |

Run the patcher safety tests without game binaries:

```powershell
python -m unittest discover -s tests/python -p test_wheresmymickey_patch.py -v
```

With the current runtime, menu, intro, and mouse input have been verified and the
first level completed with three stars. Save restoration is unverified.
