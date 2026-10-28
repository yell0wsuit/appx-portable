# Building appx-portable

appx-portable supports **x86 Windows** and an experimental **x64 Windows** build with the MSVC ABI. Build on Windows with MSVC or cross-compile x86 on macOS with `clang-cl`. Game files are not needed to build it or run its automated tests.

## Windows

Install Visual Studio's **Desktop development with C++** workload, including the MSVC x86 tools and a Windows SDK. You also need **CMake 3.25 or newer**, **Ninja**, and **Python 3** for script tests and code generation.

Open an **x86 Native Tools Command Prompt** for Visual Studio, change to the repository root, and run:

```bat
cmake --preset windows-x86
cmake --build --preset windows-x86
```

This creates `appxportable.dll` and the proxy DLLs `d3dcompiler_47.dll`, `XAudio2_8.dll` and `DWrite.dll` in `build\windows-x86`. Pass that folder to `installappx.py` with `--build` to install a game with them; see [Use your own build](installer.md#use-your-own-build).

For x64 games, use an **x64 Native Tools Command Prompt**, `cmake --preset windows-x64`, and `cmake --build --preset windows-x64`. This requires the MSVC x64 assembler and also builds the `XAudio2_9.dll` proxy. Pass `installappx.py` `--build build/windows-x64` and a matching x64 VCLibs framework. See [compatibility structure](compatibility.md) and the [embedded XBF support](xbf.md).

GitHub Actions builds and tests each architecture in its own job, with Ninja and a ccache compiler cache, and publishes `appx-portable-<version>-x86.zip` and `appx-portable-<version>-x64.zip` together. Both ZIPs contain the runtime and all four proxy DLLs. The x86 ZIP also contains `appx-host.exe`; x64 currently supports native games only, so its ZIP omits the CLR host. `installappx.py` downloads the Microsoft VCLibs packages from [yell0wsuit/vclibs-appx](https://github.com/yell0wsuit/vclibs-appx/releases/latest).

If CMake cannot find `cl`, use an x86 Visual Studio developer shell. If MSYS2's CMake appears first on `PATH`, put the native Windows CMake ahead of it (typically `C:\Program Files\CMake\bin`).

### Tests

```bat
ctest --test-dir build/windows-x86 --output-on-failure
python -m unittest discover -s tests/python
```

Native tests keep saves in the build folder and need no game files. PowerShell setup tests require PowerShell 7 (`pwsh`); shell setup tests are skipped on Windows.

For the supported x64 native path, run `ctest --test-dir build/windows-x64 --output-on-failure -LE managed-clr`. The `managed-clr` tests cover CLR activation interception, which currently supports x86 only. Hosted CI also excludes the GPU and media tests because those require desktop hardware.

### Managed launcher lint

On Windows with a .NET SDK and .NET Framework 4.8 targeting pack, check the managed launcher using the C# EditorConfig conventions shared with contrejour-dx:

```powershell
dotnet format host/managed.csproj --verify-no-changes
dotnet build host/managed.csproj --no-restore -warnaserror
```

Omit `--verify-no-changes` to apply formatting fixes. This project is for tooling; CMake still builds `appx-host.exe` with the Windows .NET Framework compiler. The tooling project keeps C# 5 compatibility with that compiler.

Open `appx-portable.slnx` for C# editing so the editor uses the launcher's `net48` target and C# 5 language version. Modern .NET suggestions such as replacing `DllImport` with `LibraryImport` do not apply to this Framework launcher.

## macOS cross-compilation

Install CMake, Ninja, LLVM, LLD, and xwin. With Homebrew:

```sh
brew install cmake ninja llvm lld xwin
```

From the repository root, download the Windows SDK and CRT for both architectures, then build:

```sh
xwin --accept-license --arch x86,x86_64 splat --output .sdk
cmake --preset macos-x86
cmake --build --preset macos-x86
python3 -m unittest discover -s tests/python
```

Use the `macos-x64` preset the same way for x64 games. The xwin command accepts Microsoft's SDK license. Outputs are in `build/macos-x86` or `build/macos-x64`; these are Windows binaries and require Wine to run. Gameplay under Wine remains experimental.

The toolchain defaults to LLVM in `/opt/homebrew/opt/llvm` and SDK files in `.sdk`. Set `LLVM_ROOT` and `XWIN_ROOT` if yours are elsewhere. See the [Wine setup instructions](installer.md#macos--wine-experimental) to launch.

### Wine graphics (macOS)

macOS stops at OpenGL 4.1, so Wine's WineD3D caps every Direct3D 11 device at feature level 9_3, even for games that ask for 11_x. At 9_x, WineD3D runs only legacy shader code that Wine's compiler never emits, and the GL backend never receives the game's D3D11 constants for it, so games render black. [`wine/wined3d.patch`](../wine/wined3d.patch), taken from ctr2-uwp-shim, instead runs the SM4 code and supplies OpenGL integer-conversion fallbacks for macOS.

Build the matched D3D11/DXGI/WineD3D stack with:

```sh
brew install bison llvm lld
scripts/build-wined3d.sh
```

The script runs on macOS or x86_64 Linux (`apt install bison flex clang lld llvm`). It downloads checksum-pinned Wine 11.18 source into `local/wine-src`, applies the patch, and builds `d3d11.dll`, `dxgi.dll`, and `wined3d.dll` for both architectures into `build/macos-x86` and `build/macos-x64`. Pass `x86` or `x64` to build one, and a folder after it to build elsewhere. Each folder gets a `wine-graphics.json` recording the DLLs' SHA-256 hashes. The first build takes a few minutes and uses Rosetta for the x86_64 build tools. The [Wine graphics workflow](../.github/workflows/wine-graphics.yml) builds both on Linux for pull requests that change the patch or script, and each release publishes them as `appx-portable-<tag>-wine-<arch>.zip`.

`installappx.py` stages the stack beside the game when it installs under Wine: from the release, or from a `--build` folder that has `wine-graphics.json`, refusing DLLs that do not match it. The Wine launcher loads the three DLLs as native overrides whenever they are beside the game; delete them to use Wine's own. The source pin identifies the stack being built, not the Wine version you must install; it was tested on Wine 11.18.

## Development tools

With LLVM installed, format and lint changed C++ files (replace `<files>` with their paths):

```sh
clang-format -i <files>
clang-tidy -p build/windows-x86 --extra-arg=--target=i686-pc-windows-msvc <files>
```

For a macOS build, use `-p build/macos-x86`. Point clangd's `--compile-commands-dir` at your build folder; the checked-in VS Code settings default to `build/macos-x86`.

### Regenerate SDK interfaces

Generated interfaces are checked in, so normal builds do not need this step. After editing `shim/generated/interfaces.json`, regenerate from your Windows SDK's `winrt` headers (adjust the SDK version to your installation):

```bat
python scripts\regenerate_sdk.py "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\winrt"
```

On macOS:

```sh
python3 scripts/regenerate_sdk.py .sdk/sdk/include/winrt
```

### Add a proxy DLL

A game loads appx-portable through a system DLL it imports and looks up in its own folder first. To support a game that imports none of the existing proxies (`proxy/*/`), pick one of its imports that is not a [known DLL](https://learn.microsoft.com/windows/win32/dlls/dynamic-link-library-search-order), then:

1. Create `proxy/<name>/exports.txt` listing the real DLL's exports, one per line, in the real DLL's ordinal order (games may import by ordinal).
2. Run `python scripts/gen_proxy.py` to generate the thunks, `.def` file and names header.
3. Add the name to the proxy list in `CMakeLists.txt`, `PROXIES` in `installappx.py`, and the release workflow.
