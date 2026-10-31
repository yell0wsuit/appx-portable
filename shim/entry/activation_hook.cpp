#include "registry.h"
#include "islands/islands.h"
#include "log.h"
#include "package/package.h"
#include "platform/mf_stream.h"
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

static void PatchLoadedModule(HMODULE module) noexcept;
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
        PatchLoadedModule(module);
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
// import-table hook, except classes that Windows itself cannot create
// outside an app container (ShimPolicy::ReplaceForAll). Windows' callers
// reach the shim only through the hotpatch; the import-table fallback
// covers the game folder alone.
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
        game = game || (_wcsnicmp(path, exe, prefix) == 0 && path[prefix] == L'\\');
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
static bool Serve(HSTRING name, void* returnAddress) noexcept {
    return FromGame(returnAddress) ||
           GetShimPolicy(WindowsGetStringRawBuffer(name, nullptr)) == ShimPolicy::ReplaceForAll;
}
static HRESULT WINAPI GetFactory(HSTRING name, REFIID iid, void** out) noexcept {
    return GetFactoryFor(name, iid, out, Serve(name, _ReturnAddress()));
}
static HRESULT WINAPI Activate(HSTRING name, IInspectable** out) noexcept {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!Serve(name, _ReturnAddress())) return originalActivate(name, out);
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

// Wine 11 exports these as stubs that abort the process when called.
// RoRegisterForApartmentShutdown hands out a dummy cookie and never calls
// back, so vccorlib's unregistration on every normal exit has nothing to undo.
static HRESULT WINAPI UnregisterForApartmentShutdown(APARTMENT_SHUTDOWN_REGISTRATION_COOKIE) noexcept { return S_OK; }
// C++/CX catch handlers report a rethrown HRESULT here. Like Wine's own
// RoOriginateError, report that no debugger took the error.
static BOOL WINAPI TransformError(HRESULT, HRESULT, HSTRING) noexcept { return FALSE; }
// vccorlib110 captures the stack here for every Platform::Exception it makes
// from an HRESULT; there is no error-reporting stack to keep it in.
static HRESULT WINAPI CaptureErrorContext(HRESULT error) noexcept {
    Log("TRACE", "Platform::Exception hr=%08lX", static_cast<unsigned long>(error));
    return S_OK;
}
static bool OnWine() noexcept {
    static const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
    return wine;
}
// Windows refuses LoadPackagedLibrary outside a package. Unity's UWP player
// loads its IL2CPP GameAssembly.dll this way; load it from the game folder,
// accepting only the package-relative names Windows would.
static HMODULE WINAPI PackagedLibrary(LPCWSTR name, DWORD reserved) noexcept {
    try {
        std::filesystem::path relative(name ? name : L"");
        bool valid = !reserved && !relative.empty() && !relative.has_root_path();
        for (const auto& part : relative)
            if (part == L"..") valid = false;
        if (!valid) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return nullptr;
        }
        const auto path = CurrentPackage().directory / relative;
        HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        const DWORD error = GetLastError();
        Log("TRACE", "LoadPackagedLibrary %ls -> %p (error %lu)", name, module, module ? 0 : error);
        if (module) PatchLoadedModule(module);
        SetLastError(error);
        return module;
    } catch (...) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
}
// C++/WinRT, as in Unity's player, looks RoGetActivationFactory up in combase
// at run time rather than importing it. Without the combase hotpatch, such
// lookups get the activation hooks too. An API set name loads combase itself.
static FARPROC WINAPI ProcAddress(HMODULE module, LPCSTR name) noexcept {
    if (module && !IS_INTRESOURCE(name) && module == GetModuleHandleW(L"combase.dll")) {
        if (strcmp(name, "RoGetActivationFactory") == 0) return reinterpret_cast<FARPROC>(&GetFactory);
        if (strcmp(name, "RoActivateInstance") == 0) return reinterpret_cast<FARPROC>(&Activate);
    }
    return GetProcAddress(module, name);
}

