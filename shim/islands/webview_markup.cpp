// XBF v1's runtime adapter names a custom type instead of built-in WebView. Grid
// metadata supplies inherited XAML properties; activation supplies our outer.
#include "webview.h"
#include "xbf_loader.h"
#include "log.h"
#include <windows.ui.xaml.markup.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
namespace Markup = ABI::Windows::UI::Xaml::Markup;
namespace Interop = ABI::Windows::UI::Xaml::Interop;
namespace appx {
namespace {
constexpr wchar_t PortableWebView[] = L"Windows.UI.Xaml.Controls.PortableWebView";
constexpr wchar_t RuntimeWebView[] = L"Windows.UI.Xaml.Controls.AppxWeb";
HRESULT BackgroundMember(Markup::IXamlMember** value, const wchar_t* type);
class WebViewType final : public RuntimeClass<RuntimeClassFlags<WinRt>, Markup::IXamlType> {
    InspectableClass(L"AppxPortable.WebViewType", BaseTrust);
    bool base_;
    const wchar_t* systemName_;
    const wchar_t* customName_;
    static HRESULT Boolean(boolean* value, bool state = false) {
        if (!value) return E_POINTER;
        *value = state;
        return S_OK;
    }
    template <class T> static HRESULT Empty(T** value) {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }

public:
    explicit WebViewType(bool base = false, const wchar_t* name = L"Windows.UI.Xaml.Controls.Grid",
                         const wchar_t* custom = PortableWebView)
        : base_(base), systemName_(name), customName_(custom) {}
    HRESULT STDMETHODCALLTYPE get_BaseType(Markup::IXamlType** value) override {
        if (base_) return Empty(value);
        return Make<WebViewType>(true).CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_ContentProperty(Markup::IXamlMember** value) override { return Empty(value); }
    HRESULT STDMETHODCALLTYPE get_FullName(HSTRING* value) override {
        return HStringReference(base_ ? systemName_ : customName_).CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_IsArray(boolean* value) override { return Boolean(value); }
    HRESULT STDMETHODCALLTYPE get_IsCollection(boolean* value) override { return Boolean(value); }
    HRESULT STDMETHODCALLTYPE get_IsConstructible(boolean* value) override { return Boolean(value, !base_); }
    HRESULT STDMETHODCALLTYPE get_IsDictionary(boolean* value) override { return Boolean(value); }
    HRESULT STDMETHODCALLTYPE get_IsMarkupExtension(boolean* value) override { return Boolean(value); }
    HRESULT STDMETHODCALLTYPE get_IsBindable(boolean* value) override { return Boolean(value); }
    HRESULT STDMETHODCALLTYPE get_ItemType(Markup::IXamlType** value) override { return Empty(value); }
    HRESULT STDMETHODCALLTYPE get_KeyType(Markup::IXamlType** value) override { return Empty(value); }
    HRESULT STDMETHODCALLTYPE get_UnderlyingType(Interop::TypeName* value) override {
        if (!value) return E_POINTER;
        *value = {};
        value->Kind = base_ ? Interop::TypeKind_Metadata : Interop::TypeKind_Custom;
        return get_FullName(&value->Name);
    }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return base_ ? E_NOTIMPL : CreateIslandWebView(value);
    }
    HRESULT STDMETHODCALLTYPE CreateFromString(HSTRING, IInspectable** value) override {
        Empty(value);
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetMember(HSTRING name, Markup::IXamlMember** value) override {
        if (!base_ && wcscmp(WindowsGetStringRawBuffer(name, nullptr), L"DefaultBackgroundColor") == 0)
            return BackgroundMember(value, customName_);
        return Empty(value);
    }
    HRESULT STDMETHODCALLTYPE AddToVector(IInspectable*, IInspectable*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE AddToMap(IInspectable*, IInspectable*, IInspectable*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RunInitializer() override { return S_OK; }
};
class ColorValue final : public RuntimeClass<RuntimeClassFlags<WinRt>, __FIReference_1_Windows__CUI__CColor> {
    InspectableClass(L"Windows.Foundation.IReference`1<Windows.UI.Color>", BaseTrust);
    ABI::Windows::UI::Color value_;

public:
    explicit ColorValue(ABI::Windows::UI::Color value) : value_(value) {}
    HRESULT STDMETHODCALLTYPE get_Value(ABI::Windows::UI::Color* value) override {
        if (!value) return E_POINTER;
        *value = value_;
        return S_OK;
    }
};
class BackgroundProperty final : public RuntimeClass<RuntimeClassFlags<WinRt>, Markup::IXamlMember> {
    InspectableClass(L"AppxPortable.WebViewBackground", BaseTrust);
    const wchar_t* type_;

public:
    explicit BackgroundProperty(const wchar_t* type) : type_(type) {}
    HRESULT STDMETHODCALLTYPE get_IsAttachable(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsDependencyProperty(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* value) override {
        return HStringReference(L"DefaultBackgroundColor").CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_TargetType(Markup::IXamlType** value) override {
        return Make<WebViewType>(false, L"Windows.UI.Xaml.Controls.Grid", type_).CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_Type(Markup::IXamlType** value) override {
        return Make<WebViewType>(true, L"Windows.UI.Color").CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE GetValue(IInspectable* instance, IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!instance) return E_POINTER;
        ComPtr<IIslandWebView> web;
        ABI::Windows::UI::Color color{};
        auto hr = instance->QueryInterface(IID_PPV_ARGS(&web));
        if (SUCCEEDED(hr)) hr = web->GetBackgroundColor(&color);
        return SUCCEEDED(hr) ? Make<ColorValue>(color).CopyTo(value) : hr;
    }
    HRESULT STDMETHODCALLTYPE SetValue(IInspectable* instance, IInspectable* value) override {
        if (!instance || !value) return E_POINTER;
        ComPtr<IIslandWebView> web;
        ComPtr<__FIReference_1_Windows__CUI__CColor> color;
        auto hr = instance->QueryInterface(IID_PPV_ARGS(&web));
        if (SUCCEEDED(hr)) hr = value->QueryInterface(IID_PPV_ARGS(&color));
        ABI::Windows::UI::Color arg{};
        if (SUCCEEDED(hr)) hr = color->get_Value(&arg);
        if (SUCCEEDED(hr)) hr = web->SetBackgroundColor(arg);
        if (FAILED(hr)) Log("FAIL", "WebView markup background failed hr=%08lx", hr);
        return hr;
    }
};
HRESULT BackgroundMember(Markup::IXamlMember** value, const wchar_t* type) {
    return Make<BackgroundProperty>(type).CopyTo(value);
}
const wchar_t* WebViewName(HSTRING name) {
    const auto* text = WindowsGetStringRawBuffer(name, nullptr);
    if (wcscmp(text, PortableWebView) == 0) return PortableWebView;
    if (wcscmp(text, RuntimeWebView) == 0) return RuntimeWebView;
    return nullptr;
}
using TypeCall = HRESULT(STDMETHODCALLTYPE*)(Markup::IXamlMetadataProvider*, Interop::TypeName, Markup::IXamlType**);
using NameCall = HRESULT(STDMETHODCALLTYPE*)(Markup::IXamlMetadataProvider*, HSTRING, Markup::IXamlType**);
TypeCall realType;
NameCall realName;
// The host owns one Application for the process lifetime. The CLR can share
// interface vtables with other metadata providers; those calls pass through.
Markup::IXamlMetadataProvider* applicationProvider;
HRESULT STDMETHODCALLTYPE Type(Markup::IXamlMetadataProvider* self, Interop::TypeName name, Markup::IXamlType** value) {
    if (self == applicationProvider)
        if (auto custom = WebViewName(name.Name))
            return Make<WebViewType>(false, L"Windows.UI.Xaml.Controls.Grid", custom).CopyTo(value);
    return realType(self, name, value);
}
HRESULT STDMETHODCALLTYPE Name(Markup::IXamlMetadataProvider* self, HSTRING name, Markup::IXamlType** value) {
    if (self == applicationProvider)
        if (auto custom = WebViewName(name))
            return Make<WebViewType>(false, L"Windows.UI.Xaml.Controls.Grid", custom).CopyTo(value);
    return realName(self, name, value);
}
}
HRESULT HookIslandWebViewMetadata(IInspectable* application) {
    // Querying an unsupported interface during Application's base constructor
    // makes the CLR probe an incomplete composable RCW. Call only after the
    // base constructor returns (LoadComponent or the deferred OnLaunched).
    static bool checked = false;
    if (checked || !application) return S_OK;
    ComPtr<Markup::IXamlMetadataProvider> provider;
    auto hr = application->QueryInterface(IID_PPV_ARGS(&provider));
    if (hr == E_NOINTERFACE) {
        checked = true;
        return S_OK;
    }
    if (FAILED(hr)) return hr;
    applicationProvider = provider.Get();
    auto** table = *reinterpret_cast<void***>(provider.Get());
    if (table[6] == reinterpret_cast<void*>(&Type)) return InstallIslandXbfLoader();
    DWORD old;
    if (!VirtualProtect(table + 6, sizeof(void*) * 2, PAGE_READWRITE, &old)) return HRESULT_FROM_WIN32(GetLastError());
    realType = reinterpret_cast<TypeCall>(table[6]);
    realName = reinterpret_cast<NameCall>(table[7]);
    InterlockedExchangePointer(table + 6, reinterpret_cast<void*>(&Type));
    InterlockedExchangePointer(table + 7, reinterpret_cast<void*>(&Name));
    VirtualProtect(table + 6, sizeof(void*) * 2, old, &old);
    hr = InstallIslandXbfLoader();
    if (SUCCEEDED(hr)) checked = true;
    return hr;
}
}
