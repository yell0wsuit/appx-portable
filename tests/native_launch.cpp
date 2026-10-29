// A native game executable that imports no proxy DLL, as a C++/CX XAML game
// does. appx-native-host.exe must start it with the shim already loaded,
// before this entry point, and pass on its own arguments.
#include <windows.h>
#include <roapi.h>
#include <windows.ui.viewmanagement.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cwchar>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int) {
    if (!GetModuleHandleW(L"appxportable.dll")) return 11;
    // The shim clears the host's mark so processes the game starts lack it.
    if (GetEnvironmentVariableW(L"APPX_PORTABLE_HOSTED", nullptr, 0)) return 12;
    if (!wcsstr(arguments, L"--fixture-argument")) return 13;
    if (FAILED(RoInitialize(RO_INIT_MULTITHREADED))) return 14;
    // Windows refuses LoadPackagedLibrary outside a package, and accepts only
    // names inside it.
    SetLastError(0);
    if (LoadPackagedLibrary(L"..\\Plugin.dll", 0) || GetLastError() != ERROR_INVALID_PARAMETER) return 15;
    HMODULE plugin = LoadPackagedLibrary(L"Plugin.dll", 0);
    if (!plugin) return 16;
    auto served = reinterpret_cast<int(__cdecl*)()>(GetProcAddress(plugin, "ActivationServed"));
    if (!served) return 17;
    if (served() != 0) return 18;
    // C++/WinRT, as in Unity's player, finds RoGetActivationFactory in combase
    // at run time rather than importing it; it must reach the shim too.
    using FactoryCall = HRESULT(WINAPI*)(HSTRING, REFIID, void**);
    auto factory = reinterpret_cast<FactoryCall>(GetProcAddress(
        LoadLibraryExW(L"combase.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), "RoGetActivationFactory"));
    ComPtr<ABI::Windows::UI::ViewManagement::IApplicationViewStatics2> statics;
    ComPtr<ABI::Windows::UI::ViewManagement::IApplicationView> view;
    if (!factory ||
        FAILED(factory(HStringReference(L"Windows.UI.ViewManagement.ApplicationView").Get(), IID_PPV_ARGS(&statics))) ||
        FAILED(statics->GetForCurrentView(&view)))
        return 19;
    return 0;
}
