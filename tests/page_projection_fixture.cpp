// Test-only SDK ABI navigation caller for a managed Page's controlling outer.
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.navigation.h>
#include <wrl.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml::Navigation;
class TestNavigationArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>, INavigationEventArgs> {
    InspectableClass(L"Windows.UI.Xaml.Navigation.NavigationEventArgs", BaseTrust);
public:
    HRESULT STDMETHODCALLTYPE get_Content(IInspectable** value) override { if (!value) return E_POINTER; *value = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Parameter(IInspectable** value) override { if (!value) return E_POINTER; *value = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_SourcePageType(ABI::Windows::UI::Xaml::Interop::TypeName* value) override {
        if (!value) return E_POINTER;
        value->Name = nullptr; value->Kind = ABI::Windows::UI::Xaml::Interop::TypeKind_Custom; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_NavigationMode(NavigationMode* value) override { if (!value) return E_POINTER; *value = NavigationMode_New; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Uri(ABI::Windows::Foundation::IUriRuntimeClass** value) override { if (!value) return E_POINTER; *value = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Uri(ABI::Windows::Foundation::IUriRuntimeClass*) override { return S_OK; }
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
