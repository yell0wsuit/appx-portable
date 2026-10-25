#include <windows.h>
#undef GetCurrentTime
#include <inspectable.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <cmath>
#include <memory>
#include <vector>
#include "log.h"
namespace appx {
namespace X = winrt::Windows::UI::Xaml;
namespace C = X::Controls;
namespace M = X::Media;
using Object = winrt::Windows::Foundation::IInspectable;
static bool Styled(X::Style style, X::DependencyProperty const& property) {
    while (style) {
        for (auto const& item : style.Setters()) {
            auto setter = item.try_as<X::Setter>();
            if (setter && setter.Property() == property) return true;
        }
        style = style.BasedOn();
    }
    return false;
}
static X::DependencyProperty BackgroundProperty(Object const& object) {
    if (object.try_as<C::Panel>()) return C::Panel::BackgroundProperty();
    if (object.try_as<C::Border>()) return C::Border::BackgroundProperty();
    if (object.try_as<C::Control>()) return C::Control::BackgroundProperty();
    return nullptr;
}
static double Linear(unsigned char value) {
    double channel = value / 255.0;
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}
// Compare the standard sRGB contrast ratios for light and dark text. Native
// XAML continues to supply the actual theme brushes, including high contrast.
// https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html
static X::ElementTheme ThemeFor(M::Brush const& background, X::ElementTheme inherited) {
    auto solid = background.try_as<M::SolidColorBrush>();
    if (!solid || solid.Opacity() != 1 || solid.Color().A != 255) return inherited;
    auto color = solid.Color();
    double luminance = 0.2126 * Linear(color.R) + 0.7152 * Linear(color.G) + 0.0722 * Linear(color.B);
    double darkText = (luminance + 0.05) / 0.05;
    double lightText = 1.05 / (luminance + 0.05);
    return darkText >= lightText ? X::ElementTheme::Light : X::ElementTheme::Dark;
}
struct Watch {
    X::DependencyObject object{nullptr};
    X::DependencyProperty property{nullptr};
    int64_t token = 0;
};
static void Clear(std::vector<Watch>& watches) noexcept {
    for (auto const& watch : watches) {
        try {
            watch.object.UnregisterPropertyChangedCallback(watch.property, watch.token);
        } catch (...) {}
    }
    watches.clear();
}
struct Appearance : std::enable_shared_from_this<Appearance> {
    C::SettingsFlyout target{nullptr};
    X::FrameworkElement page{nullptr};
    C::Primitives::Popup popup{nullptr};
    X::DependencyObject content{nullptr};
    M::Brush observedBrush{nullptr};
    std::vector<Watch> targetWatches, contentWatches, brushWatches;
    winrt::event_token pageToken{};
    bool watchingPage = false, updating = false, ownTheme = false, ownBackground = false;
    void WatchProperty(std::vector<Watch>& list, X::DependencyObject const& object,
                       X::DependencyProperty const& property, int action = 0) {
        if (!object || !property) return;
        std::weak_ptr<Appearance> weak = shared_from_this();
        auto token = object.RegisterPropertyChangedCallback(property, [weak, action](auto const&, auto const&) {
            if (auto self = weak.lock()) {
                if (self->updating) return;
                try {
                    if (action == 1) self->ownTheme = false;
                    if (action == 2) self->ownBackground = false;
                    if (action == 3) {
                        if (self->ownTheme) {
                            self->updating = true;
                            self->target.ClearValue(X::FrameworkElement::RequestedThemeProperty());
                            self->updating = false;
                        }
                        self->ownTheme = self->CanOwnTheme();
                        if (self->ownBackground) {
                            self->updating = true;
                            self->target.ClearValue(C::Control::BackgroundProperty());
                            self->updating = false;
                        }
                        self->ownBackground = self->CanOwnBackground();
                    }
                    self->Refresh();
                } catch (...) {
                    self->updating = false;
                    Log("FAIL", "adaptive settings appearance hr=%08lx", winrt::to_hresult());
                }
            }
        });
        list.push_back({object, property, token});
    }
    bool CanOwnBackground() const {
        return target.ReadLocalValue(C::Control::BackgroundProperty()) == X::DependencyProperty::UnsetValue() &&
               !Styled(target.Style(), C::Control::BackgroundProperty());
    }
    bool CanOwnTheme() const {
        return target.RequestedTheme() == X::ElementTheme::Default &&
               target.ReadLocalValue(X::FrameworkElement::RequestedThemeProperty()) ==
                   X::DependencyProperty::UnsetValue() &&
               !Styled(target.Style(), X::FrameworkElement::RequestedThemeProperty());
    }
    void Refresh() {
        if (updating) return;
        updating = true;
        struct Reset {
            bool& value;
            ~Reset() { value = false; }
        } reset{updating};
        auto nextContent = target.Content().try_as<X::DependencyObject>();
        if (nextContent != content) {
            Clear(contentWatches);
            content = nextContent;
            WatchProperty(contentWatches, content, BackgroundProperty(content));
            if (content.try_as<X::FrameworkElement>())
                WatchProperty(contentWatches, content, X::FrameworkElement::RequestedThemeProperty());
        }
        M::Brush source{nullptr};
        auto backgroundProperty = BackgroundProperty(content);
        if (backgroundProperty) source = content.GetValue(backgroundProperty).try_as<M::Brush>();
        if (!ownBackground) source = target.Background();
        if (source != observedBrush) {
            Clear(brushWatches);
            observedBrush = source;
            WatchProperty(brushWatches, source, M::Brush::OpacityProperty());
            if (auto solid = source.try_as<M::SolidColorBrush>())
                WatchProperty(brushWatches, solid, M::SolidColorBrush::ColorProperty());
        }
        auto inherited = page.ActualTheme();
        popup.RequestedTheme(inherited);
        auto chosen = ThemeFor(source, inherited);
        if (auto element = content.try_as<X::FrameworkElement>()) {
            if (element.RequestedTheme() != X::ElementTheme::Default) chosen = element.RequestedTheme();
        }
        if (ownTheme && target.RequestedTheme() != chosen) target.RequestedTheme(chosen);
        if (ownBackground) {
            if (source) {
                if (target.Background() != source) target.Background(source);
            } else
                target.ClearValue(C::Control::BackgroundProperty());
        }
    }
    void Start() {
        ownTheme = CanOwnTheme();
        ownBackground = CanOwnBackground();
        WatchProperty(targetWatches, target, X::FrameworkElement::RequestedThemeProperty(), 1);
        WatchProperty(targetWatches, target, C::Control::BackgroundProperty(), 2);
        WatchProperty(targetWatches, target, X::FrameworkElement::StyleProperty(), 3);
        WatchProperty(targetWatches, target, C::ContentControl::ContentProperty());
        std::weak_ptr<Appearance> weak = shared_from_this();
        pageToken = page.ActualThemeChanged([weak](auto const&, auto const&) {
            if (auto self = weak.lock()) {
                try {
                    self->Refresh();
                } catch (...) {
                    Log("FAIL", "adaptive page theme hr=%08lx", winrt::to_hresult());
                }
            }
        });
        watchingPage = true;
        Refresh();
    }
    ~Appearance() {
        Clear(targetWatches);
        Clear(contentWatches);
        Clear(brushWatches);
        try {
            if (watchingPage) page.ActualThemeChanged(pageToken);
            if (ownTheme) target.ClearValue(X::FrameworkElement::RequestedThemeProperty());
            if (ownBackground) target.ClearValue(C::Control::BackgroundProperty());
        } catch (...) {}
    }
};
static std::shared_ptr<Appearance> activeAppearance;
void DisconnectSettingsAppearance() { activeAppearance.reset(); }
HRESULT ConnectSettingsAppearance(IInspectable* flyout, IInspectable* page, IInspectable* popup) {
    try {
        activeAppearance.reset();
        auto appearance = std::make_shared<Appearance>();
        Object object{nullptr};
        winrt::copy_from_abi(object, flyout);
        appearance->target = object.as<C::SettingsFlyout>();
        winrt::copy_from_abi(object, page);
        appearance->page = object.as<X::FrameworkElement>();
        winrt::copy_from_abi(object, popup);
        appearance->popup = object.as<C::Primitives::Popup>();
        appearance->Start();
        activeAppearance = std::move(appearance);
        return S_OK;
    } catch (...) {
        return winrt::to_hresult();
    }
}
}
