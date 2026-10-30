// Test-only SDK ABI navigation caller for a managed Page's controlling outer.
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.navigation.h>
#include <windows.ui.xaml.markup.h>
#include <wrl.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml::Navigation;
class TestNavigationArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>, INavigationEventArgs> {
    InspectableClass(L"Windows.UI.Xaml.Navigation.NavigationEventArgs", BaseTrust);
    ComPtr<ABI::Windows::Foundation::IUriRuntimeClass> uri_;
public:
    HRESULT STDMETHODCALLTYPE get_Content(IInspectable** value) override { if (!value) return E_POINTER; *value = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Parameter(IInspectable** value) override { if (!value) return E_POINTER; *value = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_SourcePageType(ABI::Windows::UI::Xaml::Interop::TypeName* value) override {
        if (!value) return E_POINTER;
        value->Name = nullptr; value->Kind = ABI::Windows::UI::Xaml::Interop::TypeKind_Custom; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_NavigationMode(NavigationMode* value) override { if (!value) return E_POINTER; *value = NavigationMode_New; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Uri(ABI::Windows::Foundation::IUriRuntimeClass** value) override { if (!value) return E_POINTER; return uri_.CopyTo(value); }
    HRESULT STDMETHODCALLTYPE put_Uri(ABI::Windows::Foundation::IUriRuntimeClass* value) override { uri_ = value; return S_OK; }
};
extern "C" HRESULT __cdecl InvokePageOverrides(IUnknown* page) {
    if (!page) return E_POINTER;
    ComPtr<ABI::Windows::UI::Xaml::Controls::IPageOverrides> callbacks;
    auto hr = page->QueryInterface(IID_PPV_ARGS(&callbacks));
    if (FAILED(hr)) return hr;
    auto args = Make<TestNavigationArgs>();
    if (!args) return E_OUTOFMEMORY;
    hr = callbacks->OnNavigatedTo(args.Get());
    if (SUCCEEDED(hr)) hr = callbacks->OnNavigatedFrom(args.Get());
    if (SUCCEEDED(hr)) hr = callbacks->OnNavigatingFrom(nullptr);
    return hr;
}

extern "C" HRESULT __cdecl InvokeComponentConnector(IUnknown* page, INT32 id, IInspectable* target) {
    if (!page) return E_POINTER;
    ComPtr<ABI::Windows::UI::Xaml::Markup::IComponentConnector> connector;
    auto hr = page->QueryInterface(IID_PPV_ARGS(&connector));
    return FAILED(hr) ? hr : connector->Connect(id, target);
}

// Resolve the same Window the external, marked consumer populated. This tests
// callbacks across Windows.dll's assembly boundary without exposing its internals.
extern "C" HRESULT __cdecl ConnectWindowContent(INT32 id) {
    using namespace ABI::Windows::UI::Xaml;
    using GetFactory = HRESULT (WINAPI*)(HSTRING, IActivationFactory**);
    auto getFactory = reinterpret_cast<GetFactory>(GetProcAddress(GetModuleHandleW(L"appxportable.dll"), "ShimGetFactory"));
    if (!getFactory) return E_UNEXPECTED;
    ComPtr<IActivationFactory> factory;
    auto hr = getFactory(Wrappers::HStringReference(L"Windows.UI.Xaml.Window").Get(), &factory);
    ComPtr<IWindowStatics> statics;
    if (SUCCEEDED(hr)) hr = factory.As(&statics);
    ComPtr<IWindow> window;
    if (SUCCEEDED(hr)) hr = statics->get_Current(&window);
    ComPtr<IUIElement> content;
    if (SUCCEEDED(hr)) hr = window->get_Content(&content);
    ComPtr<IInspectable> target;
    if (SUCCEEDED(hr) && content) hr = content.As(&target);
    return FAILED(hr) ? hr : InvokeComponentConnector(content.Get(), id, target.Get());
}

extern "C" HRESULT __cdecl ConnectNavigationArgs(IUnknown* page, INT32 id) {
    auto args = Make<TestNavigationArgs>();
    return args ? InvokeComponentConnector(page, id, args.Get()) : E_OUTOFMEMORY;
}

class NamelessTarget final : public RuntimeClass<RuntimeClassFlags<WinRt>, ABI::Windows::Foundation::IStringable> {
    HRESULT nameResult_;
public:
    explicit NamelessTarget(HRESULT result) : nameResult_(result) {}
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* value) noexcept override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return nameResult_;
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* value) noexcept override {
        if (!value) return E_POINTER;
        *value = BaseTrust;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ToString(HSTRING* value) override { return WindowsCreateString(L"nameless", 8, value); }
};
extern "C" HRESULT __cdecl ConnectNamelessTarget(IUnknown* page, INT32 id, HRESULT nameResult) {
    auto target = Make<NamelessTarget>(nameResult);
    return target ? InvokeComponentConnector(page, id, target.Get()) : E_OUTOFMEMORY;
}
extern "C" HRESULT __cdecl CheckNamelessTarget(IInspectable* target) {
    if (!target) return E_POINTER;
    ComPtr<ABI::Windows::Foundation::IStringable> stringable;
    auto hr = target->QueryInterface(IID_PPV_ARGS(&stringable));
    Wrappers::HString text;
    if (SUCCEEDED(hr)) hr = stringable->ToString(text.GetAddressOf());
    return FAILED(hr) ? hr : wcscmp(text.GetRawBuffer(nullptr), L"nameless") == 0 ? S_OK : E_FAIL;
}
