# installappx.py Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One self-contained Python file, `installappx.py`, that takes a player from a `.appxbundle`, `.appx` or extracted folder to a playable portable game folder, replacing every setup and launch script in `scripts/`.

**Architecture:** A single standard-library script at the repo root, published as a release asset. It extracts the package, picks the game package from a bundle, adds the VCLibs DLLs the manifest needs, downloads the matching x86/x64 release zip (or uses a local build folder), adds `appx-host.exe` for managed games, downloads and runs the known game patch at the same release tag, writes Store choices into `appx-portable.ini`, and optionally writes a `00_click2startme` launcher. The runtime gains an `[options]` section in `appx-portable.ini` so Store choices apply however the game is started.

**Tech Stack:** Python 3.8+ standard library only (`zipfile`, `urllib.request`, `xml.etree`, `argparse`, `struct`); C++20 shim (`shim/options.cpp`); CMake/CTest; `unittest`.

**Spec:** The design below, agreed in conversation on 2026-10-26. It is the whole spec.

## Design (agreed)

- **Players download only `installappx.py`.** Running it with no arguments walks through prompts. Every prompt has a command-line flag so tests and developers can run it with no prompts.
- **Input:** a `.appxbundle`, a `.appx`, or an already-extracted package folder. The package file is never modified.
- **Bundle:** pick the application package for x86, then neutral, then x64. Stop with an error when only ARM/ARM64 packages exist, or when the bundle has no `AppxMetadata/AppxBundleManifest.xml`. Merge every resource package's files too. Resource packages never overwrite a file that already exists, so the main package's `resources.pri` wins.
- **VCLibs:** read `PackageDependency` names from `AppxManifest.xml`. For each `Microsoft.VCLibs.110.00` / `120.00` / `140.00` whose DLLs are not already beside the game, ask for the `.appx` or a folder, and print the download link from the README. The DLLs' PE machine must match the game's.
- **Runtime:** download `appx-portable-<tag>-<arch>.zip` from the latest GitHub release, or the release given by `--release TAG`. `--build DIR` uses a local build folder instead and downloads nothing.
- **Managed games:** if the executable has a CLR header, copy `appx-host.exe`. That exists only for x86; for a managed x64 game, stop with an error.
- **Known patches:** recognized by manifest identity name and version. When a `patch/` folder sits beside `installappx.py` (a repo checkout), it is used. Otherwise the patch's files are downloaded from `raw.githubusercontent.com` at the same release tag as the DLLs and run with the same Python.
- **Store options:** a prompt offers `1` unlock trial, `2` stub IAP, `3` both, or Enter to skip. The choice is written to the `[options]` section of `appx-portable.ini`. The runtime reads those keys, and a set environment variable still overrides them.
- **Launcher:** optional (default yes). It writes `00_click2startme.bat` on Windows, or `00_click2startme.sh` elsewhere, which starts the game's executable (or `appx-host.exe`). The `.sh` keeps `run.sh`'s Wine setup and its graphics-startup retry.
- **Windows 8/8.1:** when running on Windows 8 or 8.1, or with `--desktop-copy`, clear the AppContainer flag the way `patch_exe.ps1` did. It writes `<exe>.desktop.exe` and clears the flag in place in the staged VCLibs DLLs (which are copies), and the launcher starts the desktop copy.
- **Re-running** on an existing folder re-extracts the package, replaces the DLLs and re-applies the patch. It keeps `SaveData`, `appx-portable.ini` and logs.
- **Removed:** `scripts/setup.ps1`, `setup.sh`, `setup.bat`, `play.bat`, `game_exe.ps1`, `patch_exe.ps1`, `patch_exe.bat`, `run.sh` and `appx-portable.ini` (the installer holds the template). `scripts/` keeps only the codegen tools.

## Global Constraints

- Python 3.8 syntax and standard library only: no `match`, no `X | Y` type unions, no `str.removeprefix`, no third-party imports.
- One file: `installappx.py` at the repo root imports nothing from the repo.
- GitHub repo: `yell0wsuit/appx-portable`. Release asset name: `appx-portable-<tag>-<arch>.zip`, where `<arch>` is `x86` or `x64`. The x64 zip has no `appx-host.exe`.
- Proxy DLLs: `d3dcompiler_47.dll`, `XAudio2_8.dll`, `DWrite.dll`, plus `XAudio2_9.dll` when the build folder has it.
- VCLibs DLL names and optional 140 DLLs: exactly the `$VCLibs` and `$OptionalVCLibs140` lists in `scripts/setup.ps1`.
- Package files never staged: `AppxSignature.p7x`, `AppxBlockMap.xml`, `[Content_Types].xml`, `AppxMetadata/`, `microsoft.system.package.metadata/`.
- Destination entries never overwritten by extraction: `SaveData/`, `appx-portable.ini`, `appx-portable*.log`.
- The generated `.bat` uses CRLF line endings, and the `.sh` uses LF and is made executable.
- Errors are `InstallError`; `main` prints `Error: <message>` to stderr and returns 1.

