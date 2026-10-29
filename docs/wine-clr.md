# x86 CLR transitions on Wine and Rosetta

`wine/wow64cpu.patch` is an experimental Wine 11.18 patch for the managed XAML
prerequisite on Apple Silicon. The CI runtime artifact applies it to an owned copy of the pinned Gcenx engine.
It changes Wine's WoW64 CPU module, not the game host or the Windows XAML Islands path.

## Failure and change

An x86 Wine Mono 11.3 hello program stalls before Main. The first raw signal
shows `syscall_32to64` executing with `cs=0107`. Its RIP-relative load of
`cs32_sel` instead reads absolute address `0x2ec5`, consistent with 32-bit
decoding. At that point the transition descriptor still contains the correct
64-bit selector, `0x2b`. A native generated-call rewrite reproduces the same
fault without Mono. Merely changing the indirect far jump to an immediate far
jump does not fix it.

The patch enters 64-bit mode using `push selector; push address; retf`. It
returns to 32-bit mode through Wine's existing complete `iretq` frame instead
of the fast far-jump path. The Unix-call entry also saves EFlags for that
complete return. Changing only entry allows hello and GUID/file I/O to pass,
but managed worker threads still fail; both directions are necessary for the
tested CLR matrix.

This identifies a Wine/Rosetta transition failure and a tested workaround.
It does not establish the internal cause in Apple's translator or support for
other Wine versions, Intel Macs, Linux, or other CPU emulators. The complete
return path adds work to each WoW64 call; performance has not been measured.

## Reproducing the patch

Use the same Wine 11.18 source archive pinned by `scripts/build-wined3d.sh`:

```text
c6282f6d4daecf11f3ab9f9a2b6c598f67be7e28c363ba2cae8a3c44b611e7a4
```

Apply `wine/wow64cpu.patch` with `patch -p1` in a separate source tree. Configure
it using the macOS compiler/toolchain settings in `scripts/build-wined3d.sh`,
then build `dlls/wow64cpu/x86_64-windows/wow64cpu.dll`. Keep its Wine builtin
marker: this replaces an engine component, unlike the graphics DLLs staged
beside a game.

Test in a local clone of the Wine runtime and an explicit isolated prefix.
Replace `lib/wine/x86_64-windows/wow64cpu.dll` in that clone **and** the prefix's
`drive_c/windows/system32/wow64cpu.dll`, which may retain an older engine copy.
Record both hashes. Do not replace the installed Wine app or the user's
default prefix. Restore the original Unix `ntdll.so` after instrumentation so
the final acceptance run tests only the CPU patch.

## Checks

`wine_wow64_transition` is an x86 CTest executable. It generates a call thunk,
executes it, changes its target, and checks Unix calls and performance-counter
syscalls afterward. It needs no game, Mono, or shim. A frame pointer is retained
to keep this diagnostic's stack layout stable. On the tested stock Wine
11.18 runtime it times out after `generated TLS call passed`; with the patch it
exits zero. On Windows it is a normal native ABI regression test.

The local CLR acceptance matrix adds x86/x64 hello, x86 P/Invoke with four
concurrent Tasks and managed exceptions/GC, x86 GUID/file I/O, direct x86
`BCryptGenRandom`, and x64 GUID/file I/O. Require all six to exit zero with
`WINE_MONO_AOT` and DLL overrides unset. Also require the unchanged Bejeweled
host to write `[host] managed XAML Islands (STA)` before claiming the runtime
prerequisite is satisfied. Reaching that line does not establish game startup:
replacement WinRT assemblies and a XAML engine are separate milestones.

## CI runtime delivery

The user selected a patched runtime built in CI. `.github/workflows/wine-clr.yml`
builds the CPU module from pinned source, replaces it in the checksum-pinned
Gcenx Wine 11.18 bundle, ad-hoc signs and verifies the modified app, and runs the
six baseline CLR probes plus the minimal worker fixture on the `macos-15` Apple Silicon runner. The release workflow
requires that job before publishing these two additional assets:

- `appx-portable-wine-clr-macos.tar.xz`: runtime and provenance manifest.
- `appx-portable-wine-clr-sources.tar.xz`: Wine source archive, patch, build script,
  and provenance manifest.

Local reproduction:

```sh
bash scripts/build-wine-clr.sh build/wine-clr
python3 scripts/package-wine-clr.py --build build/wine-clr
python3 scripts/probe-wine-clr.py \
  --wine "$PWD/build/wine-clr/runtime/Wine Devel.app/Contents/Resources/wine/bin/wine" \
  --prefix "$PWD/build/wine-clr/test-prefix" \
  --output "$PWD/build/wine-clr/probes"
```

The compiler is bundled Wine Mono's `mcs.exe`, invoked directly against its
4.8 API references with C# 5. Wine's Framework `csc.exe` wrapper injects 4.0
references even with `/noconfig`; those lack `Task.Run`. The probe runner checks
both exit status and expected output. `--skip-compile` allows fixed fixture
binaries to compare engine variants; CI compiles the tracked sources with both bundled Mono and the preinstalled
Roslyn SDK, against the same Mono API references. No CLR or Microsoft framework
is added to the packaged runtime.

For released managed x86 games on Apple Silicon, the installer verifies the
archive against the GitHub release asset's SHA-256 digest and validates the
CPU module against `wine-clr.json`. It stores the runtime under
`~/Library/Application Support/appx-portable/wine/<archive-sha256>/` without
replacing Wine Devel. The launcher chooses that executable by default and
uses `.wine-prefix/<runtime-identity>` beside the game. An explicit `WINE`
still selects the user's engine. An explicit `WINEPREFIX` is preserved, but
launching the pinned engine with a retained, mismatched CPU DLL fails with a
message instead of silently executing stale code.

Older releases without the artifact and local `--build` installs without a
release tag retain the existing Wine selection. Managed games still report
the missing projection/XAML support: fixing CLR startup does not make the
hub or Classic playable. Hosted CI publication remains distinct from the
local build, signing, and probe evidence.

## Local evidence, 2026-10-29

On macOS 27.0.1 / Apple M2, the clean packaged runtime passed all six tracked
CLR probes and the installer-generated Bejeweled launcher reached the managed
STA log line in a fresh owned prefix. The launch then exited at the missing
Windows projection, as expected. All ten native diagnostic variants passed.
The x86 cross-build and Python installer suite passed; both workflow files
passed actionlint. Hosted GitHub Actions has not been run from this checkout.

Local receipts are `build/wine-clr/package-probes/results.json`,
`build/wine-clr/installer-evidence.json`, and the associated launch/probe logs.
The minimal assembly-binding proof is under
`local/bejeweled-investigation/binding-proof/`; it proves binding in a fixture,
not that the game's Windows API surface is implemented.

The entry-only candidate originally failed the worker/exception probe, but later
runs in another prefix passed even the unchanged original binary. Exposure is
sensitive to runtime/prefix layout. The tracked worker and clock/task fixtures
exercise that behavior under both compilers; they are not claimed to reject
every entry-only build deterministically. The original failure logs are retained
under `local/bejeweled-investigation/retf-loaded-clr-smoke.log`. Both directions
remain in the narrowly targeted workaround because both wrong-mode failures
were observed, and the complete patch passes the acceptance gates.
