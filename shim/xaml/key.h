#pragma once
#include "host/event_args.h"
#include <windows.ui.xaml.input.h>
#include <wrl/event.h>
namespace appx {
class XamlKeyEventSource {
    Microsoft::WRL::EventSource<ABI::Windows::UI::Xaml::Input::IKeyEventHandler> handlers_;

public:
    HRESULT Add(ABI::Windows::UI::Xaml::Input::IKeyEventHandler* handler, EventRegistrationToken* token) {
        if (!token) return E_POINTER;
        if (!handler) return E_INVALIDARG;
        auto wrapper = Microsoft::WRL::Callback<ABI::Windows::UI::Xaml::Input::IKeyEventHandler>(
            [retained = Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::Input::IKeyEventHandler>(handler)](
                IInspectable* sender, ABI::Windows::UI::Xaml::Input::IKeyRoutedEventArgs* args) -> HRESULT {
                boolean handled = false;
                const auto hr = args->get_Handled(&handled);
                return FAILED(hr) || handled ? hr : retained->Invoke(sender, args);
            });
        return wrapper ? handlers_.Add(wrapper.Get(), token) : E_OUTOFMEMORY;
    }
    HRESULT Remove(EventRegistrationToken token) { return handlers_.Remove(token); }
    HRESULT Raise(IInspectable* sender, ABI::Windows::UI::Xaml::Input::IKeyRoutedEventArgs* args) {
        return handlers_.InvokeAll(sender, args);
    }
};
// Internal bridge for routing host keys through shim elements.
MIDL_INTERFACE("6B94AD17-C1E3-40C5-BBA4-B140BA709F25") IXamlKeyTarget : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE RaiseKey(boolean released,
                                               ABI::Windows::UI::Xaml::Input::IKeyRoutedEventArgs * args) = 0;
};
class XamlKeyArgs final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>, ABI::Windows::UI::Xaml::Input::IKeyRoutedEventArgs,
          ABI::Windows::UI::Xaml::Input::IKeyRoutedEventArgs2, ABI::Windows::UI::Xaml::IRoutedEventArgs> {
    InspectableClass(L"Windows.UI.Xaml.Input.KeyRoutedEventArgs", BaseTrust);
    Microsoft::WRL::ComPtr<IInspectable> source_;
    ABI::Windows::System::VirtualKey key_;
    ABI::Windows::UI::Core::CorePhysicalKeyStatus status_;
    boolean handled_ = false;

public:
    XamlKeyArgs(IInspectable* source, WPARAM key, LPARAM status, bool released)
        : source_(source), key_(static_cast<ABI::Windows::System::VirtualKey>(key)),
          status_(KeyStatus(status, released)) {}
    HRESULT STDMETHODCALLTYPE get_Key(ABI::Windows::System::VirtualKey* value) override {
        if (!value) return E_POINTER;
        *value = key_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_OriginalKey(ABI::Windows::System::VirtualKey* value) override {
        return get_Key(value);
    }
    HRESULT STDMETHODCALLTYPE get_KeyStatus(ABI::Windows::UI::Core::CorePhysicalKeyStatus* value) override {
        if (!value) return E_POINTER;
        *value = status_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = handled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean value) override {
        handled_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_OriginalSource(IInspectable** value) override {
        if (!value) return E_POINTER;
        return source_.CopyTo(value);
    }
};
void XamlInputMessage(UINT message, WPARAM wparam, LPARAM lparam);
}