## Review Focus

- **Zip entry escaping the folder:** an entry named `../x`, `C:/x`, `/x`, or one that decodes to these (`%2E%2E/x`), is refused before any file is written. Pinned in Task 2.
- **Paths pasted with quotes:** `"C:\Games\My Game.appx"` from Explorer's "Copy as path" works at every prompt. Pinned in Task 6.
- **Re-running after playing:** saves, the edited ini, and logs survive, and the Store choice is kept. Pinned in Tasks 2, 3 and 6. The patch re-applying with its backup already present is pinned by the existing patch tests.
- **No network or no release:** a failed download names the URL and suggests `--build`, with no traceback. Pinned in Task 4.
- **VCLibs of the wrong architecture:** an x86 VCLibs `.appx` given for an x64 game is refused with both architectures named. Pinned in Task 3.

---

## File Structure

- Create: `installappx.py`. The whole installer; sections in this order: constants, PE reading, manifest, package extraction, VCLibs, runtime DLLs, settings, release download, patches, desktop copy, launchers, CLI.
- Create: `tests/python/installappx_fixtures.py`. Fake PE, `.appx`, bundle and build-folder builders shared by the installer tests, and the importlib loading of `installappx.py`.
- Create: `tests/python/test_installappx_package.py`, `test_installappx_stage.py`, `test_installappx_patches.py`, `test_installappx_launch.py`, `test_installappx_cli.py`
- Create: `tests/store_settings.cmake`. A CTest fixture that runs `storetest` beside an `appx-portable.ini`.
- Modify: `shim/options.h`, `shim/options.cpp`, `CMakeLists.txt` (store test registration, around lines 144-174)
- Modify: `.github/workflows/release.yml`, `README.md`, `docs/building.md`, `docs/islands.md`, `docs/stub-iap.md`, `docs/trial-mode.md`, `patch/*/README.md`, `.gitattributes`
- Delete: the scripts listed under "Removed" above, plus `tests/python/test_setup.py`, `test_patch_exe.py` and `test_game_exe.py` (their cases are ported into the new tests).

---

### Task 1: Runtime reads Store options from appx-portable.ini

**Files:**
- Modify: `shim/options.h:1-16`, `shim/options.cpp:27-47`
- Create: `tests/store_settings.cmake`
- Modify: `CMakeLists.txt` (after the `store_stub_iap_discovered_catalog` test)
- Modify: `docs/stub-iap.md`, `docs/trial-mode.md`

**Interfaces:**
- Produces: the ini keys `[options] UnlockTrial` and `StubIap` (an integer; nonzero means on) in `appx-portable.ini` beside the process executable. Precedence: the command-line switch turns an option on; otherwise a set environment variable decides (`1` on, anything else off); otherwise the ini decides.

- [ ] **Step 1: Write the CTest fixture `tests/store_settings.cmake`.** Model it on `tests/store_catalog.cmake`: copy `${TEST}` and `${SHIM}` into `${FIXTURE}`, write an `AppxManifest.xml`, and write `${FIXTURE}/appx-portable.ini` from `-DINI=<text>`. Then run `${FIXTURE}/storetest.exe ${ARGS}` with `cmake -E env` applying `-DENV=<list>`, where each item is `NAME=value` or `--unset=NAME`. Fail with `message(FATAL_ERROR ...)` on a nonzero result.

- [ ] **Step 2: Register four tests in `CMakeLists.txt`, each with its own fixture folder.**

| Test name | INI | ENV | ARGS |
| --- | --- | --- | --- |
| `store_unlocked_ini` | `[options]\nUnlockTrial=1` | `--unset=APPX_PORTABLE_UNLOCK_TRIAL;APPX_PORTABLE_STUB_IAP=0` | `--expect-unlocked` |
| `store_stub_iap_ini` | `[options]\nStubIap=1` | `--unset=APPX_PORTABLE_STUB_IAP;APPX_PORTABLE_UNLOCK_TRIAL=0` | `--expect-stub-iap` |
| `store_ini_env_off` | `[options]\nUnlockTrial=1\nStubIap=1` | `APPX_PORTABLE_UNLOCK_TRIAL=0;APPX_PORTABLE_STUB_IAP=0` | (none: expects locked, no stub) |
| `store_ini_absent_section` | `[storage]\nSaveLocation=game` | `--unset=APPX_PORTABLE_UNLOCK_TRIAL;--unset=APPX_PORTABLE_STUB_IAP` | (none) |

