// IMediaElement's ABI still declares the deprecated PlayToSource property.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#else
#pragma warning(disable : 4996)
#endif
#include "element.h"
#include "compat/win10/input.h"
#include "generated/controls.h"
#include "com/factory.h"
#include "registry.h"
#include <windows.ui.xaml.media.dxinterop.h>
#include <windows.ui.xaml.markup.h>
#include <dxgi1_2.h>
#include <vector>
#include <cmath>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
using namespace ABI::Windows::UI::Xaml::Interop;
namespace appx {
class RectangleGeometry final : public Composable<RectangleGeometry, ABI::Windows::UI::Xaml::Media::IRectangleGeometry,
                                                  ABI::Windows::UI::Xaml::Media::IGeometry, DefaultIDependencyObject> {
    ABI::Windows::Foundation::Rect rect_{};

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Media.RectangleGeometry";
    explicit RectangleGeometry(IInspectable* outer) : Composable(outer) {}
    HRESULT STDMETHODCALLTYPE get_Rect(ABI::Windows::Foundation::Rect* value) override {
        if (!value) return E_POINTER;
        *value = rect_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Rect(ABI::Windows::Foundation::Rect value) override {
        if (!std::isfinite(value.X) || !std::isfinite(value.Y) || !std::isfinite(value.Width) ||
            !std::isfinite(value.Height) || value.Width < 0 || value.Height < 0)
            return E_INVALIDARG;
        rect_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Bounds(ABI::Windows::Foundation::Rect* value) override { return get_Rect(value); }
    HRESULT STDMETHODCALLTYPE get_Transform(ABI::Windows::UI::Xaml::Media::ITransform** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Transform(ABI::Windows::UI::Xaml::Media::ITransform* value) override {
        return value ? E_NOTIMPL : S_OK;
    }
};
class RectangleGeometryFactory final : public Factory<> {
public:
    RectangleGeometryFactory() : Factory(RectangleGeometry::Name) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        ComPtr<ABI::Windows::UI::Xaml::Media::IRectangleGeometry> geometry;
        auto hr = Compose<RectangleGeometry>(nullptr, nullptr, geometry.GetAddressOf());
        return FAILED(hr) ? hr : geometry.CopyTo(value);
    }
};
HRESULT MakeRectangleGeometryFactory(IActivationFactory** value) {
    auto factory = Make<RectangleGeometryFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
class __declspec(uuid("a50a4bbd-8361-469c-90da-e9a40c7474df")) PanelState : public DefaultIPanel {
    winrt::Windows::Foundation::Collections::IVector<winrt::Windows::UI::Xaml::UIElement> children_ =
        winrt::single_threaded_vector<winrt::Windows::UI::Xaml::UIElement>();
    ComPtr<ABI::Windows::UI::Xaml::Media::IBrush> background_;

public:
    HRESULT STDMETHODCALLTYPE get_Children(__FIVector_1_Windows__CUI__CXaml__CUIElement** value) override {
        if (!value) return E_POINTER;
        winrt::copy_to_abi(children_, *reinterpret_cast<void**>(value));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Background(ABI::Windows::UI::Xaml::Media::IBrush** value) override {
        if (!value) return E_POINTER;
        return background_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Background(ABI::Windows::UI::Xaml::Media::IBrush* value) override {
        background_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsItemsHost(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
};
class Panel final : public Element<Panel, DefaultISwapChainPanel, PanelState, ISwapChainPanelNative> {
    ComPtr<IDXGISwapChain> swapChain_;
    EventSource<ABI::Windows::Foundation::ITypedEventHandler<SwapChainPanel*, IInspectable*>> scaleChanged_;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.SwapChainPanel";
    explicit Panel(IInspectable* outer) : Element(outer) {}
    HRESULT STDMETHODCALLTYPE
    CreateCoreIndependentInputSource(ABI::Windows::UI::Core::CoreInputDeviceTypes devices,
                                     ABI::Windows::UI::Core::ICoreInputSourceBase** value) override {
        return MakeIndependentInputSource(devices, value);
    }
    HRESULT STDMETHODCALLTYPE get_CompositionScaleX(FLOAT* value) override {
        if (!value) return E_POINTER;
        *value = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CompositionScaleY(FLOAT* value) override {
        if (!value) return E_POINTER;
        *value = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE
    add_CompositionScaleChanged(ABI::Windows::Foundation::ITypedEventHandler<SwapChainPanel*, IInspectable*>* handler,
                                EventRegistrationToken* token) override {
        return scaleChanged_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_CompositionScaleChanged(EventRegistrationToken token) override {
        return scaleChanged_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE SetSwapChain(IDXGISwapChain* value) override {
        swapChain_ = value;
        if (value)
            SetPointerTarget(static_cast<IFrameworkElement*>(this));
        else
            ClearPointerTarget(static_cast<IFrameworkElement*>(this));
        Log("TRACE", "ISwapChainPanelNative::SetSwapChain");
        return S_OK;
    }
};
HRESULT MakePanel(IInspectable** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    ISwapChainPanel* panel = nullptr;
    auto hr = Compose<Panel>(nullptr, nullptr, &panel);
    if (FAILED(hr)) return hr;
    hr = panel->QueryInterface(IID_PPV_ARGS(value));
    panel->Release();
    return hr;
}
class BackgroundPanel final
    : public Element<BackgroundPanel, ISwapChainBackgroundPanel, PanelState, ISwapChainBackgroundPanelNative> {
    ComPtr<IDXGISwapChain> swapChain_;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.SwapChainBackgroundPanel";
    explicit BackgroundPanel(IInspectable* outer) : Element(outer) {}
    HRESULT STDMETHODCALLTYPE SetSwapChain(IDXGISwapChain* value) override {
        swapChain_ = value;
        if (value)
            SetPointerTarget(static_cast<IFrameworkElement*>(this));
        else
            ClearPointerTarget(static_cast<IFrameworkElement*>(this));
        Log("TRACE", "ISwapChainBackgroundPanelNative::SetSwapChain");
        return S_OK;
    }
};
HRESULT MakeBackgroundPanel(IInspectable** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    ISwapChainBackgroundPanel* panel = nullptr;
    auto hr = Compose<BackgroundPanel>(nullptr, nullptr, &panel);
    if (FAILED(hr)) return hr;
    hr = panel->QueryInterface(IID_PPV_ARGS(value));
    panel->Release();
    return hr;
}
// MainPage's root Grid. InitializeComponent safe_casts FindName's result to
// Grid^, so a null or non-Grid object breaks the generated field binding.
class Grid final : public Element<Grid, DefaultIGrid, PanelState> {
public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.Grid";
    explicit Grid(IInspectable* outer) : Element(outer) {}
    HRESULT STDMETHODCALLTYPE get_Background(ABI::Windows::UI::Xaml::Media::IBrush** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsItemsHost(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
};
HRESULT MakeGrid(IInspectable** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    IGrid* grid = nullptr;
    auto hr = Compose<Grid>(nullptr, nullptr, &grid);
    if (FAILED(hr)) return hr;
    hr = grid->QueryInterface(IID_PPV_ARGS(value));
    grid->Release();
    return hr;
}
// DirectX games use a Canvas as an overlay without Windows' XAML core.
class Canvas final : public Element<Canvas, ICanvas, PanelState> {
public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.Canvas";
    explicit Canvas(IInspectable* outer) : Element(outer) {}
};
class CanvasFactory final : public Factory<ICanvasFactory, ICanvasStatics> {
    HRESULT GetPosition(IUIElement* element, UINT index, DOUBLE* value) {
        if (!value) return E_POINTER;
        if (!element) return E_INVALIDARG;
        ComPtr<ICanvasPlacement> placement;
        const auto hr = element->QueryInterface(IID_PPV_ARGS(&placement));
        return FAILED(hr) ? hr : placement->GetCanvasPosition(index, value);
    }
    HRESULT SetPosition(IUIElement* element, UINT index, DOUBLE value) {
        if (!element || std::isinf(value)) return E_INVALIDARG;
        ComPtr<ICanvasPlacement> placement;
        const auto hr = element->QueryInterface(IID_PPV_ARGS(&placement));
        return FAILED(hr) ? hr : placement->SetCanvasPosition(index, value);
    }

public:
    CanvasFactory() : Factory(Canvas::Name) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        ComPtr<ICanvas> canvas;
        const auto hr = Compose<Canvas>(nullptr, nullptr, canvas.GetAddressOf());
        return FAILED(hr) ? hr : canvas.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* outer, IInspectable** inner, ICanvas** value) override {
        return Compose<Canvas>(outer, inner, value);
    }
    HRESULT STDMETHODCALLTYPE get_LeftProperty(IDependencyProperty** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        Log("UNIMPL", "Canvas.LeftProperty");
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE get_TopProperty(IDependencyProperty** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        Log("UNIMPL", "Canvas.TopProperty");
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE get_ZIndexProperty(IDependencyProperty** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        Log("UNIMPL", "Canvas.ZIndexProperty");
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetLeft(IUIElement* element, DOUBLE* value) override {
        return GetPosition(element, 0, value);
    }
    HRESULT STDMETHODCALLTYPE SetLeft(IUIElement* element, DOUBLE value) override {
        return SetPosition(element, 0, value);
    }
    HRESULT STDMETHODCALLTYPE GetTop(IUIElement* element, DOUBLE* value) override {
        return GetPosition(element, 1, value);
    }
    HRESULT STDMETHODCALLTYPE SetTop(IUIElement* element, DOUBLE value) override {
        return SetPosition(element, 1, value);
    }
    HRESULT STDMETHODCALLTYPE GetZIndex(IUIElement* element, INT32* value) override {
        if (!value) return E_POINTER;
        DOUBLE zIndex = 0;
        const auto hr = GetPosition(element, 2, &zIndex);
        *value = static_cast<INT32>(zIndex);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetZIndex(IUIElement* element, INT32 value) override {
        return SetPosition(element, 2, value);
    }
};
HRESULT MakeCanvasFactory(IActivationFactory** value) {
    auto factory = Make<CanvasFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
const ShimClass canvasClass{Canvas::Name, MakeCanvasFactory};
class TextChangedArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>, ITextChangedEventArgs, IRoutedEventArgs> {
    InspectableClass(L"Windows.UI.Xaml.Controls.TextChangedEventArgs", BaseTrust);
    ComPtr<IInspectable> source_;

public:
    explicit TextChangedArgs(IInspectable* source) : source_(source) {}
    HRESULT STDMETHODCALLTYPE get_OriginalSource(IInspectable** value) override {
        if (!value) return E_POINTER;
        return source_.CopyTo(value);
    }
};
// Text entry named in Windows 8 source XAML, which games show over their
// swap chain for names and chat. These hold the text the game sets and raise
// its change events; nothing draws them and they take no typing.
class TextBox final : public Element<TextBox, DefaultITextBox, DefaultIControl> {
    std::wstring text_;
    INT32 start_ = 0, length_ = 0, maxLength_ = 0;
    bool readOnly_ = false, acceptsReturn_ = false, spellCheck_ = true, prediction_ = true;
    TextAlignment alignment_ = TextAlignment_Left;
    TextWrapping wrapping_ = TextWrapping_NoWrap;
    ComPtr<ABI::Windows::UI::Xaml::Input::IInputScope> scope_;
    EventSource<ITextChangedEventHandler> textChanged_;
    EventSource<IRoutedEventHandler> selectionChanged_;
    EventSource<IContextMenuOpeningEventHandler> contextMenu_;

    HRESULT SetText(std::wstring text, INT32 selectionLength = -1) {
        if (text == text_) return S_OK;
        text_ = std::move(text);
        const auto hr = Select(start_, selectionLength < 0 ? length_ : selectionLength);
        if (FAILED(hr)) return hr;
        auto* source = static_cast<ITextBox*>(this);
        auto args = Make<TextChangedArgs>(source);
        return args ? textChanged_.InvokeAll(source, args.Get()) : E_OUTOFMEMORY;
    }

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.TextBox";
    explicit TextBox(IInspectable* outer) : Element(outer) {}
    HRESULT STDMETHODCALLTYPE get_Text(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(text_.data(), static_cast<UINT32>(text_.size()), value);
    }
    HRESULT STDMETHODCALLTYPE put_Text(HSTRING value) override {
        UINT32 length = 0;
        const auto* raw = WindowsGetStringRawBuffer(value, &length);
        return SetText(std::wstring(raw, length));
    }
    HRESULT STDMETHODCALLTYPE get_SelectedText(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(text_.data() + start_, static_cast<UINT32>(length_), value);
    }
    HRESULT STDMETHODCALLTYPE put_SelectedText(HSTRING value) override {
        UINT32 length = 0;
        const auto* raw = WindowsGetStringRawBuffer(value, &length);
        auto text = text_;
        text.replace(start_, length_, raw, length);
        return SetText(std::move(text), static_cast<INT32>(length));
    }
    HRESULT STDMETHODCALLTYPE get_SelectionLength(INT32* value) override {
        if (!value) return E_POINTER;
        *value = length_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_SelectionLength(INT32 value) override { return Select(start_, value); }
    HRESULT STDMETHODCALLTYPE get_SelectionStart(INT32* value) override {
        if (!value) return E_POINTER;
        *value = start_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_SelectionStart(INT32 value) override { return Select(value, length_); }
    HRESULT STDMETHODCALLTYPE get_MaxLength(INT32* value) override {
        if (!value) return E_POINTER;
        *value = maxLength_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_MaxLength(INT32 value) override {
        if (value < 0) return E_INVALIDARG;
        maxLength_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(boolean* value) override {
        if (!value) return E_POINTER;
        *value = readOnly_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsReadOnly(boolean value) override {
        readOnly_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_AcceptsReturn(boolean* value) override {
        if (!value) return E_POINTER;
        *value = acceptsReturn_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_AcceptsReturn(boolean value) override {
        acceptsReturn_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_TextAlignment(TextAlignment* value) override {
        if (!value) return E_POINTER;
        *value = alignment_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_TextAlignment(TextAlignment value) override {
        alignment_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_TextWrapping(TextWrapping* value) override {
        if (!value) return E_POINTER;
        *value = wrapping_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_TextWrapping(TextWrapping value) override {
        wrapping_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsSpellCheckEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = spellCheck_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsSpellCheckEnabled(boolean value) override {
        spellCheck_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsTextPredictionEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = prediction_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsTextPredictionEnabled(boolean value) override {
        prediction_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_InputScope(ABI::Windows::UI::Xaml::Input::IInputScope** value) override {
        if (!value) return E_POINTER;
        return scope_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_InputScope(ABI::Windows::UI::Xaml::Input::IInputScope* value) override {
        scope_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_TextChanged(ITextChangedEventHandler* handler,
                                              EventRegistrationToken* token) override {
        return textChanged_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_TextChanged(EventRegistrationToken token) override {
        return textChanged_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_SelectionChanged(IRoutedEventHandler* handler,
                                                   EventRegistrationToken* token) override {
        return selectionChanged_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_SelectionChanged(EventRegistrationToken token) override {
        return selectionChanged_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_ContextMenuOpening(IContextMenuOpeningEventHandler* handler,
                                                     EventRegistrationToken* token) override {
        return contextMenu_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_ContextMenuOpening(EventRegistrationToken token) override {
        return contextMenu_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE Select(INT32 start, INT32 length) override {
        if (start < 0 || length < 0) return E_INVALIDARG;
        start = (std::min)(start, static_cast<INT32>(text_.size()));
        length = (std::min)(length, static_cast<INT32>(text_.size()) - start);
        if (start == start_ && length == length_) return S_OK;
        start_ = start;
        length_ = length;
        auto* source = static_cast<ITextBox*>(this);
        auto args = Make<RoutedArgs>(source);
        return args ? selectionChanged_.InvokeAll(source, args.Get()) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE SelectAll() override { return Select(0, static_cast<INT32>(text_.size())); }
};
class PasswordBox final : public Element<PasswordBox, DefaultIPasswordBox, DefaultIControl> {
    Wrappers::HString password_, passwordChar_;
    INT32 maxLength_ = 0;
    bool reveal_ = true;
    EventSource<IRoutedEventHandler> passwordChanged_;
    EventSource<IContextMenuOpeningEventHandler> contextMenu_;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.PasswordBox";
    explicit PasswordBox(IInspectable* outer) : Element(outer) {
        passwordChar_.Set(L"\x25CF"); // XAML's default bullet
    }
    HRESULT STDMETHODCALLTYPE get_Password(HSTRING* value) override {
        if (!value) return E_POINTER;
        return password_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Password(HSTRING value) override {
        INT32 order = 0;
        auto hr = WindowsCompareStringOrdinal(password_.Get(), value, &order);
        if (FAILED(hr) || order == 0) return hr;
        hr = password_.Set(value);
        if (FAILED(hr)) return hr;
        auto* source = static_cast<IPasswordBox*>(this);
        auto args = Make<RoutedArgs>(source);
        return args ? passwordChanged_.InvokeAll(source, args.Get()) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_PasswordChar(HSTRING* value) override {
        if (!value) return E_POINTER;
        return passwordChar_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_PasswordChar(HSTRING value) override { return passwordChar_.Set(value); }
    HRESULT STDMETHODCALLTYPE get_IsPasswordRevealButtonEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = reveal_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsPasswordRevealButtonEnabled(boolean value) override {
        reveal_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_MaxLength(INT32* value) override {
        if (!value) return E_POINTER;
        *value = maxLength_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_MaxLength(INT32 value) override {
        if (value < 0) return E_INVALIDARG;
        maxLength_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_PasswordChanged(IRoutedEventHandler* handler,
                                                  EventRegistrationToken* token) override {
        return passwordChanged_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PasswordChanged(EventRegistrationToken token) override {
        return passwordChanged_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_ContextMenuOpening(IContextMenuOpeningEventHandler* handler,
                                                     EventRegistrationToken* token) override {
        return contextMenu_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_ContextMenuOpening(EventRegistrationToken token) override {
        return contextMenu_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE SelectAll() override { return S_OK; }
};
template <class T, class I> static HRESULT MakeElement(IInspectable** value) {
    ComPtr<I> element;
    auto hr = Compose<T>(nullptr, nullptr, element.GetAddressOf());
    return FAILED(hr) ? hr : element.CopyTo(value);
}
HRESULT MakeSourceXamlElement(const wchar_t* tag, IInspectable** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    if (!wcscmp(tag, L"Canvas")) return MakeElement<Canvas, ICanvas>(value);
    if (!wcscmp(tag, L"TextBox")) return MakeElement<TextBox, ITextBox>(value);
    if (!wcscmp(tag, L"PasswordBox")) return MakeElement<PasswordBox, IPasswordBox>(value);
    return MakeWin10XamlElement(tag, value);
}
// Lets a Frame tell the shim Page it hosts which frame that is (Page.Frame).
// The Frame clears it before dropping the page; the page never owns the frame.
struct __declspec(uuid("ce92a3c3-5de3-4473-9528-b14b510702c0")) IHostedPage : public IUnknown {
    virtual void STDMETHODCALLTYPE SetHostFrame(IFrame* frame) = 0;
};
static void SetHostFrame(IInspectable* page, IFrame* frame) {
    ComPtr<IHostedPage> hosted;
    if (page && SUCCEEDED(page->QueryInterface(IID_PPV_ARGS(&hosted)))) hosted->SetHostFrame(frame);
}
class NavigationArgs final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, ABI::Windows::UI::Xaml::Navigation::INavigationEventArgs> {
    InspectableClass(L"Windows.UI.Xaml.Navigation.NavigationEventArgs", BaseTrust);
    ComPtr<IInspectable> content_, parameter_;
    Wrappers::HString typeName_;
    TypeKind typeKind_;
    ABI::Windows::UI::Xaml::Navigation::NavigationMode mode_;
    ComPtr<ABI::Windows::Foundation::IUriRuntimeClass> uri_;

public:
    NavigationArgs(IInspectable* content, IInspectable* parameter, const TypeName& type,
                   ABI::Windows::UI::Xaml::Navigation::NavigationMode mode)
        : content_(content), parameter_(parameter), typeKind_(type.Kind), mode_(mode) {
        typeName_.Set(type.Name);
    }
    HRESULT STDMETHODCALLTYPE get_Content(IInspectable** value) override {
        if (!value) return E_POINTER;
        return content_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_Parameter(IInspectable** value) override {
        if (!value) return E_POINTER;
        return parameter_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_SourcePageType(TypeName* value) override {
        if (!value) return E_POINTER;
        value->Kind = typeKind_;
        return typeName_.CopyTo(&value->Name);
    }
    HRESULT STDMETHODCALLTYPE get_NavigationMode(ABI::Windows::UI::Xaml::Navigation::NavigationMode* value) override {
        if (!value) return E_POINTER;
        *value = mode_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Uri(ABI::Windows::Foundation::IUriRuntimeClass** value) override {
        if (!value) return E_POINTER;
        return uri_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Uri(ABI::Windows::Foundation::IUriRuntimeClass* value) override {
        uri_ = value;
        return S_OK;
    }
};
class Page final : public Element<Page, DefaultIPage, DefaultIContentControl, DefaultIUserControl, DefaultIControl,
                                  DefaultIPageOverrides, IHostedPage> {
    ComPtr<IInspectable> content_;
    ABI::Windows::UI::Xaml::Navigation::NavigationCacheMode cache_ =
        ABI::Windows::UI::Xaml::Navigation::NavigationCacheMode_Disabled;
    IFrame* frame_ = nullptr;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.Page";
    explicit Page(IInspectable* outer) : Element(outer) {}
    void STDMETHODCALLTYPE SetHostFrame(IFrame* frame) override { frame_ = frame; }
    HRESULT STDMETHODCALLTYPE get_Frame(IFrame** value) override {
        if (!value) return E_POINTER;
        *value = frame_;
        if (frame_) frame_->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE
    get_NavigationCacheMode(ABI::Windows::UI::Xaml::Navigation::NavigationCacheMode* value) override {
        if (!value) return E_POINTER;
        *value = cache_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE
    put_NavigationCacheMode(ABI::Windows::UI::Xaml::Navigation::NavigationCacheMode value) override {
        cache_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Content(IInspectable** value) override {
        if (!value) return E_POINTER;
        return content_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Content(IInspectable* value) override {
        content_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Content(IUIElement** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return content_ ? content_.CopyTo(value) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Content(IUIElement* value) override {
        content_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnNavigatedFrom(ABI::Windows::UI::Xaml::Navigation::INavigationEventArgs*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnNavigatedTo(ABI::Windows::UI::Xaml::Navigation::INavigationEventArgs*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE
    OnNavigatingFrom(ABI::Windows::UI::Xaml::Navigation::INavigatingCancelEventArgs*) override {
        return S_OK;
    }
};
class UserControl final : public Element<UserControl, DefaultIUserControl, DefaultIControl> {
    ComPtr<IUIElement> content_;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.UserControl";
    explicit UserControl(IInspectable* outer) : Element(outer) {}
    HRESULT STDMETHODCALLTYPE get_Content(IUIElement** value) override {
        if (!value) return E_POINTER;
        return content_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Content(IUIElement* value) override {
        content_ = value;
        return S_OK;
    }
};
class Frame final : public Element<Frame, DefaultIFrame, DefaultIContentControl, DefaultIControl, INavigate> {
    struct Entry {
        ComPtr<IInspectable> page, parameter;
        Wrappers::HString typeName;
        TypeKind typeKind = TypeKind_Custom;
    };
    ComPtr<IInspectable> content_;
    Entry current_;
    // Back entries keep their page instances, as if every page used
    // NavigationCacheMode.Required: GoBack returns to the same MainPage, which
    // owns the game's swap chain panel, instead of constructing a new one.
    std::vector<Entry> back_;

    static HRESULT CallOverride(IInspectable* page, bool to, IInspectable* content, const Entry& entry,
                                ABI::Windows::UI::Xaml::Navigation::NavigationMode mode) {
        ComPtr<IPageOverrides> overrides;
        if (!page || FAILED(page->QueryInterface(IID_PPV_ARGS(&overrides)))) return S_OK;
        TypeName type{entry.typeName.Get(), entry.typeKind};
        auto args = Make<NavigationArgs>(content, entry.parameter.Get(), type, mode);
        if (!args) return E_OUTOFMEMORY;
        auto hr = to ? overrides->OnNavigatedTo(args.Get()) : overrides->OnNavigatedFrom(args.Get());
        if (FAILED(hr)) Log("FAIL", "Page OnNavigated%s hr=%08lx", to ? "To" : "From", hr);
        return hr;
    }
    // XAML order: the old page's OnNavigatedFrom, the content swap, then the new
    // page's OnNavigatedTo; Loaded follows from the UI queue. On a failed
    // OnNavigatedFrom nothing changes and `next` is left intact.
    HRESULT Show(Entry& next, ABI::Windows::UI::Xaml::Navigation::NavigationMode mode) {
        auto hr = CallOverride(content_.Get(), false, next.page.Get(), next, mode);
        if (FAILED(hr)) return hr;
        if (content_) {
            if (mode == ABI::Windows::UI::Xaml::Navigation::NavigationMode_Back)
                SetHostFrame(content_.Get(), nullptr); // Dropped from the frame.
            else
                back_.push_back(std::move(current_));
        }
        content_ = next.page;
        current_ = std::move(next);
        SetHostFrame(content_.Get(), this);
        hr = CallOverride(content_.Get(), true, content_.Get(), current_, mode);
        if (FAILED(hr)) return hr;
        return IsWindowVisible(HostWindow()) ? QueueElementLoaded(content_.Get()) : S_OK;
    }

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.Frame";
    explicit Frame(IInspectable* outer) : Element(outer) {}
    ~Frame() {
        SetHostFrame(content_.Get(), nullptr);
        for (auto& entry : back_) SetHostFrame(entry.page.Get(), nullptr);
    }
    HRESULT STDMETHODCALLTYPE get_Content(IInspectable** value) override {
        if (!value) return E_POINTER;
        return content_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Content(IInspectable* value) override {
        content_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CanGoBack(boolean* value) override {
        if (!value) return E_POINTER;
        *value = !back_.empty();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BackStackDepth(INT32* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<INT32>(back_.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GoBack() override {
        if (back_.empty()) return E_ILLEGAL_METHOD_CALL;
        Log("TRACE", "Frame::GoBack(%ls)", back_.back().typeName.GetRawBuffer(nullptr));
        auto entry = std::move(back_.back());
        back_.pop_back();
        auto hr = Show(entry, ABI::Windows::UI::Xaml::Navigation::NavigationMode_Back);
        if (entry.page) back_.push_back(std::move(entry)); // OnNavigatedFrom refused; restore.
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_CanGoForward(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Navigate(TypeName type, boolean* result) override {
        return Navigate(type, nullptr, result);
    }
    HRESULT STDMETHODCALLTYPE Navigate(TypeName type, IInspectable* parameter, boolean* result) override {
        if (!result) return E_POINTER;
        *result = false;
        Log("TRACE", "Frame::Navigate(%ls)", WindowsGetStringRawBuffer(type.Name, nullptr));
        ComPtr<IApplication> app;
        auto hr = CurrentApplication(&app);
        if (FAILED(hr) || !app) return E_UNEXPECTED;
        ComPtr<ABI::Windows::UI::Xaml::Markup::IXamlMetadataProvider> provider;
        hr = app.As(&provider);
        if (FAILED(hr)) return hr;
        ComPtr<ABI::Windows::UI::Xaml::Markup::IXamlType> xamlType;
        hr = provider->GetXamlType(type, &xamlType);
        if (FAILED(hr)) return hr;
        if (!xamlType) return TYPE_E_ELEMENTNOTFOUND;
        Log("TRACE", "Frame activating page");
        ComPtr<IInspectable> page;
        hr = xamlType->ActivateInstance(&page);
        Log("TRACE", "Frame page activation returned %08lx", hr);
        if (FAILED(hr)) return hr;
        if (!page) return E_UNEXPECTED;
        Entry next;
        next.page = std::move(page);
        next.parameter = parameter;
        next.typeName.Set(type.Name);
        next.typeKind = type.Kind;
        hr = Show(next, ABI::Windows::UI::Xaml::Navigation::NavigationMode_New);
        if (FAILED(hr)) return hr;
        *result = true;
        return S_OK;
    }
};
HRESULT DeliverElementLoaded(IInspectable* object) {
    ComPtr<IFrameworkElement> element;
    if (object && SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&element)))) {
        auto found = LoadedCallbacks().find(element.Get());
        if (found != LoadedCallbacks().end()) {
            auto hr = found->second();
            if (FAILED(hr)) return hr;
        }
    }
    ComPtr<IContentControl> content;
    if (object && SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&content)))) {
        ComPtr<IInspectable> child;
        auto hr = content->get_Content(&child);
        if (FAILED(hr)) return hr;
        return child ? DeliverElementLoaded(child.Get()) : S_OK;
    }
    ComPtr<IUserControl> user;
    if (object && SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&user)))) {
        ComPtr<IUIElement> child;
        auto hr = user->get_Content(&child);
        if (FAILED(hr)) return hr;
        if (child) return DeliverElementLoaded(child.Get());
    }
    return S_OK;
}
HRESULT RaiseElementSizeChanged() {
    // Handlers may create or destroy elements; re-find each key before calling.
    std::vector<IFrameworkElement*> keys;
    for (auto& item : SizeCallbacks()) keys.push_back(item.first);
    for (auto* key : keys) {
        auto found = SizeCallbacks().find(key);
        if (found == SizeCallbacks().end()) continue;
        auto callback = found->second;
        auto hr = callback();
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}
HRESULT QueueElementLoaded(IInspectable* value) {
    if (!value) return S_OK;
    ComPtr<IInspectable> retained(value);
    return PostHostWork(0, [retained] {
        auto hr = DeliverElementLoaded(retained.Get());
        if (FAILED(hr)) Log("FAIL", "Loaded handler hr=%08lx", hr);
    });
}
class FrameFactory final : public Factory<DefaultIFrameFactory> {
public:
    FrameFactory() : Factory(L"Windows.UI.Xaml.Controls.Frame") {}
    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* outer, IInspectable** inner, IFrame** value) override {
        return Compose<Frame>(outer, inner, value);
    }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        ComPtr<IFrame> frame;
        auto hr = Compose<Frame>(nullptr, nullptr, frame.GetAddressOf());
        return FAILED(hr) ? hr : frame.CopyTo(value);
    }
};
class PageFactory final : public Factory<DefaultIPageFactory> {
public:
    PageFactory() : Factory(L"Windows.UI.Xaml.Controls.Page") {}
    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* outer, IInspectable** inner, IPage** value) override {
        return Compose<Page>(outer, inner, value);
    }
};
class UserControlFactory final : public Factory<DefaultIUserControlFactory> {
public:
    UserControlFactory() : Factory(L"Windows.UI.Xaml.Controls.UserControl") {}
    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* outer, IInspectable** inner, IUserControl** value) override {
        return Compose<UserControl>(outer, inner, value);
    }
};
HRESULT MakeControlFactory(const wchar_t* name, IActivationFactory** value) {
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.UserControl") == 0) {
        auto factory = Make<UserControlFactory>();
        return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
    }
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Frame") == 0) {
        auto factory = Make<FrameFactory>();
        return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
    }
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Page") == 0) {
        auto factory = Make<PageFactory>();
        return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}
const ShimClass rectangleGeometryClass{L"Windows.UI.Xaml.Media.RectangleGeometry", MakeRectangleGeometryFactory};
const ShimClass frameClass{L"Windows.UI.Xaml.Controls.Frame", MakeControlFactory};
const ShimClass pageClass{L"Windows.UI.Xaml.Controls.Page", MakeControlFactory};
const ShimClass userControlClass{L"Windows.UI.Xaml.Controls.UserControl", MakeControlFactory};
}
