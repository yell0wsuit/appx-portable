// The game module loads after the shim. Its activation import must be patched
// before entry, including on x64, where no combase hotpatch is available.
#include <windows.h>
#include <roapi.h>
#include <windows.ui.viewmanagement.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
extern "C" int __cdecl RHBinder__ShimExeMain() {
    APTTYPE apartment{};
    APTTYPEQUALIFIER qualifier{};
    if (FAILED(CoGetApartmentType(&apartment, &qualifier)) || apartment != APTTYPE_MAINSTA) return 1;
    ComPtr<ABI::Windows::UI::ViewManagement::IApplicationViewStatics2> statics;
    auto hr = RoGetActivationFactory(HStringReference(L"Windows.UI.ViewManagement.ApplicationView").Get(),
                                     IID_PPV_ARGS(&statics));
    if (FAILED(hr)) return 2;
    ComPtr<ABI::Windows::UI::ViewManagement::IApplicationView> view;
    if (FAILED(statics->GetForCurrentView(&view))) return 3;
    boolean fullscreen = false;
#pragma warning(suppress : 4996)
    if (FAILED(view->get_IsFullScreen(&fullscreen)) || !fullscreen) return 4;
    return 0;
}