- [ ] **Step 3: Build on Windows and run the tests to see them fail.**
Run: `cmake --build --preset windows-x86 && ctest --test-dir build/windows-x86 -R "store_.*ini" --output-on-failure`
Expected: `store_unlocked_ini` and `store_stub_iap_ini` FAIL; the other two pass. This needs Windows. On macOS, cross-compile only and leave the run to CI.

- [ ] **Step 4: Implement `bool Setting(const wchar_t* variable, const wchar_t* key, const std::wstring& ini)` in `shim/options.cpp`'s anonymous namespace.** If the variable is set (length > 0), return whether it equals `L"1"`. Otherwise return `GetPrivateProfileIntW(L"options", key, 0, ini.c_str()) != 0`. Build `ini` once in `Parse()` from `GetModuleFileNameW(nullptr, ...)` with the file name replaced by `appx-portable.ini`; this runs in kernel32 only, which is safe under the loader lock. Use `Setting` for `unlockTrial` and `stubIap`, then leave the command-line loop unchanged. Add the ini key to each of the two lines in the `options.h` comment table.

- [ ] **Step 5: Run the tests to see them pass.**
Run: `ctest --test-dir build/windows-x86 -R "store_" --output-on-failure`
Expected: every `store_` test passes, including the existing env and CLI ones.

- [ ] **Step 6: Document the ini keys.** In `docs/stub-iap.md` and `docs/trial-mode.md`, add an `appx-portable.ini` `[options]` example (`StubIap=1`, `UnlockTrial=1`) as the double-click way to turn each option on. Note that `installappx.py` writes it, and that a set environment variable overrides it.

- [ ] **Step 7: Commit**

```bash
git add shim/options.h shim/options.cpp tests/store_settings.cmake CMakeLists.txt docs/stub-iap.md docs/trial-mode.md
git commit -m "feat: read Store options from appx-portable.ini"
```

---

### Task 2: Reading and extracting packages

**Files:**
- Create: `installappx.py` (constants, PE reading, manifest, extraction)
- Create: `tests/python/installappx_fixtures.py`, `tests/python/test_installappx_package.py`

**Interfaces:**
- Produces:
  - `class InstallError(Exception)`
  - `@dataclass class PEInfo: machine: int; managed: bool; dll: bool` and `pe_info(path: Path) -> PEInfo`, which raises `InstallError` for a non-PE file. `ARCHITECTURES = {0x14C: "x86", 0x8664: "x64"}`.
  - `@dataclass class Manifest: name: str; version: str; architecture: str; executable: str; dependencies: List[str]`. `executable` is POSIX-style relative, for example `Sub/Game.exe`.
  - `read_manifest(data: bytes) -> Manifest`, matching elements by local name. It rejects an absolute `Executable` or one containing `..`.
  - `extract_game(source: Path, destination: Path) -> Manifest`, which accepts a `.appxbundle`, a `.appx` or a folder.
  - `select_bundle_packages(bundle: zipfile.ZipFile) -> Tuple[str, List[str]]`, returning the application entry name and the resource entry names.
- Fixtures (`installappx_fixtures.py`): `load_installer()` (returns the module, via importlib from `ROOT / "installappx.py"`), `pe(machine=0x14C, managed=False, dll=False, appcontainer=False) -> bytes` (a full PE32/PE32+ header as built by `test_patch_exe.executable()`, CLR directory nonzero when `managed`), `manifest_xml(name="Example.Game", version="1.0.0.0", arch="x86", executable="Game.exe", vclibs=("120",)) -> bytes`, `appx(path, files: Dict[str, bytes]) -> Path`, `bundle(path, packages: List[Tuple[str, str, str, Dict[str, bytes]]]) -> Path` (each tuple is type, architecture, file name, files; it writes `AppxMetadata/AppxBundleManifest.xml`).

