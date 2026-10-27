#pragma once
#include <windows.h>
#include <activation.h>
extern "C" HRESULT WINAPI ShimGetFactory(HSTRING name, IActivationFactory** factory) noexcept;
namespace appx {
// How a shim class relates to the Windows class of the same name.
enum class ShimPolicy {
    Replace,       // the shim's class serves the game in place of Windows'
    FillMissing,   // Windows' class when it has one; the shim's fills in where it is missing (Wine)
    ReplaceForAll, // as Replace, and for Windows' own callers too: Windows' class needs an app container
};
// XAML Islands mode uses system XAML and its own island classes, so there only
// the classes registered with InIslands replace Windows'.
constexpr bool InIslands = true;
// A class the shim serves, registered by a namespace-scope ShimClass beside
// its implementation; ShimGetFactory serves every registered class.
class ShimClass {
public:
    using Make = HRESULT (*)(IActivationFactory** factory);
    using MakeNamed = HRESULT (*)(const wchar_t* name, IActivationFactory** factory);
    // name is a class name or, for a MakeNamed factory, a namespace prefix ending in '.'.
    ShimClass(const wchar_t* name, Make make, ShimPolicy policy = ShimPolicy::Replace, bool islands = false) noexcept;
    ShimClass(const wchar_t* name, MakeNamed make, ShimPolicy policy = ShimPolicy::Replace,
              bool islands = false) noexcept;
    ShimClass(const ShimClass&) = delete;
    ShimClass& operator=(const ShimClass&) = delete;
    bool Serves(const wchar_t* name) const noexcept;
    HRESULT Create(const wchar_t* name, IActivationFactory** factory) const {
        return make_ ? make_(factory) : named_(name, factory);
    }
    const ShimPolicy policy;
    const bool islands;
    const ShimClass* const next;

private:
    const wchar_t* const name_;
    const Make make_ = nullptr;
    const MakeNamed named_ = nullptr;
};
// The registered class serving name, or nullptr.
const ShimClass* FindShimClass(const wchar_t* name) noexcept;
// The policy of the shim class for name; Replace for classes the shim does not serve.
ShimPolicy GetShimPolicy(const wchar_t* name) noexcept;
HRESULT MakeCoreWindowFactory(IActivationFactory** value);
HRESULT MakeStorageFactory(IActivationFactory** value);
void InstallActivationHook() noexcept;
// The OS activation functions, bypassing the hook: for shim code that wraps an OS class.
HRESULT OsGetActivationFactory(HSTRING name, REFIID iid, void** out) noexcept;
HRESULT OsActivateInstance(HSTRING name, IInspectable** out) noexcept;
}
