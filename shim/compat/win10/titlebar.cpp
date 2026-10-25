#include "titlebar.h"
#include <atomic>
#include <wrl.h>
#include <wrl/event.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::ApplicationModel::Core;
namespace appx {
class CoreTitleBar final : public RuntimeClass<RuntimeClassFlags<WinRt>, ICoreApplicationViewTitleBar, FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.Core.CoreApplicationViewTitleBar", BaseTrust);
    std::atomic<boolean> extend_{false};
    using Handler = ABI::Windows::Foundation::ITypedEventHandler<CoreApplicationViewTitleBar*, IInspectable*>;
    EventSource<Handler> layout_, visible_;

public:
    HRESULT STDMETHODCALLTYPE put_ExtendViewIntoTitleBar(boolean value) override {
        extend_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ExtendViewIntoTitleBar(boolean* value) override {
        if (!value) return E_POINTER;
        *value = extend_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SystemOverlayLeftInset(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SystemOverlayRightInset(DOUBLE* value) override {
        return get_SystemOverlayLeftInset(value);
    }
    HRESULT STDMETHODCALLTYPE get_Height(DOUBLE* value) override {
        // The desktop caption is outside the game's client area.
        return get_SystemOverlayLeftInset(value);
    }
    HRESULT STDMETHODCALLTYPE get_IsVisible(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_LayoutMetricsChanged(Handler* h, EventRegistrationToken* t) override {
        return layout_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_LayoutMetricsChanged(EventRegistrationToken t) override {
        return layout_.Remove(t);
    }
    HRESULT STDMETHODCALLTYPE add_IsVisibleChanged(Handler* h, EventRegistrationToken* t) override {
        return visible_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_IsVisibleChanged(EventRegistrationToken t) override { return visible_.Remove(t); }
};
HRESULT MakeWin10TitleBar(ICoreApplicationViewTitleBar** value) {
    if (!value) return E_POINTER;
    auto bar = Make<CoreTitleBar>();
    return bar ? bar.CopyTo(value) : E_OUTOFMEMORY;
}
}
