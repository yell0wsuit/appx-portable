#include "registry.h"
#include "islands/islands.h"
#include "log.h"
#include "version.h"
#include <winstring.h>
#include <new>

namespace appx {
namespace {
// Classes register during static initialization, before DllMain and any
// activation, so the list is complete and unchanging once the shim runs.
constinit const ShimClass* registered = nullptr;
}
ShimClass::ShimClass(const wchar_t* name, Make make, ShimPolicy policy, bool islands) noexcept
    : policy(policy), islands(islands), next(registered), name_(name), make_(make) {
    registered = this;
}
ShimClass::ShimClass(const wchar_t* name, MakeNamed make, ShimPolicy policy, bool islands) noexcept
    : policy(policy), islands(islands), next(registered), name_(name), named_(make) {
    registered = this;
}
bool ShimClass::Serves(const wchar_t* name) const noexcept {
    const size_t length = wcslen(name_);
    return named_ && name_[length - 1] == L'.' ? wcsncmp(name, name_, length) == 0 : wcscmp(name, name_) == 0;
}
const ShimClass* FindShimClass(const wchar_t* name) noexcept {
    if (!name) return nullptr;
    for (const auto* entry = registered; entry; entry = entry->next)
        if (entry->Serves(name)) return entry;
    return nullptr;
}
ShimPolicy GetShimPolicy(const wchar_t* name) noexcept {
    const auto* entry = FindShimClass(name);
    return entry ? entry->policy : ShimPolicy::Replace;
}
}

extern "C" HRESULT WINAPI ShimGetFactory(HSTRING name, IActivationFactory** factory) noexcept {
    if (!factory) return E_POINTER;
    *factory = nullptr;
    const wchar_t* text = WindowsGetStringRawBuffer(name, nullptr);
    try {
        const bool islands = appx::IslandsMode();
        if (islands) {
            auto hr = appx::MakeIslandFactory(text, factory);
            if (hr != CLASS_E_CLASSNOTAVAILABLE) return hr;
        }
        const auto* entry = appx::FindShimClass(text);
        if (entry && (!islands || entry->islands)) return entry->Create(text, factory);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}
extern "C" HRESULT WINAPI DllGetActivationFactory(HSTRING name, IActivationFactory** value) noexcept {
    return ShimGetFactory(name, value);
}
extern "C" HRESULT WINAPI DllCanUnloadNow() { return S_FALSE; }
extern "C" void __cdecl ShimBootstrap() noexcept {}
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // Opens each run's log and names the build, so a log shows which DLL ran.
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        appx::Log("INFO", "==== appxportable attached version=%s commit=%s pid=%lu exe=%ls ====", APPX_PORTABLE_VERSION,
                  APPX_PORTABLE_COMMIT, GetCurrentProcessId(), path);
        appx::InstallActivationHook();
    }
    return TRUE;
}
