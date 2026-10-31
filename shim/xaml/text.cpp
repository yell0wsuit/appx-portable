#include "text.h"
#include "element.h"
#include "generated/text.h"
#include "com/factory.h"
#include "registry.h"
#include <cmath>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
using namespace ABI::Windows::UI::Xaml::Media;
namespace appx {
namespace {
class FontFamily final : public Composable<FontFamily, IFontFamily, IAgileObject> {
    Wrappers::HString source_;

public:
    static constexpr auto Name = L"Windows.UI.Xaml.Media.FontFamily";
    explicit FontFamily(IInspectable* outer) : Composable(outer) {}
    void Initialize(HSTRING value) { source_.Attach(value); }
    HRESULT STDMETHODCALLTYPE get_Source(HSTRING* value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return source_.CopyTo(value);
    }
};
class FontFamilyFactory final : public Factory<IFontFamilyFactory> {
public:
    FontFamilyFactory() : Factory<IFontFamilyFactory>(FontFamily::Name) {}
    HRESULT STDMETHODCALLTYPE CreateInstanceWithName(HSTRING name, IInspectable* outer, IInspectable** inner,
                                                     IFontFamily** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (inner) *inner = nullptr;
        if (!WindowsGetStringLen(name) || (outer && !inner)) return E_INVALIDARG;
        try {
            // Duplicate fast-pass strings before publishing either COM interface.
            Wrappers::HString retained;
            auto hr = retained.Set(name);
            if (FAILED(hr)) return hr;
            ComPtr<IInspectable> composed;
            ComPtr<IFontFamily> family;
            hr = Compose<FontFamily>(outer, inner ? composed.GetAddressOf() : nullptr, family.GetAddressOf());
            if (FAILED(hr)) return hr;
            static_cast<FontFamily*>(family.Get())->Initialize(retained.Detach());
            *value = family.Detach();
            if (inner) *inner = composed.Detach();
            return S_OK;
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        }
    }
};
HRESULT MakeFontFactory(IActivationFactory** value) {
    auto factory = Make<FontFamilyFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
class TextBlock final : public Element<TextBlock, DefaultITextBlock> {
    Wrappers::HString text_;
    ComPtr<IFontFamily> family_;
    ComPtr<IBrush> foreground_;
    bool hasFamily_ = false, hasForeground_ = false, hasSize_ = false;
    double size_ = 0;
    TextWrapping wrapping_ = TextWrapping_NoWrap;
    TextAlignment alignment_ = TextAlignment_Left;
    HRESULT Missing(const wchar_t* name) {
        auto hr = CheckSourceThread();
        if (FAILED(hr)) return hr;
        Log("UNIMPL", "TextBlock.%ls hr=%08lx", name, E_NOTIMPL);
        return E_NOTIMPL;
    }

public:
    static constexpr auto Name = L"Windows.UI.Xaml.Controls.TextBlock";
    explicit TextBlock(IInspectable* outer) : Element(outer) {}
    HRESULT STDMETHODCALLTYPE get_Text(HSTRING* value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckSourceThread();
        return FAILED(hr) ? hr : text_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Text(HSTRING value) override {
        auto hr = CheckSourceThread();
        return FAILED(hr) ? hr : text_.Set(value);
    }
    HRESULT STDMETHODCALLTYPE get_FontSize(double* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        auto hr = CheckSourceThread();
        if (FAILED(hr)) return hr;
        if (!hasSize_) return Missing(L"FontSize default");
        *value = size_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FontSize(double value) override {
        auto hr = CheckSourceThread();
        if (FAILED(hr)) return hr;
        if (!std::isfinite(value) || value < 0) return E_INVALIDARG;
        size_ = value;
        hasSize_ = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FontFamily(IFontFamily** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckSourceThread();
        if (FAILED(hr)) return hr;
        return hasFamily_ ? family_.CopyTo(value) : Missing(L"FontFamily default");
    }
    HRESULT STDMETHODCALLTYPE put_FontFamily(IFontFamily* value) override {
        auto hr = CheckSourceThread();
        if (FAILED(hr)) return hr;
        family_ = value;
        hasFamily_ = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Foreground(IBrush** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckSourceThread();
        if (FAILED(hr)) return hr;
        return hasForeground_ ? foreground_.CopyTo(value) : Missing(L"Foreground theme value");
    }
    HRESULT STDMETHODCALLTYPE put_Foreground(IBrush* value) override {
        auto hr = CheckSourceThread();
        if (FAILED(hr)) return hr;
        foreground_ = value;
        hasForeground_ = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_TextWrapping(TextWrapping* value) override {
        if (!value) return E_POINTER;
        *value = {};
        auto hr = CheckSourceThread();
        if (SUCCEEDED(hr)) *value = wrapping_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_TextWrapping(TextWrapping value) override {
        auto hr = CheckSourceThread();
        if (FAILED(hr)) return hr;
        if (value < TextWrapping_NoWrap || value > TextWrapping_WrapWholeWords) return E_INVALIDARG;
        wrapping_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_TextAlignment(TextAlignment* value) override {
        if (!value) return E_POINTER;
        *value = {};
        auto hr = CheckSourceThread();
        if (SUCCEEDED(hr)) *value = alignment_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_TextAlignment(TextAlignment value) override {
        auto hr = CheckSourceThread();
        if (FAILED(hr)) return hr;
        if (value < TextAlignment_Center || value > TextAlignment_DetectFromContent) return E_INVALIDARG;
        alignment_ = value;
        return S_OK;
    }
};
class TextFactory final : public Factory<> {
public:
    TextFactory() : Factory<>(TextBlock::Name) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        try {
            ComPtr<ITextBlock> block;
            auto hr = Compose<TextBlock>(nullptr, nullptr, block.GetAddressOf());
            return FAILED(hr) ? hr : block.CopyTo(value);
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        }
    }
};
HRESULT MakeTextFactory(IActivationFactory** value) {
    auto factory = Make<TextFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
HRESULT MakeFontFamily(HSTRING source, IFontFamily** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    ComPtr<IActivationFactory> factory;
    auto hr = MakeFontFactory(&factory);
    ComPtr<IFontFamilyFactory> typed;
    if (SUCCEEDED(hr)) hr = factory.As(&typed);
    return FAILED(hr) ? hr : typed->CreateInstanceWithName(source, nullptr, nullptr, value);
}
const ShimClass textBlockClass{TextBlock::Name, MakeTextFactory, ShimPolicy::FillMissing};
const ShimClass fontFamilyClass{FontFamily::Name, MakeFontFactory, ShimPolicy::FillMissing};
}
