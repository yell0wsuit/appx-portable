#include <windows.h>
#undef GetCurrentTime
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.controls.h>
#include <wrl.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include "log.h"
#include "com/factory.h"
#include "registry.h"
namespace appx {
namespace Xaml = winrt::Windows::UI::Xaml;
namespace Controls = Xaml::Controls;
static bool HorizontalPanel(Xaml::DependencyObject const& panel) {
    if (auto stack = panel.try_as<Controls::StackPanel>())
        return stack.Orientation() == Controls::Orientation::Horizontal;
    if (auto wrap = panel.try_as<Controls::VariableSizedWrapGrid>())
        return wrap.Orientation() == Controls::Orientation::Vertical;
    if (auto wrap = panel.try_as<Controls::WrapGrid>()) return wrap.Orientation() == Controls::Orientation::Vertical;
    if (auto wrap = panel.try_as<Controls::ItemsWrapGrid>())
        return wrap.Orientation() == Controls::Orientation::Vertical;
    return false;
}
static Controls::ScrollViewer GridScroller(Xaml::DependencyObject const& element) {
    if (auto scroll = element.try_as<Controls::ScrollViewer>()) return scroll;
    int count = Xaml::Media::VisualTreeHelper::GetChildrenCount(element);
    for (int i = 0; i < count; ++i) {
        if (auto found = GridScroller(Xaml::Media::VisualTreeHelper::GetChild(element, i))) return found;
    }
    return nullptr;
}
static void RestoreGridScrolling(Controls::GridView const& grid) {
    // Win8 horizontal group panels no longer select a horizontal ScrollViewer
    // in the modern GridView template. Detect the authored panel direction.
    auto panel = grid.ItemsPanelRoot();
    if (!panel || !HorizontalPanel(panel)) return;
    auto scroll = GridScroller(grid);
    if (!scroll) return;
    scroll.HorizontalScrollMode(Controls::ScrollMode::Enabled);
    scroll.HorizontalScrollBarVisibility(Controls::ScrollBarVisibility::Auto);
    scroll.VerticalScrollMode(Controls::ScrollMode::Disabled);
    scroll.VerticalScrollBarVisibility(Controls::ScrollBarVisibility::Disabled);
    appx::Log("TRACE", "restored horizontal GridView scrolling");
}
static HRESULT ConnectGridScrolling(IInspectable* instance) {
    // Query the non-delegating inner during composition. The managed outer's
    // RCW is not ready until its base constructor returns.
    Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::IFrameworkElement> element;
    auto hr = instance->QueryInterface(IID_PPV_ARGS(&element));
    if (FAILED(hr)) return hr;
    auto loaded = Microsoft::WRL::Callback<ABI::Windows::UI::Xaml::IRoutedEventHandler>(
        [](IInspectable* sender, ABI::Windows::UI::Xaml::IRoutedEventArgs*) -> HRESULT {
            try {
                winrt::Windows::Foundation::IInspectable object{nullptr};
                winrt::copy_from_abi(object, sender);
                RestoreGridScrolling(object.as<Controls::GridView>());
                return S_OK;
            } catch (...) {
                return winrt::to_hresult();
            }
        });
    EventRegistrationToken token{};
    return element->add_Loaded(loaded.Get(), &token);
}
class GridFactory final : public Factory<ABI::Windows::UI::Xaml::Controls::IGridViewFactory> {
    Microsoft::WRL::ComPtr<IActivationFactory> real_;

public:
    explicit GridFactory(IActivationFactory* real) : Factory(L"Windows.UI.Xaml.Controls.GridView"), real_(real) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        auto hr = real_->ActivateInstance(value);
        if (FAILED(hr)) return hr;
        Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::Controls::IGridView> grid;
        hr = (*value)->QueryInterface(IID_PPV_ARGS(&grid));
        return SUCCEEDED(hr) ? ConnectGridScrolling(grid.Get()) : hr;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* outer, IInspectable** inner,
                                             ABI::Windows::UI::Xaml::Controls::IGridView** value) override {
        Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::Controls::IGridViewFactory> factory;
        auto hr = real_.As(&factory);
        if (SUCCEEDED(hr)) hr = factory->CreateInstance(outer, inner, value);
        return SUCCEEDED(hr) ? ConnectGridScrolling(inner && *inner ? *inner : *value) : hr;
    }
};
HRESULT MakeIslandGridFactory(IActivationFactory** value) {
    Microsoft::WRL::ComPtr<IActivationFactory> real;
    auto hr = OsGetActivationFactory(
        Microsoft::WRL::Wrappers::HStringReference(L"Windows.UI.Xaml.Controls.GridView").Get(), IID_PPV_ARGS(&real));
    return SUCCEEDED(hr) ? Microsoft::WRL::Make<GridFactory>(real.Get()).CopyTo(value) : hr;
}
static Xaml::UIElement wheelRoot{nullptr};
static winrt::event_token wheelToken;
HRESULT ConnectIslandScrolling(IInspectable* root) {
    try {
        if (winrt::get_abi(wheelRoot) == root) return S_OK;
        if (wheelRoot) wheelRoot.PointerWheelChanged(wheelToken);
        wheelRoot = nullptr;
        if (!root) return S_OK;
        winrt::copy_from_abi(wheelRoot, root);
        wheelToken =
            wheelRoot.PointerWheelChanged([](auto const& sender, Xaml::Input::PointerRoutedEventArgs const& args) {
                if (args.Handled() || (args.KeyModifiers() & winrt::Windows::System::VirtualKeyModifiers::Control) !=
                                          winrt::Windows::System::VirtualKeyModifiers::None)
                    return;
                auto point = args.GetCurrentPoint(sender.template as<Xaml::UIElement>());
                auto properties = point.Properties();
                double delta = properties.MouseWheelDelta();
                if (!properties.IsHorizontalMouseWheel()) delta = -delta;
                auto element = args.OriginalSource().try_as<Xaml::DependencyObject>();
                while (element) {
                    auto scroll = element.try_as<Controls::ScrollViewer>();
                    if (scroll) {
                        // Let native vertical scrollers and application handlers
                        // keep their input. Legacy horizontal pages need ordinary
                        // mouse wheels to pan when there is no vertical overflow.
                        if (scroll.ScrollableHeight() > 0) return;
                        if (scroll.ScrollableWidth() > 0 &&
                            scroll.HorizontalScrollMode() != Controls::ScrollMode::Disabled) {
                            UINT units = 3;
                            SystemParametersInfoW(properties.IsHorizontalMouseWheel() ? SPI_GETWHEELSCROLLCHARS
                                                                                      : SPI_GETWHEELSCROLLLINES,
                                                  0, &units, 0);
                            double distance = units == WHEEL_PAGESCROLL ? scroll.ViewportWidth() : units * 16.0;
                            double next = (std::clamp)(scroll.HorizontalOffset() + delta * distance / WHEEL_DELTA, 0.0,
                                                       scroll.ScrollableWidth());
                            if (next != scroll.HorizontalOffset()) {
                                auto offset =
                                    winrt::box_value(next).as<winrt::Windows::Foundation::IReference<double>>();
                                if (scroll.ChangeView(offset, nullptr, nullptr, true)) {
                                    scroll.HorizontalScrollBarVisibility(Controls::ScrollBarVisibility::Auto);
                                    args.Handled(true);
                                    return;
                                }
                            }
                        }
                    }
                    element = Xaml::Media::VisualTreeHelper::GetParent(element);
                }
            });
        return S_OK;
    } catch (...) {
        return winrt::to_hresult();
    }
}
}
