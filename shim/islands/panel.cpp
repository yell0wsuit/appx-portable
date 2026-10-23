// SwapChainBackgroundPanel (Windows 8.0) must be the root of
// a CoreWindow's visual tree, which a XAML Island has none of. Compose a real
// SwapChainPanel under the app's subclass instead, answering as the old panel.
//
// Aggregation layout, for `class SwapChainBGPanel : SwapChainBackgroundPanel`:
//   managed outer (identity)
//     -> PanelShim (non-delegating inner): ISwapChainBackgroundPanel{,Native}
//          -> SwapChainPanel inner (composed with the same managed outer)
#include <windows.h>
#undef GetCurrentTime
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.media.dxinterop.h>
#include <windows.ui.xaml.media.h>
#include <windows.ui.xaml.shapes.h>
#include <wrl.h>
#include <atomic>
#include <initializer_list>
using namespace Microsoft::WRL;
namespace C = ABI::Windows::UI::Xaml::Controls;

#include "registry.h"
#include "log.h"
namespace appx {
void IslandPointerEvent(UINT message, ABI::Windows::UI::Input::IPointerPoint* point,
                        ABI::Windows::System::VirtualKeyModifiers modifiers);
static HRESULT ConnectIslandPointerInput(IInspectable* inner) {
    using namespace ABI::Windows::UI::Xaml;
    ComPtr<IUIElement> element;
    auto hr = inner->QueryInterface(IID_PPV_ARGS(&element));
    if (FAILED(hr)) return hr;
    // SwapChainPanel rejects Background. A transparent child supplies a
    // hit-test surface without altering the composition swap chain itself.
    ComPtr<C::IPanel> panel;
    ComPtr<IInspectable> brush, rectangle;
    ComPtr<Media::ISolidColorBrush> solid;
    ComPtr<Media::IBrush> fill;
    ComPtr<Shapes::IShape> shape;
    ComPtr<IUIElement> hitSurface;
    ComPtr<ABI::Windows::Foundation::Collections::IVector<UIElement*>> children;
    hr = inner->QueryInterface(IID_PPV_ARGS(&panel));
    if (SUCCEEDED(hr))
        hr = OsActivateInstance(Wrappers::HStringReference(L"Windows.UI.Xaml.Media.SolidColorBrush").Get(), &brush);
    if (SUCCEEDED(hr)) hr = brush.As(&solid);
    if (SUCCEEDED(hr)) hr = solid->put_Color(ABI::Windows::UI::Color{0, 0, 0, 0});
    if (SUCCEEDED(hr)) hr = brush.As(&fill);
    if (SUCCEEDED(hr))
        hr = OsActivateInstance(Wrappers::HStringReference(L"Windows.UI.Xaml.Shapes.Rectangle").Get(), &rectangle);
    if (SUCCEEDED(hr)) hr = rectangle.As(&shape);
    if (SUCCEEDED(hr)) hr = shape->put_Fill(fill.Get());
    if (SUCCEEDED(hr)) hr = rectangle.As(&hitSurface);
    if (SUCCEEDED(hr)) hr = panel->get_Children(&children);
    if (SUCCEEDED(hr)) hr = children->Append(hitSurface.Get());
    if (FAILED(hr)) return hr;
    // The panel owns the delegates; they do not retain the panel/managed outer.
    for (UINT message : {WM_LBUTTONDOWN, WM_MOUSEMOVE, WM_LBUTTONUP, WM_MOUSEWHEEL}) {
        auto handler = Callback<Input::IPointerEventHandler>(
            [message](IInspectable* sender, Input::IPointerRoutedEventArgs* args) -> HRESULT {
                ComPtr<IUIElement> panel;
                ComPtr<ABI::Windows::UI::Input::IPointerPoint> point;
                ComPtr<Input::IPointer> pointer;
                ABI::Windows::System::VirtualKeyModifiers modifiers{};
                auto result = sender->QueryInterface(IID_PPV_ARGS(&panel));
                if (SUCCEEDED(result)) result = args->GetCurrentPoint(panel.Get(), &point);
                if (SUCCEEDED(result)) result = args->get_KeyModifiers(&modifiers);
                if (FAILED(result)) return result;
                IslandPointerEvent(message, point.Get(), modifiers);
                // Retain drags outside the panel through XAML, not SetCapture
                // on the desktop frame, which takes input away from the island.
                if (SUCCEEDED(args->get_Pointer(&pointer))) {
                    if (message == WM_LBUTTONDOWN) {
                        boolean captured = false;
                        panel->CapturePointer(pointer.Get(), &captured);
                    } else if (message == WM_LBUTTONUP) {
                        panel->ReleasePointerCapture(pointer.Get());
                    }
                }
                return S_OK;
            });
        if (!handler) return E_OUTOFMEMORY;
        EventRegistrationToken token{};
        if (message == WM_LBUTTONDOWN)
            hr = element->add_PointerPressed(handler.Get(), &token);
        else if (message == WM_LBUTTONUP)
            hr = element->add_PointerReleased(handler.Get(), &token);
        else if (message == WM_MOUSEWHEEL)
            hr = element->add_PointerWheelChanged(handler.Get(), &token);
        else
            hr = element->add_PointerMoved(handler.Get(), &token);
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}
class PanelShim {
    // Non-delegating IInspectable handed to the outer as its inner.
    struct Inner final : IInspectable {
        PanelShim* self;
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override { return self->InnerQuery(iid, out); }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++self->refs_; }
        ULONG STDMETHODCALLTYPE Release() override {
            ULONG left = --self->refs_;
            if (!left) delete self;
            return left;
        }
        HRESULT STDMETHODCALLTYPE GetIids(ULONG* n, IID** iids) override { return self->panelInner_->GetIids(n, iids); }
        HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* v) override {
            return WindowsCreateString(L"Windows.UI.Xaml.Controls.SwapChainBackgroundPanel", 49, v);
        }
        HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* v) override {
            *v = BaseTrust;
            return S_OK;
        }
    } inner_;
    // ISwapChainBackgroundPanel has no methods; IUnknown/IInspectable go to the outer.
    struct Panel final : C::ISwapChainBackgroundPanel {
        PanelShim* self;
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
            return self->outer_->QueryInterface(iid, out);
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return self->outer_->AddRef(); }
        ULONG STDMETHODCALLTYPE Release() override { return self->outer_->Release(); }
        HRESULT STDMETHODCALLTYPE GetIids(ULONG* n, IID** iids) override { return self->outer_->GetIids(n, iids); }
        HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* v) override {
            return self->outer_->GetRuntimeClassName(v);
        }
        HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* v) override { return self->outer_->GetTrustLevel(v); }
    } panel_;
    struct Native final : ISwapChainBackgroundPanelNative {
        PanelShim* self;
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
            return self->outer_->QueryInterface(iid, out);
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return self->outer_->AddRef(); }
        ULONG STDMETHODCALLTYPE Release() override { return self->outer_->Release(); }
        HRESULT STDMETHODCALLTYPE SetSwapChain(IDXGISwapChain* chain) override {
            // QI through the non-delegating inner; the result holds a ref on the outer.
            ISwapChainPanelNative* native = nullptr;
            HRESULT hr = self->panelInner_->QueryInterface(IID_PPV_ARGS(&native));
            if (SUCCEEDED(hr)) {
                hr = native->SetSwapChain(chain);
                native->Release();
            }
            appx::Log("TRACE", "SwapChainBackgroundPanel.SetSwapChain(%p) -> SwapChainPanel hr=0x%08lX", chain, hr);
            return hr;
        }
    } native_;
    std::atomic<ULONG> refs_{1};
    IInspectable* outer_; // not owned (aggregation)
    ComPtr<IInspectable> panelInner_;

    HRESULT InnerQuery(REFIID iid, void** out) {
        if (!out) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable)) {
            *out = &inner_;
            inner_.AddRef();
            return S_OK;
        }
        if (iid == __uuidof(C::ISwapChainBackgroundPanel)) {
            *out = &panel_;
            panel_.AddRef();
            return S_OK;
        }
        if (iid == __uuidof(ISwapChainBackgroundPanelNative)) {
            *out = &native_;
            native_.AddRef();
            return S_OK;
        }
        return panelInner_->QueryInterface(iid, out); // everything else is the SwapChainPanel
    }

