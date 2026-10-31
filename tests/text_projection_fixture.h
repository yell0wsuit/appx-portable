#include <windows.ui.xaml.media.h>
#include <limits>
#include <thread>
#include <atomic>
namespace text_fixture {
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
using namespace ABI::Windows::UI::Xaml::Media;
using namespace ABI::Windows::Foundation::Collections;
ComPtr<IFontFamily> retained;
class Outer final : public IInspectable {
    std::atomic<ULONG> refs_{1};
    bool* destroyed_;

public:
    ComPtr<IInspectable> inner;
    explicit Outer(bool* destroyed) : destroyed_(destroyed) {}
    ~Outer() { *destroyed_ = true; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable)) {
            *value = static_cast<IInspectable*>(this);
            AddRef();
            return S_OK;
        }
        return inner ? inner->QueryInterface(iid, value) : E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        auto refs = --refs_;
        if (!refs) delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** values) override {
        if (!count || !values) return E_POINTER;
        *count = 0;
        *values = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* value) override {
        return WindowsCreateString(L"Test.FontOuter", 14, value);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* value) override {
        if (!value) return E_POINTER;
        *value = BaseTrust;
        return S_OK;
    }
};
HRESULT Aggregation() {
    using GetFactory = HRESULT(WINAPI*)(HSTRING, IActivationFactory**);
    auto get = reinterpret_cast<GetFactory>(GetProcAddress(GetModuleHandleW(L"appxportable.dll"), "ShimGetFactory"));
    if (!get) return E_UNEXPECTED;
    ComPtr<IActivationFactory> factory;
    ComPtr<IFontFamilyFactory> typed;
    if (FAILED(get(Wrappers::HStringReference(L"Windows.UI.Xaml.Media.FontFamily").Get(), &factory)) ||
        FAILED(factory.As(&typed)))
        return E_FAIL;
    bool destroyed = false;
    ComPtr<Outer> outer;
    outer.Attach(new Outer(&destroyed));
    ComPtr<IInspectable> inner;
    ComPtr<IFontFamily> family;
    auto name = Wrappers::HStringReference(L"Unresolved test family # Ω");
    if (typed->CreateInstanceWithName(name.Get(), outer.Get(), nullptr, &family) != E_INVALIDARG || family)
        return E_FAIL;
    if (typed->CreateInstanceWithName(nullptr, outer.Get(), &inner, &family) != E_INVALIDARG || inner || family)
        return E_FAIL;
    if (FAILED(typed->CreateInstanceWithName(name.Get(), outer.Get(), &inner, &family)) || !inner || !family)
        return E_FAIL;
    outer->inner = inner;
    inner.Reset();
    outer.Reset();
    Wrappers::HString source;
    if (destroyed || FAILED(family->get_Source(source.GetAddressOf())) ||
        wcscmp(source.GetRawBuffer(nullptr), L"Unresolved test family # Ω"))
        return E_FAIL;
    family.Reset();
    return destroyed ? S_OK : E_FAIL;
}

