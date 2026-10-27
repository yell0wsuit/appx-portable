#include "registry.h"
#include "islands/islands.h"
#include "log.h"
#include "package/package.h"
#include <roapi.h>
#include <psapi.h>
#include <intrin.h>
#include <algorithm>
#include <wrl/client.h>
#include <cstring>
#include <mutex>
#include <vector>

namespace appx {
using FactoryCall = HRESULT(WINAPI*)(HSTRING, REFIID, void**);
using ActivateCall = HRESULT(WINAPI*)(HSTRING, IInspectable**);
static FactoryCall originalFactory = &RoGetActivationFactory;
static ActivateCall originalActivate = &RoActivateInstance;
HRESULT OsGetActivationFactory(HSTRING name, REFIID iid, void** out) noexcept {
    return originalFactory(name, iid, out);
}
HRESULT OsActivateInstance(HSTRING name, IInspectable** out) noexcept { return originalActivate(name, out); }

// Classes the package registers in its own manifest are unknown to Windows
// outside the package; load them from their module as the package would.
static HRESULT PackageFactory(HSTRING name, IActivationFactory** factory) {
    const wchar_t* text = WindowsGetStringRawBuffer(name, nullptr);
    for (const auto& entry : CurrentPackage().classes) {
        if (entry.id != text) continue;
        // Only modules shipped in the package: a system host such as
        // CLRHost.dll (C# components) needs the package context to work.
        if (GetFileAttributesW(entry.module.c_str()) == INVALID_FILE_ATTRIBUTES) {
            Log("TRACE", "in-package class %ls: %ls is not in the game folder", text, entry.module.c_str());
            return REGDB_E_CLASSNOTREG;
        }
        HMODULE module = LoadLibraryExW(entry.module.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) {
            const DWORD error = GetLastError();
            Log("FAIL", "in-package class %ls: cannot load %ls (error %lu)", text, entry.module.c_str(), error);
            return HRESULT_FROM_WIN32(error);
        }
        using GetFactoryCall = HRESULT(WINAPI*)(HSTRING, IActivationFactory**);
        auto get = reinterpret_cast<GetFactoryCall>(GetProcAddress(module, "DllGetActivationFactory"));
        if (!get) {
            Log("FAIL", "in-package class %ls: %ls has no DllGetActivationFactory", text, entry.module.c_str());
            return CLASS_E_CLASSNOTAVAILABLE;
        }
        return get(name, factory);
    }
    return REGDB_E_CLASSNOTREG;
}
// The combase hotpatch also sees Windows' own activations (media, input,
// XAML internals); those must keep getting the real classes. Only callers in
// the game folder, and the shim itself, are served shim classes, as with the
// import-table hook.
static bool FromGame(void* returnAddress) noexcept {
    static std::mutex mutex;
    static std::vector<std::pair<HMODULE, bool>> known;
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(returnAddress), &module))
        return true; // code outside any module (thunks, JIT): treat as the game's
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& [handle, game] : known)
        if (handle == module) return game;
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&FromGame), &self);
    bool game = module == self;
    wchar_t path[MAX_PATH]{}, exe[MAX_PATH]{};
    if (!game && GetModuleFileNameW(module, path, MAX_PATH) && GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
        if (ManagedMode() && _wcsicmp(std::filesystem::path(path).filename().c_str(), L"clr.dll") == 0) game = true;
        const size_t prefix = wcsrchr(exe, L'\\') - exe;
        game = game || _wcsnicmp(path, exe, prefix) == 0 && path[prefix] == L'\\';
    }
    try {
        known.emplace_back(module, game);
    } catch (...) {}
    return game;
}
static HRESULT GetFactoryFor(HSTRING name, REFIID iid, void** out, bool game) noexcept {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!game) return originalFactory(name, iid, out);
    HRESULT hr;
    if (TraceMode()) {
        hr = originalFactory(name, iid, out);
    } else {
        // Shim classes that only fill in where the OS lacks a class (Wine)
        // leave the complete Windows implementation in place when it exists.
        if (GetShimPolicy(WindowsGetStringRawBuffer(name, nullptr)) == ShimPolicy::FillMissing) {
            hr = originalFactory(name, iid, out);
            if (SUCCEEDED(hr)) {
                LogActivation(name, iid, hr);
                return hr;
            }
            *out = nullptr;
        }
        Microsoft::WRL::ComPtr<IActivationFactory> factory;
        hr = ShimGetFactory(name, &factory);
        if (hr == CLASS_E_CLASSNOTAVAILABLE) {
            hr = originalFactory(name, iid, out);
            if (hr == REGDB_E_CLASSNOTREG) {
                *out = nullptr;
                hr = PackageFactory(name, &factory);
                if (SUCCEEDED(hr)) hr = factory->QueryInterface(iid, out);
            }
        } else if (SUCCEEDED(hr)) {
            hr = factory->QueryInterface(iid, out);
        }
    }
    LogActivation(name, iid, hr);
    return hr;
}
static HRESULT WINAPI GetFactory(HSTRING name, REFIID iid, void** out) noexcept {
    return GetFactoryFor(name, iid, out, FromGame(_ReturnAddress()));
}
static HRESULT WINAPI Activate(HSTRING name, IInspectable** out) noexcept {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!FromGame(_ReturnAddress())) return originalActivate(name, out);
    Microsoft::WRL::ComPtr<IActivationFactory> factory;
    HRESULT hr = GetFactoryFor(name, __uuidof(IActivationFactory), &factory, true);
    if (SUCCEEDED(hr)) hr = factory->ActivateInstance(out);
    return hr;
}

