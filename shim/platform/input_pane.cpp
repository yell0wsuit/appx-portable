// IInputPaneInterop is declared for Windows 10 (RS1) and later.
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000002
#include "registry.h"
#include "log.h"
#include "com/factory.h"
#include "host/host.h"
#include <inputpaneinterop.h>
#include <windows.ui.viewmanagement.h>
#include <wrl/wrappers/corewrappers.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::ViewManagement;
namespace appx {
namespace {
// InputPane.GetForCurrentView needs a CoreWindow from a package, so Windows'
// own fails here and Unity's text input then uses a null pane. A desktop
// window has its own input pane through IInputPaneInterop; the view's pane is
// the game window's, on the game's UI thread only, as on Windows.
class InputPaneFactory final : public Factory<IInputPaneStatics> {
public:
    InputPaneFactory() : Factory(L"Windows.UI.ViewManagement.InputPane") {}
    HRESULT STDMETHODCALLTYPE GetForCurrentView(IInputPane** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!HostThread() || GetCurrentThreadId() != HostThread() || !HostWindow())
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        ComPtr<IInputPaneInterop> interop;
        auto hr = OsGetActivationFactory(Wrappers::HStringReference(L"Windows.UI.ViewManagement.InputPane").Get(),
                                         IID_PPV_ARGS(&interop));
        if (SUCCEEDED(hr)) hr = interop->GetForWindow(HostWindow(), IID_PPV_ARGS(value));
        if (FAILED(hr)) Log("FAIL", "InputPane for the game window hr=%08lx", hr);
        return hr;
    }
};
HRESULT MakeInputPaneFactory(IActivationFactory** value) {
    auto factory = Make<InputPaneFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
const ShimClass inputPaneClass{L"Windows.UI.ViewManagement.InputPane", MakeInputPaneFactory};
}