- [ ] **Step 1: Write the failing tests in `test_installappx_package.py`.**
  - `test_appx_extracts_and_decodes_names`: an `.appx` holding `AppxManifest.xml`, `Game.exe`, `Assets/%21_Doc.xml`, `User%20Controls/How%20To.xaml`, `AppxSignature.p7x`, `AppxBlockMap.xml`, `[Content_Types].xml` and `AppxMetadata/CodeIntegrity.cat`. Assert that `Assets/!_Doc.xml` and `User Controls/How To.xaml` exist, that none of the four metadata names exist, and that the returned `Manifest.executable == "Game.exe"`.
  - `test_refuses_escaping_entries`: for each of `../evil.txt`, `%2E%2E/evil.txt`, `/evil.txt`, `C:/evil.txt` and `a\\..\\..\\evil.txt`, assert `InstallError` and that nothing exists outside the destination.
  - `test_bundle_prefers_x86_and_merges_resources`: a bundle with application packages x64 and x86, plus a resource package that contains `Assets/Logo.scale-140.png` and its own `resources.pri`. Assert the x86 `Game.exe` bytes were staged, `Logo.scale-140.png` exists, and `resources.pri` holds the main package's bytes.
  - `test_bundle_neutral_before_x64`: application packages x64 and neutral; the neutral one is chosen.
  - `test_arm_only_bundle_fails`: application packages arm and arm64; `InstallError` message contains `ARM`.
  - `test_bundle_without_manifest_fails`: `InstallError`.
  - `test_folder_source_is_copied`: an extracted folder source; same staged result as the `.appx` case.
  - `test_rerun_keeps_saves_ini_and_logs`: stage, write `SaveData/save.dat`, `appx-portable.ini` and `appx-portable.log`, then stage again from a package that also contains `SaveData/save.dat` and `appx-portable.ini`. All three keep their bytes.
  - `test_manifest_rejects_escaping_executable`: `Executable="..\\Game.exe"` and `Executable="C:\\Game.exe"` raise `InstallError`.
  - `test_pe_info`: for the `pe()` fixture, x86/x64 machine, `managed`, `dll` round-trip; `b"MZ"` raises `InstallError`.
  - `test_python_38_syntax`: `ast.parse(source, feature_version=(3, 8))` on `installappx.py` succeeds.

- [ ] **Step 2: Run them to see them fail.**
Run: `python3 -m unittest discover -s tests/python -p "test_installappx_package.py" -v`
Expected: errors, because `installappx.py` does not exist.

- [ ] **Step 3: Implement the four interfaces above in `installappx.py`.** Read nested `.appx` entries with `zipfile.ZipFile(bundle.open(name))`. Decode each entry name with `urllib.parse.unquote` and split it on `/` and `\`. Refuse empty, `.` and `..` parts, and any first part that has a drive colon or starts with `/`. Never let a resource package overwrite an existing file. Port the PE reading from `scripts/setup.ps1` (`Read-PEMachine`, `Test-ManagedPE`).

- [ ] **Step 4: Run the tests to see them pass.**
Run: `python3 -m unittest discover -s tests/python -p "test_installappx_package.py" -v`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add installappx.py tests/python/installappx_fixtures.py tests/python/test_installappx_package.py
git commit -m "feat(installappx): extract appx, appxbundle and folder packages"
```

---

### Task 3: VCLibs, runtime DLLs and the settings file

**Files:**
- Modify: `installappx.py` (VCLibs, runtime DLLs, settings sections)
- Create: `tests/python/test_installappx_stage.py`

**Interfaces:**
- Consumes: `Manifest`, `pe_info`, `InstallError`, fixtures from Task 2.
- Produces:
  - `VCLIBS: Dict[str, Tuple[str, ...]]`, `OPTIONAL_VCLIBS140`, and `VCLIBS_DOWNLOADS: Dict[Tuple[str, str], str]`, keyed by (dependency, architecture) with the README links.
  - `missing_vclibs(manifest: Manifest, game: Path) -> List[str]`: dependency names whose DLLs are not all in `game`.
  - `add_vclibs(dependency: str, source: Path, game: Path, architecture: str) -> None`: `source` is an `.appx` or a folder. It copies that dependency's DLLs and, for 140, any optional DLLs present. It raises `InstallError` naming the dependency and both architectures on a mismatch, and `InstallError` when DLLs are missing.
  - `stage_runtime(build: Path, game: Path, architecture: str, managed: bool) -> List[str]`: copies `appxportable.dll`, the proxies, and `appx-host.exe` when managed. Each file is replaced through a temporary file and `os.replace`. Every file's machine must match. Returns the copied names. It raises `InstallError("Managed x64 games are not supported")` for a managed x64 game.
  - `SETTINGS_TEMPLATE: str`: the current `scripts/appx-portable.ini` text plus an `[options]` section with comments and `UnlockTrial=0`, `StubIap=0`.
  - `write_settings(game: Path, store: Optional[str]) -> None`: `store` is one of `None` (keep), `"none"`, `"trial"`, `"iap"`, `"both"`. It writes the template when the file is missing. It edits only the `UnlockTrial` and `StubIap` lines, adding an `[options]` section if absent, and keeps every other line and comment.
  - `read_store(game: Path) -> str`: the current choice, as one of the four strings.

