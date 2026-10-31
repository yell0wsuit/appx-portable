#include "viewbox.h"
#include "element.h"
#include "com/factory.h"
#include "registry.h"
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
using namespace ABI::Windows::UI::Xaml::Media;
namespace appx {
namespace {
MIDL_INTERFACE("452b507b-d764-40e5-b0c0-a1812901d20f") ISourceViewbox : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE InstallChild(IUIElement * value) = 0;
};
class Viewbox final : public Element<Viewbox, IViewbox, ISourceViewbox> {
    ComPtr<IUIElement> child_;
    Stretch stretch_ = Stretch_Uniform;
    StretchDirection direction_ = StretchDirection_Both;

public:
    static constexpr auto Name = L"Windows.UI.Xaml.Controls.Viewbox";
    explicit Viewbox(IInspectable* outer) : Element(outer) {}
    HRESULT STDMETHODCALLTYPE get_Child(IUIElement** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckSourceThread();
        return FAILED(hr) ? hr : child_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Child(IUIElement*) override {
        auto hr = CheckSourceThread();
        if (FAILED(hr)) return hr;
        Log("UNIMPL", "Viewbox.Child visual parenting hr=%08lx", E_NOTIMPL);
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE InstallChild(IUIElement* value) override {
        auto hr = CheckSourceThread();
        if (FAILED(hr)) return hr;
        if (value == static_cast<IUIElement*>(this)) return E_INVALIDARG;
        child_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Stretch(Stretch* value) override {
        if (!value) return E_POINTER;
        *value = {};
        auto hr = CheckSourceThread();
        if (SUCCEEDED(hr)) *value = stretch_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_Stretch(Stretch value) override {
        auto hr = CheckSourceThread();
        if (FAILED(hr)) return hr;
        if (value < Stretch_None || value > Stretch_UniformToFill) return E_INVALIDARG;
        stretch_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_StretchDirection(StretchDirection* value) override {
        if (!value) return E_POINTER;
        *value = {};
        auto hr = CheckSourceThread();
        if (SUCCEEDED(hr)) *value = direction_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_StretchDirection(StretchDirection value) override {
        auto hr = CheckSourceThread();
        if (FAILED(hr)) return hr;
        if (value < StretchDirection_UpOnly || value > StretchDirection_Both) return E_INVALIDARG;
        direction_ = value;
        return S_OK;
    }
};
class ViewboxFactory final : public Factory<> {
public:
    ViewboxFactory() : Factory<>(Viewbox::Name) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        try {
            ComPtr<IViewbox> viewbox;
            auto hr = Compose<Viewbox>(nullptr, nullptr, viewbox.GetAddressOf());
            return FAILED(hr) ? hr : viewbox.CopyTo(value);
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        }
    }
};
HRESULT MakeViewboxFactory(IActivationFactory** value) {
    auto factory = Make<ViewboxFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
HRESULT SetSourceViewboxChild(IInspectable* viewbox, IUIElement* child) {
    if (!viewbox) return E_INVALIDARG;
    ComPtr<ISourceViewbox> target;
    auto hr = viewbox->QueryInterface(IID_PPV_ARGS(&target));
    return FAILED(hr) ? hr : target->InstallChild(child);
}
const ShimClass viewboxClass{Viewbox::Name, MakeViewboxFactory, ShimPolicy::FillMissing};
}