// Preferred: hotpatch combase's exports, which catches every caller,
// including in-package DLLs loaded later. x86 system DLLs start each export
// with `mov edi, edi` after five padding bytes; jump from the padding and
// call the original past the two-byte prologue.
static void* Hotpatch(HMODULE module, const char* name, void* hook) noexcept {
#ifdef _WIN64
    // The x86 hotpatch layout and rel32 range do not apply to x64.
    (void)module;
    (void)name;
    (void)hook;
    return nullptr;
#else
    auto* entry = module ? reinterpret_cast<BYTE*>(GetProcAddress(module, name)) : nullptr;
    if (!entry || entry[0] != 0x8B || entry[1] != 0xFF) return nullptr;
    for (int i = 1; i <= 5; ++i)
        if (entry[-i] != 0xCC && entry[-i] != 0x90) return nullptr;
    DWORD old;
    if (!VirtualProtect(entry - 5, 7, PAGE_EXECUTE_READWRITE, &old)) return nullptr;
    entry[-5] = 0xE9; // jmp rel32 to the hook
    *reinterpret_cast<INT32*>(entry - 4) = static_cast<INT32>(static_cast<BYTE*>(hook) - entry);
    InterlockedExchange16(reinterpret_cast<SHORT*>(entry), static_cast<SHORT>(0xF9EB)); // jmp short -7
    DWORD ignored;
    VirtualProtect(entry - 5, 7, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), entry - 5, 7);
    return entry + 2;
#endif
}

// Fallback (Wine builds without hotpatch prologues): redirect the imports of
// every module in the game folder, which covers the game and its VC runtime.
static void PatchModule(HMODULE module) noexcept {
    if (!module) return;
    auto* base = reinterpret_cast<BYTE*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC) return;
    auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;
    auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; imports->Name; ++imports) {
        if (!imports->OriginalFirstThunk) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* symbol = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            void* hook = nullptr;
            if (strcmp(reinterpret_cast<char*>(symbol->Name), "RoGetActivationFactory") == 0)
                hook = reinterpret_cast<void*>(&GetFactory);
            if (strcmp(reinterpret_cast<char*>(symbol->Name), "RoActivateInstance") == 0)
                hook = reinterpret_cast<void*>(&Activate);
            if (!hook) continue;
            DWORD old;
            if (!VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) continue;
            InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&slots->u1.Function), hook);
            DWORD ignored;
            VirtualProtect(&slots->u1.Function, sizeof(void*), old, &ignored);
        }
    }
}
static void PatchGameModules() noexcept {
    wchar_t game[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, game, MAX_PATH)) return;
    *wcsrchr(game, L'\\') = L'\0';
    const size_t prefix = wcslen(game);
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&PatchGameModules), &self);
    std::vector<HMODULE> modules(256);
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), modules.data(), static_cast<DWORD>(modules.size() * sizeof(HMODULE)),
                            &needed))
        return;
    modules.resize(std::min<size_t>(modules.size(), needed / sizeof(HMODULE)));
    for (HMODULE module : modules) {
        wchar_t path[MAX_PATH]{};
        if (module == self || !GetModuleFileNameW(module, path, MAX_PATH)) continue;
        if (_wcsnicmp(path, game, prefix) == 0 && path[prefix] == L'\\') PatchModule(module);
    }
}
void InstallActivationHook() noexcept {
    HMODULE combase = GetModuleHandleW(L"combase.dll");
    if (auto* original = Hotpatch(combase, "RoGetActivationFactory", reinterpret_cast<void*>(&GetFactory))) {
        originalFactory = reinterpret_cast<FactoryCall>(original);
        // RoActivateInstance may resolve through the exported factory call
        // itself; hooking it too keeps one path when it does not.
        if (auto* activate = Hotpatch(combase, "RoActivateInstance", reinterpret_cast<void*>(&Activate)))
            originalActivate = reinterpret_cast<ActivateCall>(activate);
        Log("INFO", "activation hook: combase hotpatch");
        return;
    }
    PatchGameModules();
    Log("INFO", "activation hook: import tables of game modules");
}
}