- [ ] **Step 1: Write the failing tests in `test_installappx_stage.py`.**
  - `test_vclibs_from_appx_and_folder`: for 110, 120 and 140, both an `.appx` (zip with the DLLs at its root plus a `../escape.dll` entry) and a folder work; `escape.dll` never appears anywhere.
  - `test_vclibs_beside_game_need_nothing`: `missing_vclibs` returns `[]` when the DLLs are already in the game folder.
  - `test_vclibs_architecture_mismatch`: x86 DLLs for an x64 game raise `InstallError` whose message contains `x86` and `x64`.
  - `test_vclibs140_optional_dlls`: `concrt140_app.dll` and `vcruntime140_1_app.dll` in the source are copied.
  - `test_runtime_native_and_managed`: a native x86 game gets the four proxies and the shim, and no `appx-host.exe`; a managed x86 game also gets `appx-host.exe`; a managed x64 game raises `InstallError`; `XAudio2_9.dll` is copied only when present.
  - `test_runtime_wrong_machine`: an x64 `appxportable.dll` for an x86 game raises `InstallError`.
  - `test_settings_template_and_choices`: with no ini, `write_settings(game, None)` writes the template (contains `SaveLocation=game` and `[options]`). Each of the four choices gives the expected key values, and `read_store` round-trips each one.
  - `test_settings_keep_edits`: an ini with `SaveLocation=documents`, a custom comment, and no `[options]` keeps both after `write_settings(game, "both")`, and gains `UnlockTrial=1` and `StubIap=1`.

- [ ] **Step 2: Run them to see them fail.**
Run: `python3 -m unittest discover -s tests/python -p "test_installappx_stage.py" -v`
Expected: failures with `AttributeError`.

- [ ] **Step 3: Implement the interfaces above.** Read `.appx` VCLibs by exact root entry names only, as `Expand-RuntimePackage` in `setup.ps1` did, so entry paths never reach the file system.

- [ ] **Step 4: Run the tests to see them pass.**
Run: `python3 -m unittest discover -s tests/python -p "test_installappx_*.py" -v`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add installappx.py tests/python/test_installappx_stage.py
git commit -m "feat(installappx): stage VCLibs, runtime DLLs and settings"
```

---

### Task 4: Release download and known patches

**Files:**
- Modify: `installappx.py` (release download and patch sections)
- Create: `tests/python/test_installappx_patches.py`

**Interfaces:**
- Consumes: `Manifest`, `InstallError`.
- Produces:
  - `REPOSITORY = "yell0wsuit/appx-portable"`
  - `fetch(url: str) -> bytes`: `urllib.request` with a `User-Agent: installappx` header. On `URLError`/`HTTPError` it raises `InstallError(f"Could not download {url}: {reason}. Check the connection, or pass --build with a local build folder.")`. Tests patch this function; nothing else opens URLs.
  - `resolve_release(tag: Optional[str]) -> str`: `None` reads `tag_name` from `https://api.github.com/repos/{REPOSITORY}/releases/latest`.
  - `download_runtime(tag: str, architecture: str, folder: Path) -> Path`: reads `/releases/tags/{tag}`, finds the asset named `appx-portable-{tag}-{architecture}.zip`, downloads its `browser_download_url`, and extracts only plain file names (no folders) into `folder`, which it returns. It raises `InstallError` naming the asset when it is missing.
  - `@dataclass class KnownPatch: folder: str; files: Tuple[str, ...]; target: str`. `target` is `""` for the game folder, otherwise a path relative to it.
  - `KNOWN_PATCHES: Dict[Tuple[str, str], KnownPatch]`:
    - `("PopCap.BejweledLIVE", "4.0.0.10")` maps to `KnownPatch("bejeweled", ("patch.py",), "")`
    - `("Microsoft.MicrosoftSudoku", "1.7.10190.0")` maps to `KnownPatch("mssudoku", ("patch.py",), "")`
    - `("Disney.WheresMyMickey", "1.1.0.30")` maps to `KnownPatch("wheresmymickey", ("patch.py",), "WheresMyMickey.Windows8.exe")`
    - `("Ubisoft.RaymanFiestaRunWindows10Edition", "1.0.23.0")` maps to `KnownPatch("raymanwin10", ("patch.py", "CustomTitleBar.xaml"), "")`
  - `apply_known_patch(manifest: Manifest, game: Path, tag: Optional[str], local: Optional[Path]) -> Optional[str]`: returns `None` for an unknown game. It copies the patch files from `local / folder` when `local` is given, otherwise fetches `https://raw.githubusercontent.com/{REPOSITORY}/{tag}/patch/{folder}/{file}`, all into a new temporary folder. It runs `[sys.executable, "-I", patch.py, game / target]` and returns the patch's stdout. A nonzero exit raises `InstallError` with the patch's stderr.

