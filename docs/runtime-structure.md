# Runtime structure

The runtime shares its app lifecycle, desktop host, storage, COM objects, and graphics redirection across supported package generations. Extend existing objects with additional documented interfaces when their behavior is compatible. Keep existing Windows 8/8.1 interfaces and their ABI layouts intact.

`shim/compat/win10/` holds Windows 10 additions that require distinct behavior: independent input queues, desktop memory reporting, caption support, and extra source-XAML elements. The package reader marks Windows 10 manifests from `TargetDeviceFamily`; source-XAML extensions and the legacy `SwapChainPanel` name fallback use this marker. Windows 8/8.1 keep their existing background-panel selection.

Architecture is separate from package generation. Build the same shared runtime for x86 or x64; import-table hooks and diagnostics use the process architecture. x86 proxies retain their inline assembly, while x64 proxies use generated MASM thunks that preserve argument registers and stack arguments. The x86 combase hotpatch is never attempted on x64.

`appx-native-host.exe` hosts .NET Native XAML applications on Windows. It loads the shim before the game's companion native DLL, installs activation hooks for that DLL and its framework dependencies, and calls `RHBinder__ShimExeMain` on an STA thread. This uses XAML Islands without loading the desktop CLR. The normal `appx-host.exe` continues to host CLR assemblies.

When the package executable has no companion DLL, `appx-native-host.exe` starts it as a native game instead, such as a C++/CX Unity player. Such a game imports no proxy DLL, so nothing would load the shim before its `Application::Start` or `CoreApplication::Run`, which Windows refuses outside a package. The host creates the game process suspended and queues `LoadLibraryW` of the shim as an APC, which runs after the loader has initialized the game's imports and before its entry point. It sets `APPX_PORTABLE_HOSTED` for that process: `xaml` when the package has compiled XAML (`.xbf` files, loose or in `resources.pri`), else `1`. The shim clears it and continues the host's log. A XAML game runs in an island, on an STA thread of its own since C++/CX calls `Application::Start` from its MTA main thread; a CoreWindow game runs on the shim's CoreWindow, as when a proxy loads the shim. The host waits for the game and exits with its exit code. In-package modules loaded later, by class activation or `LoadPackagedLibrary`, get the same import hooks as the game, and their `GetProcAddress` lookups of combase's `RoGetActivationFactory` (C++/WinRT's way) get the activation hook; `LoadPackagedLibrary` loads package-relative names from the game folder, as Windows does inside a package. Outside XAML Islands, the shim also serves the view's `InputPane` (the game window's, through `IInputPaneInterop`) and `CoreTextServicesManager`, whose edit contexts take the game window's typed characters as `TextUpdating` and delete on Backspace when the key goes down, as Windows' text service does; Unity's text fields take text only this way. IME composition is not offered.

Game-specific asset workarounds belong under `patch/`. Rayman Windows 10 keeps its
source-caption fallback in normal installations. Its alternative
[PRI/XBF connection loader](xbf.md) has passed local Windows 11 testing and a
user-reported Windows 10 Pro 22H2 laptop test (OS build 19045.7807).
The interfaces remain undocumented, so the installer retains the loose fallback.
Keep game data and Microsoft runtime binaries out of the repository.
