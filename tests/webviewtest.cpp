#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#undef GetCurrentTime
#include <roapi.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.hosting.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Xaml;

static int Run() {
    if (FAILED(RoInitialize(RO_INIT_SINGLETHREADED))) return 1;
    auto dll = LoadLibraryW(L"appxportable.dll");
    if (!dll) return 2;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    if (!get) return 3;
    ComPtr<Hosting::IWindowsXamlManagerStatics> statics;
    ComPtr<Hosting::IWindowsXamlManager> manager;
    if (FAILED(RoGetActivationFactory(HStringReference(L"Windows.UI.Xaml.Hosting.WindowsXamlManager").Get(),
                                      IID_PPV_ARGS(&statics))) ||
        FAILED(statics->InitializeForCurrentThread(&manager)))
        return 4;
    ComPtr<IActivationFactory> factory;
    if (FAILED(get(HStringReference(L"Windows.UI.Xaml.Controls.WebView").Get(), &factory))) return 5;
    // Static dependency properties must still come from system XAML.
    ComPtr<Controls::IWebViewStatics> properties;
    ComPtr<IDependencyProperty> source;
    if (FAILED(factory.As(&properties)) || FAILED(properties->get_SourceProperty(&source)) || !source) return 6;
    for (int i = 0; i < 20; ++i) {
        ComPtr<IInspectable> object;
        ComPtr<Controls::IWebView> web;
        ComPtr<IUIElement> ui;
        ComPtr<IFrameworkElement> element;
        if (FAILED(factory->ActivateInstance(&object)) || FAILED(object.As(&web)) || FAILED(object.As(&ui)) ||
            FAILED(object.As(&element)))
            return 7;
        if (FAILED(ui->put_IsRightTapEnabled(false)) ||
            FAILED(element->put_Name(HStringReference(L"articleView").Get())) || FAILED(element->put_Width(500)) ||
            FAILED(element->put_Margin({10, 20, 30, 40})))
            return 8;
        ComPtr<IUnknown> outerIdentity, elementIdentity;
        if (FAILED(web.As(&outerIdentity)) || FAILED(element.As(&elementIdentity)) || outerIdentity != elementIdentity)
            return 9;
        HString name;
        if (FAILED(object->GetRuntimeClassName(name.GetAddressOf())) ||
            wcscmp(name.GetRawBuffer(nullptr), L"Windows.UI.Xaml.Controls.WebView"))
            return 10;
        if (FAILED(web->NavigateToString(HStringReference(L"<p>queued before Loaded</p>").Get()))) return 11;
        // Exercise event removal and composed inner destruction. Neither the
        // Grid nor our callbacks may retain the outer after the caller leaves.
        outerIdentity.Reset();
        elementIdentity.Reset();
        element.Reset();
        ui.Reset();
        web.Reset();
        if (object.Detach()->Release() != 0) return 12;
    }
    puts("webviewtest: composition, identity, properties, statics and lifetime passed");
    return 0;
}
int main() {
    auto result = Run();
    if (result) printf("webviewtest: failed check %d\n", result);
    return result;
}