- [ ] **Step 1: Write the failing tests in `test_installappx_patches.py`.** Patch `installer.fetch` with a dict-backed fake throughout.
  - `test_resolve_latest_and_explicit`: the latest JSON `{"tag_name": "v2026.10.1"}` gives `"v2026.10.1"`; `resolve_release("v1")` returns `"v1"` without fetching.
  - `test_download_runtime_picks_architecture`: release JSON with x86 and x64 assets; `download_runtime(tag, "x64", folder)` extracts the x64 zip's files; a zip entry `sub/../../evil.dll` is not written outside `folder`.
  - `test_missing_asset`: `InstallError` message contains `appx-portable-v1-x86.zip`.
  - `test_fetch_failure_message`: with `urllib.request.urlopen` patched to raise `URLError("offline")`, the `InstallError` message contains the URL and `--build`.
  - `test_unknown_game_has_no_patch`: returns `None` and fetches nothing.
  - `test_local_patch_runs_against_game`: a temporary `local/bejeweled/patch.py` that writes `sys.argv[1]` to a file. The recorded argument is the game folder, and nothing is fetched.
  - `test_downloaded_patch_uses_tag`: the Rayman entry fetches exactly `.../v1/patch/raymanwin10/patch.py` and `.../v1/patch/raymanwin10/CustomTitleBar.xaml`.
  - `test_mickey_targets_executable`: the recorded argument ends with `WheresMyMickey.Windows8.exe`.
  - `test_failing_patch_raises`: a patch that exits 1 with `Unsupported` on stderr raises `InstallError` containing `Unsupported`.
  - Re-applying after a re-stage (the backup already exists and matches) is the patch scripts' own behavior. It is already pinned by `test_wheresmymickey_patch.py` and `test_mssudoku_patch.py`'s "repeatable" tests, so it gets no new test here.

- [ ] **Step 2: Run them to see them fail.**
Run: `python3 -m unittest discover -s tests/python -p "test_installappx_patches.py" -v`
Expected: failures with `AttributeError`.

- [ ] **Step 3: Implement the interfaces above.**

- [ ] **Step 4: Run the tests to see them pass.**
Run: `python3 -m unittest discover -s tests/python -p "test_installappx_*.py" -v`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add installappx.py tests/python/test_installappx_patches.py
git commit -m "feat(installappx): download releases and apply known patches"
```

---

### Task 5: Windows 8/8.1 desktop copy and launchers

**Files:**
- Modify: `installappx.py` (desktop copy and launcher sections)
- Create: `tests/python/test_installappx_launch.py`

**Interfaces:**
- Consumes: `pe_info`, `VCLIBS`, `OPTIONAL_VCLIBS140`, `InstallError`.
- Produces:
  - `needs_desktop_copy() -> bool`: true on `win32` when `sys.getwindowsversion()[:2]` is `(6, 2)` or `(6, 3)`.
  - `make_desktop_copy(game: Path, executable: str) -> str`: for an x86 PE32 executable with the AppContainer flag (`0x1000` in DllCharacteristics) set, it writes `<stem>.desktop<ext>` beside it with only that bit cleared, replacing an earlier desktop copy. It clears the same bit in place in each staged VCLibs DLL, then returns the desktop executable's relative path. If the flag is already clear, or the game is not x86, it returns `executable` unchanged. The checks and byte offset are ported from `scripts/patch_exe.ps1`'s `Convert-DesktopImage`.
  - `launch_executable(executable: str, managed: bool) -> str`: `"appx-host.exe"` when managed, otherwise `executable`.
  - `write_launcher(game: Path, executable: str, windows: bool) -> Path`: writes `00_click2startme.bat` (CRLF) or `00_click2startme.sh` (LF, mode `0o755`) and returns its path.
- The `.bat` body is: `@echo off`, `cd /d "%~dp0"`, `start "" "%~dp0<exe with backslashes>" %*`.
- The `.sh` body is `scripts/run.sh` with these differences: the game folder is the script's own folder; there is no `GAME_DIRECTORY` argument; it passes `"$@"` after `--log`; there is no Python call (the executable name is written in); and it uses `WINE` when set, else `wine` on `PATH`, else the Wine Devel path. `WINEPREFIX` is left to Wine's default unless set. It keeps the `WINEDLLOVERRIDES` line, the crash-dialog registry setting, and the three-attempt `CreateDXGIFactory1` retry exactly.

- [ ] **Step 1: Write the failing tests in `test_installappx_launch.py`.** Port the five cases in `tests/python/test_patch_exe.py`, calling `make_desktop_copy` instead of the PowerShell script:
  - only bit `0x1000` changes;
  - the source is untouched;
  - an already-clear flag returns the same name and writes nothing;
  - a DLL or x64 image is left alone;
  - the staged `msvcr110.dll` has its flag cleared.

  Add:
  - `test_launch_executable`: `("Sub/Game.exe", False)` gives `"Sub/Game.exe"`, and `managed=True` gives `"appx-host.exe"`.
  - `test_bat_launcher`: the file bytes contain `\r\n` and no bare `\n`, and contain `start "" "%~dp0Sub\Game.exe" %*`.
  - `test_sh_launcher`: the file is executable, has no `\r`, contains `"$wine" "Sub/Game.exe" "${flags[@]}"`, `CreateDXGIFactory1 begin` and `--log`, and `bash -n` succeeds on it (skip when `bash` is missing).
  - `test_needs_desktop_copy`: with `sys.platform` and `sys.getwindowsversion` patched, `(6, 3)` is true, `(10, 0)` is false, and `darwin` is false.

- [ ] **Step 2: Run them to see them fail.**
Run: `python3 -m unittest discover -s tests/python -p "test_installappx_launch.py" -v`
Expected: failures with `AttributeError`.

- [ ] **Step 3: Implement the interfaces above.**

- [ ] **Step 4: Run the tests to see them pass.**
Run: `python3 -m unittest discover -s tests/python -p "test_installappx_*.py" -v`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add installappx.py tests/python/test_installappx_launch.py
git commit -m "feat(installappx): Windows 8 desktop copy and launchers"
```

