# Runtime structure

The runtime shares its app lifecycle, desktop host, storage, COM objects, and graphics redirection across supported package generations. Extend existing objects with additional documented interfaces when their behavior is compatible. Keep existing Windows 8/8.1 interfaces and their ABI layouts intact.

`shim/compat/win10/` holds Windows 10 additions that require distinct behavior: independent input queues, desktop memory reporting, caption support, and extra source-XAML elements. The package reader marks Windows 10 manifests from `TargetDeviceFamily`; source-XAML extensions and the legacy `SwapChainPanel` name fallback use this marker. Windows 8/8.1 keep their existing background-panel selection.

Architecture is separate from package generation. Build the same shared runtime for x86 or x64; import-table hooks and diagnostics use the process architecture. x86 proxies retain their inline assembly, while x64 proxies use generated MASM thunks that preserve argument registers and stack arguments. The x86 combase hotpatch is never attempted on x64.

Game-specific asset workarounds belong under `patch/`. Rayman Windows 10 keeps its
source-caption fallback in normal installations. Its alternative
[PRI/XBF connection loader](xbf.md) has passed local Windows 11 testing and a
user-reported Windows 10 Pro 22H2 laptop test (OS build 19045.7807).
The interfaces remain undocumented, so the installer retains the loose fallback.
Keep game data and Microsoft runtime binaries out of the repository.
