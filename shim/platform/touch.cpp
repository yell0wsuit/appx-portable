// Windows.Devices.Input.TouchCapabilities for Wine, which has none. UWP
// DirectX templates read it while starting their render loop, so a missing
// class ends that loop before the game has set up its input handlers.
#include "generated/pointer_device.h"
#include "com/factory.h"
#include "registry.h"
#include <wrl.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Devices::Input;
namespace appx {
namespace {
class TouchCapabilities final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultITouchCapabilities, FtmBase> {
    InspectableClass(L"Windows.Devices.Input.TouchCapabilities", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE get_TouchPresent(INT32* value) override {
        if (!value) return E_POINTER;
        *value = GetSystemMetrics(SM_MAXIMUMTOUCHES) > 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Contacts(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<UINT32>(GetSystemMetrics(SM_MAXIMUMTOUCHES));
        return S_OK;
    }
};
class TouchCapabilitiesFactory final : public Factory<> {
public:
    TouchCapabilitiesFactory() : Factory(L"Windows.Devices.Input.TouchCapabilities") {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto capabilities = Make<TouchCapabilities>();
        return capabilities ? capabilities.CopyTo(value) : E_OUTOFMEMORY;
    }
};
HRESULT MakeTouchCapabilitiesFactory(IActivationFactory** value) {
    auto factory = Make<TouchCapabilitiesFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
const ShimClass touchCapabilitiesClass{L"Windows.Devices.Input.TouchCapabilities", MakeTouchCapabilitiesFactory,
                                       ShimPolicy::FillMissing};
}