public:
    static HRESULT Create(IInspectable* outer, IInspectable** inner, C::ISwapChainBackgroundPanel** value) {
        auto* shim = new (std::nothrow) PanelShim();
        if (!shim) return E_OUTOFMEMORY;
        shim->inner_.self = shim->panel_.self = shim->native_.self = shim;
        shim->outer_ = outer ? outer : static_cast<IInspectable*>(&shim->inner_);

        ComPtr<C::ISwapChainPanelFactory> factory;
        HSTRING_HEADER h;
        HSTRING n;
        const wchar_t* cls = L"Windows.UI.Xaml.Controls.SwapChainPanel";
        WindowsCreateStringReference(cls, static_cast<UINT32>(wcslen(cls)), &h, &n);
        HRESULT hr = OsGetActivationFactory(n, IID_PPV_ARGS(&factory));
        C::ISwapChainPanel* instance = nullptr;
        if (SUCCEEDED(hr)) hr = factory->CreateInstance(shim->outer_, &shim->panelInner_, &instance);
        if (instance) instance->Release(); // holds a ref on the outer; we keep the inner only
        appx::Log("TRACE", "SwapChainBackgroundPanel composed over SwapChainPanel (outer=%p) hr=0x%08lX", outer, hr);
        if (FAILED(hr)) {
            delete shim;
            return hr;
        }
        hr = ConnectIslandPointerInput(shim->panelInner_.Get());
        if (FAILED(hr)) {
            delete shim;
            return hr;
        }
        *inner = &shim->inner_; // carries the initial reference
        *value = &shim->panel_;
        shim->panel_.AddRef();
        return S_OK;
    }
};

class PanelFactory final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, IActivationFactory, C::ISwapChainBackgroundPanelFactory> {
public:
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* v) noexcept override {
        return WindowsCreateString(L"Windows.UI.Xaml.Controls.SwapChainBackgroundPanel", 49, v);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* v) noexcept override {
        *v = BaseTrust;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** v) override {
        ComPtr<IInspectable> inner;
        ComPtr<C::ISwapChainBackgroundPanel> panel;
        HRESULT hr = PanelShim::Create(nullptr, &inner, &panel);
        return SUCCEEDED(hr) ? inner.CopyTo(v) : hr;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* outer, IInspectable** inner,
                                             C::ISwapChainBackgroundPanel** value) override {
        return PanelShim::Create(outer, inner, value);
    }
};

HRESULT MakeIslandPanelFactory(IActivationFactory** out) { return Make<PanelFactory>().CopyTo(out); }
}