bool Source(IFontFamily* family, const wchar_t* expected) {
    Wrappers::HString text;
    return family && SUCCEEDED(family->get_Source(text.GetAddressOf())) &&
           !wcscmp(text.GetRawBuffer(nullptr), expected);
}
HRESULT Check(int stage) {
    ComPtr<IUIElement> root, content;
    ComPtr<IUserControl> page;
    auto hr = WindowContent(&root);
    if (FAILED(hr) || FAILED(root.As(&page)) || FAILED(page->get_Content(&content)) || !content) return E_FAIL;
    ComPtr<ITextBlock> block;
    if (stage) {
        if (!Source(retained.Get(), L"/Fonts/MetaOT-Bold.otf#Meta OT") || FAILED(content.As(&block))) return E_FAIL;
        retained.Reset();
        Wrappers::HString text;
        double size = 0;
        ComPtr<IFontFamily> family;
        ComPtr<IBrush> foreground;
        TextWrapping wrapping{};
        TextAlignment alignment{};
        if (FAILED(block->get_Text(text.GetAddressOf())) || text.Length() ||
            FAILED(block->get_TextWrapping(&wrapping)) || wrapping != TextWrapping_NoWrap ||
            FAILED(block->get_TextAlignment(&alignment)) || alignment != TextAlignment_Left ||
            block->get_FontSize(&size) != E_NOTIMPL || block->get_FontFamily(&family) != E_NOTIMPL ||
            block->get_Foreground(&foreground) != E_NOTIMPL || block->get_BaselineOffset(&size) != E_NOTIMPL)
            return E_FAIL;
        return S_OK;
    }
    ComPtr<IPanel> panel;
    ComPtr<IVector<ABI::Windows::UI::Xaml::UIElement*>> children;
    ComPtr<IUIElement> child;
    if (FAILED(content.As(&panel)) || FAILED(panel->get_Children(&children)) || FAILED(children->GetAt(0, &child)) ||
        FAILED(child.As(&block)))
        return E_FAIL;
    Wrappers::HString text;
    double size = 0;
    TextWrapping wrapping{};
    TextAlignment alignment{};
    ComPtr<IFontFamily> family;
    ComPtr<IBrush> foreground;
    if (FAILED(block->get_Text(text.GetAddressOf())) || wcscmp(text.GetRawBuffer(nullptr), L"Éclair & Ω") ||
        FAILED(block->get_FontSize(&size)) || size != 40 || FAILED(block->get_FontFamily(&family)) ||
        !Source(family.Get(), L"/Fonts/MetaOT-Bold.otf#Meta OT") || FAILED(block->get_TextWrapping(&wrapping)) ||
        wrapping != TextWrapping_Wrap || FAILED(block->get_TextAlignment(&alignment)) ||
        alignment != TextAlignment_Center || FAILED(block->get_Foreground(&foreground)) ||
        !progress_fixture::Color(foreground.Get(), 255, 255, 255, 255))
        return E_FAIL;
    if (FAILED(Aggregation())) return E_FAIL;
    for (const auto& expected : {std::make_pair(1u, TextAlignment_Start), std::make_pair(2u, TextAlignment_End)}) {
        ComPtr<IUIElement> extra;
        ComPtr<ITextBlock> label;
        if (FAILED(children->GetAt(expected.first, &extra)) || FAILED(extra.As(&label)) ||
            FAILED(label->get_TextAlignment(&alignment)) || alignment != expected.second ||
            FAILED(block->put_TextAlignment(expected.second)) || FAILED(block->get_TextAlignment(&alignment)) ||
            alignment != expected.second)
            return E_FAIL;
    }
    retained = family;
    ComPtr<IFrameworkElement> element;
    Thickness margin{};
    double width = 0, height = 0;
    if (FAILED(child.As(&element)) || FAILED(element->get_Margin(&margin)) || margin.Top != 40 || margin.Bottom != 40 ||
        FAILED(element->get_Width(&width)) || width != 1300 || FAILED(element->get_Height(&height)) || height != 65)
        return E_FAIL;
    if (block->put_FontSize(-1) != E_INVALIDARG ||
        block->put_FontSize(std::numeric_limits<double>::infinity()) != E_INVALIDARG ||
        FAILED(block->put_FontSize(0)) || FAILED(block->get_FontSize(&size)) || size || FAILED(block->put_FontSize(40)))
        return E_FAIL;
    if (block->put_TextWrapping(static_cast<TextWrapping>(99)) != E_INVALIDARG ||
        block->put_TextAlignment(static_cast<TextAlignment>(99)) != E_INVALIDARG)
        return E_FAIL;
    HRESULT wrong = E_FAIL;
    bool sameSource = false;
    std::thread worker([&] {
        Wrappers::HString value;
        wrong = family->get_Source(value.GetAddressOf());
        sameSource = SUCCEEDED(wrong) && !wcscmp(value.GetRawBuffer(nullptr), L"/Fonts/MetaOT-Bold.otf#Meta OT");
    });
    worker.join();
    if (wrong != S_OK || !sameSource || FAILED(block->put_FontFamily(nullptr)) ||
        FAILED(block->put_Foreground(nullptr)))
        return E_FAIL;
    return S_OK;
}
}
extern "C" HRESULT __cdecl ValidateTextData(INT32 stage) { return text_fixture::Check(stage); }