---

### Task 6: Command line and prompts

**Files:**
- Modify: `installappx.py` (CLI section, `if __name__ == "__main__": sys.exit(main())`)
- Create: `tests/python/test_installappx_cli.py`

**Interfaces:**
- Consumes: everything above.
- Produces: `main(argv: Optional[List[str]] = None, ask: Callable[[str], str] = input) -> int`.
- Arguments:
  - `package` (optional positional)
  - `--dest DIR`
  - `--vclibs PATH` (repeatable; each source is tried for every missing dependency)
  - `--build DIR`
  - `--release TAG`
  - `--store {none,trial,iap,both}`
  - `--launcher` / `--no-launcher`
  - `--no-patch`
  - `--desktop-copy`
  - `--yes`: never prompt; a missing required value is an error.
- Flow:
  1. Package.
  2. Destination: the default is `<package folder>/<identity name>`, or `<name>-portable` when that equals the source folder.
  3. `extract_game`.
  4. Each missing VCLibs dependency, from the `--vclibs` sources first and then a prompt that prints `VCLIBS_DOWNLOADS`.
  5. Runtime: `--build`, or resolve and download into a temporary folder.
  6. `stage_runtime`.
  7. The patch, unless `--no-patch`. `local` is `Path(__file__).parent / "patch"` when that folder exists.
  8. The desktop copy, when needed or forced.
  9. Store: the prompt shows the current choice, and Enter keeps it.
  10. `write_settings`.
  11. The launcher: the prompt defaults to yes.
  12. A summary.
- Every prompted path is stripped of surrounding whitespace and `"` quotes.
- When `argv` was empty (a double-click), it ends with `ask("Press Enter to close.")`, also after an error.

- [ ] **Step 1: Write the failing tests in `test_installappx_cli.py`.** Use the Task 2/3 fixtures, a fake build folder through `--build`, and a fake `ask` that serves queued answers and fails if more are asked for.
  - `test_noninteractive_install`: `main([appx, "--dest", d, "--build", b, "--vclibs", v, "--store", "both", "--launcher", "--yes"])` returns 0. The game, DLLs, VCLibs, ini (`UnlockTrial=1`, `StubIap=1`) and the platform's launcher all exist.
  - `test_prompts_accept_quoted_paths`: `main([], ask)` with answers `'"<appx>"'`, `'"<dest>"'`, `'"<vclibs.appx>"'`, `"3"`, `""` (launcher default yes) and `""` (close). It returns 0 and the ini has both options on.
  - `test_store_prompt_choices`: the answers `"1"`, `"2"`, `"3"` and `""` give `trial`, `iap`, `both`, and the existing choice kept.
  - `test_yes_without_vclibs_fails`: a missing VCLibs dependency with `--yes` returns 1, and stderr names the dependency and contains the download link.
  - `test_default_destination`: with no `--dest` under `--yes`, the game lands in `<package folder>/Example.Game`.
  - `test_known_game_is_patched`: with `apply_known_patch` patched to record its call, it gets the extracted `Manifest`; with `--no-patch` it is not called.
  - `test_error_is_one_line`: a corrupt `.appx` returns 1 and prints `Error: ` with no `Traceback`.
  - `test_rerun_keeps_store_choice`: a second `--yes` run without `--store` keeps `UnlockTrial=1`.

