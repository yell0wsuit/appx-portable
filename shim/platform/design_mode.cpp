// Windows.ApplicationModel.DesignMode for Wine, which has none. The Visual Studio
// page template's LayoutAwarePage asks for it while constructing; a running game
// is never in a designer, so it is always off.
#include "com/factory.h"
#include "registry.h"
#include <windows.applicationmodel.h>
#include <wrl.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::ApplicationModel;
namespace appx {
namespace {
class DesignModeFactory final : public Factory<IDesignModeStatics> {
public:
    DesignModeFactory() : Factory(L"Windows.ApplicationModel.DesignMode") {}
    HRESULT STDMETHODCALLTYPE get_DesignModeEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
};
HRESULT MakeDesignModeFactory(IActivationFactory** value) {
    auto factory = Make<DesignModeFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
const ShimClass designModeClass{L"Windows.ApplicationModel.DesignMode", MakeDesignModeFactory, ShimPolicy::FillMissing};
}