// Redirect the imports of every module in the game folder, which covers the
// game and its VC runtime: the activation calls as a fallback for Wine builds
// without hotpatch prologues, and Wine's aborting stubs whenever on Wine.
static void PatchModule(HMODULE module, bool activation) noexcept {
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
            const char* name = reinterpret_cast<char*>(symbol->Name);
            void* hook = nullptr;
            if (activation && strcmp(name, "RoGetActivationFactory") == 0) hook = reinterpret_cast<void*>(&GetFactory);
            if (activation && strcmp(name, "RoActivateInstance") == 0) hook = reinterpret_cast<void*>(&Activate);
            if (activation && strcmp(name, "GetProcAddress") == 0) hook = reinterpret_cast<void*>(&ProcAddress);
            if (strcmp(name, "LoadPackagedLibrary") == 0) hook = reinterpret_cast<void*>(&PackagedLibrary);
            if (OnWine() && strcmp(name, "RoUnregisterForApartmentShutdown") == 0)
                hook = reinterpret_cast<void*>(&UnregisterForApartmentShutdown);
            if (OnWine() && strcmp(name, "RoTransformError") == 0) hook = reinterpret_cast<void*>(&TransformError);
            if (OnWine() && strcmp(name, "RoCaptureErrorContext") == 0)
                hook = reinterpret_cast<void*>(&CaptureErrorContext);
            if (OnWine() && _stricmp(reinterpret_cast<const char*>(base + imports->Name), "mfplat.dll") == 0 &&
                strcmp(name, "MFCreateMFByteStreamOnStreamEx") == 0)
                hook = reinterpret_cast<void*>(&CreateMFByteStreamOnStreamEx);
            if (!hook) continue;
            DWORD old;
            if (!VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) continue;
            InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&slots->u1.Function), hook);
            DWORD ignored;
            VirtualProtect(&slots->u1.Function, sizeof(void*), old, &ignored);
        }
    }
}
static void PatchGameModules(bool activation) noexcept {
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
        if (_wcsnicmp(path, game, prefix) == 0 && path[prefix] == L'\\') PatchModule(module, activation);
    }
    // The shim's own WRL event sources report a failing game handler through
    // RoTransformError; its activation calls must still reach combase.
    if (OnWine()) PatchModule(self, false);
}
// Without the combase hotpatch, game modules reach the activation hook only
// through their import tables.
static bool importActivation = false;
// Modules loaded from the game folder after the shim attached (in-package
// classes, packaged libraries) need the same import hooks as the game.
static void PatchLoadedModule(HMODULE module) noexcept { PatchModule(module, importActivation); }
void InstallActivationHook() noexcept {
    HMODULE combase = GetModuleHandleW(L"combase.dll");
    if (auto* original = Hotpatch(combase, "RoGetActivationFactory", reinterpret_cast<void*>(&GetFactory))) {
        originalFactory = reinterpret_cast<FactoryCall>(original);
        // RoActivateInstance may resolve through the exported factory call
        // itself; hooking it too keeps one path when it does not.
        if (auto* activate = Hotpatch(combase, "RoActivateInstance", reinterpret_cast<void*>(&Activate)))
            originalActivate = reinterpret_cast<ActivateCall>(activate);
        Log("INFO", "activation hook: combase hotpatch");
        PatchGameModules(false);
        return;
    }
    importActivation = true;
    PatchGameModules(true);
    Log("INFO", "activation hook: import tables of game modules");
}
// The launcher's own arguments, after its path, passed on to the game as given.
static const wchar_t* LauncherArguments() noexcept {
    const wchar_t* text = GetCommandLineW();
    bool quoted = false;
    for (; *text && (quoted || (*text != L' ' && *text != L'\t')); ++text)
        if (*text == L'"') quoted = !quoted;
    while (*text == L' ' || *text == L'\t') ++text;
    return text;
}
// Compiled XAML, as installappx.py detects it: .xbf files, loose or named in
// resources.pri. Such a game starts real XAML; the others run a CoreWindow.
static bool PackageHasXaml(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::recursive_directory_iterator files(
        directory, std::filesystem::directory_options::skip_permission_denied, error),
        end;
    for (; !error && files != end; files.increment(error))
        if (_wcsicmp(files->path().extension().c_str(), L".xbf") == 0) return true;
    HANDLE file = CreateFileW((directory / L"resources.pri").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::string data(GetFileSize(file, nullptr), '\0');
    DWORD read = 0;
    const bool loaded = !data.empty() && ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr);
    CloseHandle(file);
    data.resize(loaded ? read : 0);
    return data.find(".xbf") != std::string::npos || data.find(std::string(".\0x\0b\0f\0", 8)) != std::string::npos;
}
// A native C++/CX game such as a Unity IL2CPP player may import no proxy DLL,
// so nothing loads the shim before its Application::Start or CoreApplication::Run,
// which Windows refuses outside a package. Start the game suspended and queue
// LoadLibraryW of the shim as an APC: it runs once the loader has initialized
// the game's imports, before its entry point. APPX_PORTABLE_HOSTED tells that
// shim it was started this way, and whether the game is a XAML one.
static int LaunchHosted(const std::filesystem::path& executable) {
    wchar_t shim[MAX_PATH]{};
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&LaunchHosted), &self))
        return HRESULT_FROM_WIN32(GetLastError());
    const DWORD length = GetModuleFileNameW(self, shim, MAX_PATH);
    if (!length || length == MAX_PATH) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    std::wstring line = L"\"" + executable.wstring() + L"\"";
    if (*LauncherArguments()) line += L" " + std::wstring(LauncherArguments());
    STARTUPINFOW startup{sizeof startup};
    GetStartupInfoW(&startup);
    PROCESS_INFORMATION process{};
    const bool xaml = PackageHasXaml(executable.parent_path());
    SetEnvironmentVariableW(L"APPX_PORTABLE_HOSTED", xaml ? L"xaml" : L"1");
    const BOOL created = CreateProcessW(executable.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                                        nullptr, executable.parent_path().c_str(), &startup, &process);
    DWORD error = GetLastError();
    SetEnvironmentVariableW(L"APPX_PORTABLE_HOSTED", nullptr);
    if (!created) {
        Log("FAIL", "native game: cannot start %ls (error %lu)", executable.c_str(), error);
        return HRESULT_FROM_WIN32(error);
    }
    struct Handles {
        PROCESS_INFORMATION& process;
        ~Handles() {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
    } handles{process};
    const SIZE_T bytes = (wcslen(shim) + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process.hProcess, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    auto load = reinterpret_cast<PAPCFUNC>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    if (!remote || !load || !WriteProcessMemory(process.hProcess, remote, shim, bytes, nullptr) ||
        !QueueUserAPC(load, process.hThread, reinterpret_cast<ULONG_PTR>(remote)) ||
        ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        error = GetLastError();
        TerminateProcess(process.hProcess, ERROR_DLL_INIT_FAILED);
        Log("FAIL", "native game: cannot load the shim into %ls (error %lu)", executable.c_str(), error);
        return HRESULT_FROM_WIN32(error);
    }
    Log("INFO", "native game: started %ls (pid %lu) with the shim, %s", executable.c_str(), process.dwProcessId,
        xaml ? "XAML in an island" : "CoreWindow");
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    return static_cast<int>(code);
}
// .NET Native executables are forwarding stubs. Load their companion DLL only
// after the shim, then patch its imports before calling the native entry point.
// In particular, x64's import-table hook must cover SharedLibrary.dll, which
// was not loaded when the shim attached to the launcher. A native executable
// without that companion is started in a process of its own instead.
extern "C" int __cdecl ShimRunNativeApplication() noexcept {
    try {
        const auto& package = CurrentPackage();
        std::filesystem::path relative(package.executable);
        if (relative.empty() || relative.has_root_path()) return E_INVALIDARG;
        for (const auto& part : relative)
            if (part == L"..") return E_INVALIDARG;
        auto path = package.directory / std::filesystem::path(relative).replace_extension(L".dll");
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            return LaunchHosted(package.directory / relative);
        auto hr = RoInitialize(RO_INIT_SINGLETHREADED);
        if (FAILED(hr)) return hr;
        SetNativeXamlMode();
        relative.replace_extension(L".dll");
        auto module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log("FAIL", ".NET Native: loading %ls hr=%08lx", path.c_str(), hr);
            return hr;
        }
        auto entry = reinterpret_cast<int(__cdecl*)()>(GetProcAddress(module, "RHBinder__ShimExeMain"));
        if (!entry) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        PatchGameModules(true);
        Log("INFO", ".NET Native: starting %ls on STA", relative.c_str());
        return entry();
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}
}