- [ ] **Step 2: Run them to see them fail.**
Run: `python3 -m unittest discover -s tests/python -p "test_installappx_cli.py" -v`
Expected: failures.

- [ ] **Step 3: Implement `main` as specified.**

- [ ] **Step 4: Run the whole Python suite.**
Run: `python3 -m unittest discover -s tests/python -v`
Expected: all pass. The old setup, patch_exe and game_exe tests still pass or skip, because they are removed only in Task 7.

- [ ] **Step 5: Smoke-test the help text.**
Run: `python3 installappx.py --help`
Expected: a usage line listing every argument above.

- [ ] **Step 6: Commit**

```bash
git add installappx.py tests/python/test_installappx_cli.py
git commit -m "feat(installappx): interactive and scripted installs"
```

---

### Task 7: Remove the old scripts; docs and release

**Files:**
- Delete: `scripts/setup.ps1`, `scripts/setup.sh`, `scripts/setup.bat`, `scripts/play.bat`, `scripts/game_exe.ps1`, `scripts/patch_exe.ps1`, `scripts/patch_exe.bat`, `scripts/run.sh`, `scripts/appx-portable.ini`, `tests/python/test_setup.py`, `tests/python/test_patch_exe.py`, `tests/python/test_game_exe.py`
- Modify: `.gitattributes`, `.github/workflows/release.yml`, `README.md`, `docs/building.md`, `docs/islands.md`, `docs/stub-iap.md`, `docs/trial-mode.md`, `patch/bejeweled/README.md`, `patch/mssudoku/README.md`, `patch/wheresmymickey/README.md`, `patch/raymanwin10/README.md`

**Interfaces:**
- Consumes: the finished `installappx.py`.

- [ ] **Step 1: Delete the files listed above with `git rm`.** Remove the `*.bat` rule and its comment from `.gitattributes`, since no batch files remain in the repo.

- [ ] **Step 2: Check that nothing still points at them.**
Run: `git grep -n -E "setup\.(ps1|sh|bat)|play\.bat|game_exe|patch_exe|run\.sh|scripts/appx-portable\.ini"`
Expected: no output after Steps 3-5.

- [ ] **Step 3: Release workflow.** In `release.yml`'s "Publish release" step, add `installappx.py` to the files passed to `gh release create` alongside `@archives`.

- [ ] **Step 4: README.**
  - **Requirements:** Python 3 is now needed on every platform, with python.org / winget for Windows, and Wine and `python3` for macOS. Windows 8/8.1 players need a Python 3 release that still supports their Windows version.
  - **Set up and play:** replace steps 1 and 3-5 with four steps: get the `.appxbundle`/`.appx`; get VCLibs (keep the existing table); download `installappx.py` from the latest release; run `py installappx.py` or double-click it. Then add a short flag reference, a `--build` note for your own builds, and the `00_click2startme` launcher.
  - **macOS:** `python3 installappx.py ... --build build/macos-x86`, then `./00_click2startme.sh`.
  - **Troubleshooting:** the Windows 8 entry now says the installer makes the desktop copy automatically on Windows 8/8.1 (`--desktop-copy` forces it).

- [ ] **Step 5: Other docs.**
  - `docs/building.md` step 3 lists `installappx.py`'s `PROXIES` and the release workflow instead of the setup scripts.
  - `docs/islands.md` says the installer copies `appx-host.exe` and the launcher starts it.
  - `docs/stub-iap.md` and `docs/trial-mode.md` mention the installer's Store prompt.
  - Each `patch/*/README.md` gains one line saying `installappx.py` applies the patch automatically for this exact version; the manual `py patch/...` commands stay for re-running or restoring.

- [ ] **Step 6: Run the full Python suite and the codegen check.**
Run: `python3 -m unittest discover -s tests/python -v && python3 scripts/gen_proxy.py && git diff --exit-code proxy/`
Expected: all tests pass, and the generated proxies are unchanged.

- [ ] **Step 7: Commit**

```bash
git add -A scripts tests/python .gitattributes .github/workflows/release.yml README.md docs patch
git commit -m "refactor: replace setup and launch scripts with installappx.py"
```

---

## Left to the implementer

- Prompt wording and the summary's layout. Keep them short and plain.
- How `installappx.py`'s sections are split into helper functions beyond the interfaces named here.
- Temporary folder names. Clean them up however the install ends (use `tempfile.TemporaryDirectory`).
