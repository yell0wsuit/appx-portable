// Windows.Devices.Input's Touch, Keyboard and MouseCapabilities for Wine, which
// has none. Games read them while starting: UWP DirectX templates query touch
// in their render loop, and Leo's Fortune ends its view when keyboard
// capabilities cannot be created.
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
// Windows reports a keyboard whenever one is attached; a desktop always has one.
class KeyboardCapabilities final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIKeyboardCapabilities, FtmBase> {
    InspectableClass(L"Windows.Devices.Input.KeyboardCapabilities", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE get_KeyboardPresent(INT32* value) override {
        if (!value) return E_POINTER;
        *value = 1;
        return S_OK;
    }
};
class MouseCapabilities final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIMouseCapabilities, FtmBase> {
    InspectableClass(L"Windows.Devices.Input.MouseCapabilities", BaseTrust);
    static HRESULT Metric(int index, INT32* value) {
        if (!value) return E_POINTER;
        *value = GetSystemMetrics(index) != 0;
        return S_OK;
    }

public:
    HRESULT STDMETHODCALLTYPE get_MousePresent(INT32* value) override { return Metric(SM_MOUSEPRESENT, value); }
    HRESULT STDMETHODCALLTYPE get_VerticalWheelPresent(INT32* value) override {
        return Metric(SM_MOUSEWHEELPRESENT, value);
    }
    HRESULT STDMETHODCALLTYPE get_HorizontalWheelPresent(INT32* value) override {
        return Metric(SM_MOUSEHORIZONTALWHEELPRESENT, value);
    }
    HRESULT STDMETHODCALLTYPE get_SwapButtons(INT32* value) override { return Metric(SM_SWAPBUTTON, value); }
    HRESULT STDMETHODCALLTYPE get_NumberOfButtons(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<UINT32>(GetSystemMetrics(SM_CMOUSEBUTTONS));
        return S_OK;
    }
};
template <class Capabilities> class CapabilitiesFactory final : public Factory<> {
public:
    explicit CapabilitiesFactory(const wchar_t* name) : Factory(name) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto capabilities = Make<Capabilities>();
        return capabilities ? capabilities.CopyTo(value) : E_OUTOFMEMORY;
    }
};
template <class Capabilities> HRESULT MakeFactory(const wchar_t* name, IActivationFactory** value) {
    auto factory = Make<CapabilitiesFactory<Capabilities>>(name);
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
// The factory keeps the name it is given, so pass the literal, not the caller's string.
HRESULT MakeCapabilitiesFactory(const wchar_t* name, IActivationFactory** value) {
    static constexpr const wchar_t* touch = L"Windows.Devices.Input.TouchCapabilities";
    static constexpr const wchar_t* keyboard = L"Windows.Devices.Input.KeyboardCapabilities";
    if (wcscmp(name, touch) == 0) return MakeFactory<TouchCapabilities>(touch, value);
    if (wcscmp(name, keyboard) == 0) return MakeFactory<KeyboardCapabilities>(keyboard, value);
    return MakeFactory<MouseCapabilities>(L"Windows.Devices.Input.MouseCapabilities", value);
}
}
const ShimClass touchCapabilitiesClass{L"Windows.Devices.Input.TouchCapabilities", MakeCapabilitiesFactory,
                                       ShimPolicy::FillMissing};
const ShimClass keyboardCapabilitiesClass{L"Windows.Devices.Input.KeyboardCapabilities", MakeCapabilitiesFactory,
                                          ShimPolicy::FillMissing};
const ShimClass mouseCapabilitiesClass{L"Windows.Devices.Input.MouseCapabilities", MakeCapabilitiesFactory,
                                       ShimPolicy::FillMissing};
}
