#include "registry.h"
#include "xaml.h"
#ifndef __clang__
#pragma warning(disable : 4996)
#endif
#include "com/factory.h"
#include "xaml/element.h"
#include "generated/controls.h"
#include "generated/win10_controls.h"
#include "generated/win10_primitives.h"
#include "generated/win10_shapes.h"
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
using namespace ABI::Windows::UI::Xaml::Controls::Primitives;
using namespace ABI::Windows::UI::Xaml::Shapes;
namespace appx {
namespace {
class GridFactory final : public Factory<DefaultIGridStatics> {
    HRESULT Get(IFrameworkElement* element, UINT index, INT32* value) {
        if (!value) return E_POINTER;
        *value = 0;
        if (!element) return E_INVALIDARG;
        ComPtr<IGridPlacement> placement;
        auto hr = element->QueryInterface(IID_PPV_ARGS(&placement));
        return FAILED(hr) ? hr : placement->GetPlacement(index, value);
    }
    HRESULT Set(IFrameworkElement* element, UINT index, INT32 value) {
        if (!element) return E_INVALIDARG;
        ComPtr<IGridPlacement> placement;
        auto hr = element->QueryInterface(IID_PPV_ARGS(&placement));
        return FAILED(hr) ? hr : placement->SetPlacement(index, value);
    }

public:
    GridFactory() : Factory(L"Windows.UI.Xaml.Controls.Grid") {}
#define GRID_PROPERTY(Name, Index)                                                                                     \
    HRESULT STDMETHODCALLTYPE Get##Name(IFrameworkElement* e, INT32* v) override { return Get(e, Index, v); }          \
    HRESULT STDMETHODCALLTYPE Set##Name(IFrameworkElement* e, INT32 v) override { return Set(e, Index, v); }
    GRID_PROPERTY(Row, 0)
    GRID_PROPERTY(Column, 1)
    GRID_PROPERTY(RowSpan, 2)
    GRID_PROPERTY(ColumnSpan, 3)
#undef GRID_PROPERTY
};
class CaptionButton final
    : public Element<CaptionButton, IButton, DefaultIButtonBase, DefaultIContentControl, DefaultIControl> {
    EventSource<IRoutedEventHandler> click_;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.Button";
    explicit CaptionButton(IInspectable* outer) : Element(outer) {}
    HRESULT STDMETHODCALLTYPE add_Click(IRoutedEventHandler* h, EventRegistrationToken* t) override {
        return click_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_Click(EventRegistrationToken t) override { return click_.Remove(t); }
};
class CaptionRectangle final : public Element<CaptionRectangle, DefaultIRectangle, DefaultIShape> {
public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Shapes.Rectangle";
    explicit CaptionRectangle(IInspectable* outer) : Element(outer) {}
};
class CaptionSymbol final : public Element<CaptionSymbol, DefaultISymbolIcon, DefaultIIconElement> {
    Symbol symbol_ = Symbol_FullScreen;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.SymbolIcon";
    explicit CaptionSymbol(IInspectable* outer) : Element(outer) {}
    HRESULT STDMETHODCALLTYPE get_Symbol(Symbol* value) override {
        if (!value) return E_POINTER;
        *value = symbol_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Symbol(Symbol value) override {
        symbol_ = value;
        return S_OK;
    }
};
template <class T, class I> HRESULT Create(IInspectable** value) {
    ComPtr<I> element;
    auto hr = Compose<T>(nullptr, nullptr, element.GetAddressOf());
    return FAILED(hr) ? hr : element.CopyTo(value);
}
}
HRESULT MakeWin10GridFactory(IActivationFactory** value) {
    auto factory = Make<GridFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
const ShimClass gridClass{L"Windows.UI.Xaml.Controls.Grid", MakeWin10GridFactory};
HRESULT MakeWin10XamlElement(const wchar_t* tag, IInspectable** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    if (!wcscmp(tag, L"Grid")) return MakeGrid(value);
    if (!wcscmp(tag, L"Button")) return Create<CaptionButton, IButton>(value);
    if (!wcscmp(tag, L"Rectangle")) return Create<CaptionRectangle, IRectangle>(value);
    if (!wcscmp(tag, L"SymbolIcon")) return Create<CaptionSymbol, ISymbolIcon>(value);
    return S_FALSE;
}
}
