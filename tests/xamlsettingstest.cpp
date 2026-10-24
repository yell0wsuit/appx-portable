#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#undef GetCurrentTime
#include <roapi.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.controls.primitives.h>
#include <windows.ui.xaml.hosting.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <cstring>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Xaml;

static HRESULT Run(bool shim) {
    auto hr = RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(hr)) return hr;
    if (shim && !LoadLibraryW(L"appxportable.dll")) return HRESULT_FROM_WIN32(GetLastError());
    ComPtr<Hosting::IWindowsXamlManagerStatics> statics;
    ComPtr<Hosting::IWindowsXamlManager> manager;
    hr = RoGetActivationFactory(HStringReference(L"Windows.UI.Xaml.Hosting.WindowsXamlManager").Get(),
                                IID_PPV_ARGS(&statics));
    if (SUCCEEDED(hr)) hr = statics->InitializeForCurrentThread(&manager);
    if (FAILED(hr)) return hr;
    ComPtr<IInspectable> object;
    ComPtr<Controls::IToggleSwitch> toggle;
    hr = RoActivateInstance(HStringReference(L"Windows.UI.Xaml.Controls.ToggleSwitch").Get(), &object);
    if (SUCCEEDED(hr)) hr = object.As(&toggle);
    if (FAILED(hr)) return hr;
    int toggles = 0;
    auto toggled = Callback<IRoutedEventHandler>([&](IInspectable*, IRoutedEventArgs*) -> HRESULT {
        ++toggles;
        return S_OK;
    });
    EventRegistrationToken token{};
    hr = toggle->add_Toggled(toggled.Get(), &token);
    boolean value = true;
    if (SUCCEEDED(hr)) hr = toggle->get_IsOn(&value);
    if (FAILED(hr) || value) return E_UNEXPECTED;
    if (FAILED(toggle->put_IsOn(false)) || toggles != 0) return E_UNEXPECTED;
    if (FAILED(toggle->put_IsOn(true)) || toggles != 1) return E_UNEXPECTED;
    if (FAILED(toggle->put_IsOn(true)) || toggles != 1) return E_UNEXPECTED;
    if (FAILED(toggle->put_IsOn(false)) || toggles != 2) return E_UNEXPECTED;
    toggle->remove_Toggled(token);

    ComPtr<Controls::Primitives::IRangeBase> slider;
    object.Reset();
    hr = RoActivateInstance(HStringReference(L"Windows.UI.Xaml.Controls.Slider").Get(), &object);
    if (SUCCEEDED(hr)) hr = object.As(&slider);
    if (FAILED(hr)) return hr;
    int changes = 0;
    auto changed = Callback<Controls::Primitives::IRangeBaseValueChangedEventHandler>(
        [&](IInspectable*, Controls::Primitives::IRangeBaseValueChangedEventArgs*) -> HRESULT {
            ++changes;
            return S_OK;
        });
    hr = slider->add_ValueChanged(changed.Get(), &token);
    if (FAILED(hr)) return hr;
    if (FAILED(slider->put_Value(42)) || changes != 1) return E_UNEXPECTED;
    if (FAILED(slider->put_Value(42)) || changes != 1) return E_UNEXPECTED;
    slider->remove_ValueChanged(token);
    printf("%s: unchanged switch state raises no event; restored slider change raises one event\n",
           shim ? "islands" : "system XAML");
    return S_OK;
}
int main(int argc, char** argv) {
    auto hr = Run(argc > 1 && strcmp(argv[1], "--islands") == 0);
    if (FAILED(hr)) printf("XAML settings contract failed hr=%08lx\n", hr);
    return FAILED(hr) ? 1 : 0;
}
