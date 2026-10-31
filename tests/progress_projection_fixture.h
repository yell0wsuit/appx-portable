#include <windows.ui.xaml.controls.primitives.h>
#include <windows.ui.xaml.media.h>
#include <thread>
namespace progress_fixture {
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
using namespace ABI::Windows::UI::Xaml::Media;
using namespace ABI::Windows::Foundation::Collections;
ComPtr<IBrush> retained;
bool Color(IBrush* brush, BYTE a, BYTE r, BYTE g, BYTE b) {
    ComPtr<ISolidColorBrush> solid;
    ABI::Windows::UI::Color value{};
    return brush && SUCCEEDED(brush->QueryInterface(IID_PPV_ARGS(&solid))) && SUCCEEDED(solid->get_Color(&value)) &&
           value.A == a && value.R == r && value.G == g && value.B == b;
}
HRESULT Check(int stage) {
    if (stage) {
        auto match = Color(retained.Get(), 255, 255, 255, 255);
        retained.Reset();
        return match ? S_OK : E_FAIL;
    }
    ComPtr<IUIElement> root, content, first, second, third;
    ComPtr<IUserControl> page;
    ComPtr<IPanel> panel;
    ComPtr<IVector<ABI::Windows::UI::Xaml::UIElement*>> children;
    auto hr = WindowContent(&root);
    if (FAILED(hr) || FAILED(root.As(&page)) || FAILED(page->get_Content(&content)) || FAILED(content.As(&panel)) ||
        FAILED(panel->get_Children(&children)) || FAILED(children->GetAt(0, &first)) ||
        FAILED(children->GetAt(1, &second)) || FAILED(children->GetAt(2, &third)))
        return E_FAIL;
    ComPtr<IProgressRing> active, inactive;
    ComPtr<IControl> control, other, theme;
    if (FAILED(first.As(&active)) || FAILED(second.As(&inactive)) || FAILED(first.As(&control)) ||
        FAILED(second.As(&other)) || FAILED(third.As(&theme)))
        return E_FAIL;
    ComPtr<IProgressRing> defaultRing;
    if (FAILED(third.As(&defaultRing))) return E_FAIL;
    boolean defaultActive = true;
    if (FAILED(defaultRing->get_IsActive(&defaultActive)) || defaultActive) return E_FAIL;
    boolean value = false;
    if (FAILED(active->get_IsActive(&value)) || !value || FAILED(inactive->get_IsActive(&value)) || value)
        return E_FAIL;
    ComPtr<IBrush> foreground, paint, unset;
    if (FAILED(control->get_Foreground(&foreground)) || !Color(foreground.Get(), 255, 255, 255, 255) ||
        FAILED(other->get_Foreground(&paint)) || !Color(paint.Get(), 0x80, 0x12, 0x34, 0x56) ||
        theme->get_Foreground(&unset) != E_NOTIMPL)
        return E_FAIL;
    ComPtr<IFrameworkElement> names;
    ComPtr<IInspectable> namedPaint;
    ComPtr<IBrush> actualPaint;
    if (FAILED(root.As(&names)) || FAILED(names->FindName(Wrappers::HStringReference(L"paint").Get(), &namedPaint)) ||
        !namedPaint || FAILED(namedPaint.As(&actualPaint)) || actualPaint.Get() != paint.Get())
        return E_FAIL;
    retained = foreground;
    double opacity = 0;
    if (FAILED(paint->get_Opacity(&opacity)) || opacity != .5) return E_FAIL;
    ComPtr<IFrameworkElement> element;
    Thickness margin{};
    double width = 0, height = 0;
    if (FAILED(first.As(&element)) || FAILED(element->get_Margin(&margin)) || margin.Top != 420 ||
        FAILED(element->get_Width(&width)) || width != 100 || FAILED(element->get_Height(&height)) || height != 100)
        return E_FAIL;
    ComPtr<Primitives::IProgressRingTemplateSettings> settings;
    if (active->get_TemplateSettings(&settings) != E_NOTIMPL || settings) return E_FAIL;
    if (FAILED(other->put_Foreground(foreground.Get())) || FAILED(other->get_Foreground(&paint)) ||
        paint.Get() != foreground.Get() || FAILED(other->put_Foreground(nullptr)) ||
        FAILED(other->get_Foreground(&paint)) || paint)
        return E_FAIL;
    if (FAILED(control->put_Foreground(nullptr))) return E_FAIL;
    HRESULT wrong = S_OK;
    std::thread worker([&] {
        ComPtr<IBrush> brush;
        wrong = control->get_Foreground(&brush);
    });
    worker.join();
    if (wrong != RPC_E_WRONG_THREAD) return E_FAIL;
    ComPtr<IFrameworkElement> rootElement;
    ComPtr<IInspectable> named;
    ComPtr<IBrush> transparent;
    if (FAILED(root.As(&rootElement)) ||
        FAILED(rootElement->FindName(Wrappers::HStringReference(L"transparentBrush").Get(), &named)) || !named ||
        FAILED(named.As(&transparent)) || !Color(transparent.Get(), 0, 255, 255, 255))
        return E_FAIL;
    for (const auto& entry : {std::make_pair(L"defaultSolid", ABI::Windows::UI::Color{0, 255, 255, 255}),
                              std::make_pair(L"slate", ABI::Windows::UI::Color{255, 0x2F, 0x4F, 0x4F})}) {
        named.Reset();
        ComPtr<IBrush> brush;
        if (FAILED(rootElement->FindName(Wrappers::HStringReference(entry.first).Get(), &named)) || !named ||
            FAILED(named.As(&brush)) ||
            !Color(brush.Get(), entry.second.A, entry.second.R, entry.second.G, entry.second.B))
            return E_FAIL;
    }
    ComPtr<IInspectable> created;
    ComPtr<IActivationFactory> factory;
    if (FAILED(animation_fixture::Activate(L"Windows.UI.Xaml.Media.SolidColorBrush", &created, &factory)))
        return E_FAIL;
    ComPtr<IBrush> brush;
    ComPtr<ISolidColorBrushFactory> colorFactory;
    ComPtr<ISolidColorBrush> solid;
    if (FAILED(created.As(&brush)) || !Color(brush.Get(), 0, 255, 255, 255) || FAILED(brush->get_Opacity(&opacity)) ||
        opacity != 1 || FAILED(factory.As(&colorFactory)) ||
        FAILED(colorFactory->CreateInstanceWithColor({64, 17, 34, 51}, &solid)) || FAILED(solid.As(&brush)) ||
        !Color(brush.Get(), 64, 17, 34, 51))
        return E_FAIL;
    if (FAILED(solid->put_Color({255, 3, 2, 1})) || !Color(brush.Get(), 255, 3, 2, 1)) return E_FAIL;
    return S_OK;
}
}
extern "C" HRESULT __cdecl ValidateProgressData(INT32 stage) { return progress_fixture::Check(stage); }
