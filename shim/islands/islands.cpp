#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000007
// Host a managed Store app with real system XAML in a desktop island.
#include <windows.h>
#undef GetCurrentTime
#include <roapi.h>
#include <winstring.h>
#include <wrl.h>
#include <wrl/event.h>
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.hosting.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <windows.applicationmodel.activation.h>
#include <windows.ui.core.h>
#include <shlwapi.h>
#include <xmllite.h>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <optional>
#pragma comment(lib, "runtimeobject.lib")
#pragma comment(lib, "user32.lib")
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::ApplicationModel::Activation;
namespace Hosting = ABI::Windows::UI::Xaml::Hosting;
using ABI::Windows::Foundation::Rect;

#include "registry.h"
#include "log.h"
#include "host/host.h"
#include "host/event_args.h"
#include "islands.h"
#include "webview.h"
#include "package/package.h"
namespace appx {
HRESULT MakeCoreWindow(ABI::Windows::UI::Core::ICoreWindow** value);
static std::string Narrow(HSTRING s) {
    UINT32 n = 0;
    const wchar_t* w = WindowsGetStringRawBuffer(s, &n);
    std::string out(n, '?');
    for (UINT32 i = 0; i < n; ++i) out[i] = w[i] < 128 ? static_cast<char>(w[i]) : '?';
    return out;
}

static HRESULT GetReal(const wchar_t* name, REFIID iid, void** out) {
    return OsGetActivationFactory(Wrappers::HStringReference(name).Get(), iid, out);
}
// ---- island state --------------------------------------------------------------
static HWND islandWindow;
static ComPtr<Hosting::IWindowsXamlManager> xamlManager;
static ComPtr<Hosting::IDesktopWindowXamlSource> xamlSource;
static ComPtr<IDesktopWindowXamlSourceNative2> xamlNative;
static ComPtr<IInspectable> appInstance;
// The system Application itself: its managed outer may still be constructing.
static ComPtr<IApplication> systemApplication;
HRESULT ConnectIslandScrolling(IInspectable* root);
HRESULT MakeIslandGridFactory(IActivationFactory** value);
static ComPtr<IUIElement> pendingContent;
static ComPtr<ABI::Windows::UI::Core::ICoreDispatcher> islandDispatcher;
using RequestedThemeCall = HRESULT(STDMETHODCALLTYPE*)(IApplication*, ApplicationTheme);
using RequestedThemeGetCall = HRESULT(STDMETHODCALLTYPE*)(IApplication*, ApplicationTheme*);
static RequestedThemeCall realRequestedTheme;
static RequestedThemeGetCall realGetRequestedTheme;
// The application's theme: its own request or authored theme, otherwise its
// platform's default. Windows 8 apps defaulted to dark; Windows 10 apps follow
// the system, as modern islands already do, so they start with none.
static std::optional<ApplicationTheme> requestedTheme;
static bool xamlRunning, applicationThemeSet, themeApplied;
// Give the system Application the theme as soon as XAML runs, so resources
// the app loads later resolve against it. XAML refuses this once the app has
// begun loading its own XAML, which is how generated App constructors start.
static void ApplyApplicationTheme() {
    if (xamlRunning || !systemApplication) return;
    xamlRunning = true;
    if (!requestedTheme) return;
    auto hr = realRequestedTheme(systemApplication.Get(), *requestedTheme);
    applicationThemeSet = SUCCEEDED(hr);
    appx::Log("TRACE", "application theme %d hr=0x%08lX", *requestedTheme, hr);
}
// The theme in the application's text XAML, from the URI it loads.
static std::optional<ApplicationTheme> AuthoredTheme(ABI::Windows::Foundation::IUriRuntimeClass* uri) {
    Wrappers::HString scheme, path;
    if (FAILED(uri->get_SchemeName(scheme.GetAddressOf())) || FAILED(uri->get_Path(path.GetAddressOf())) ||
        _wcsicmp(scheme.GetRawBuffer(nullptr), L"ms-appx") != 0)
        return {};
    const auto relative = std::filesystem::path(path.GetRawBuffer(nullptr)).relative_path().lexically_normal();
    if (relative.empty() || *relative.begin() == L"..") return {};
    const auto file = CurrentPackage().directory / relative;
    ComPtr<IStream> stream;
    ComPtr<IXmlReader> reader;
    if (FAILED(SHCreateStreamOnFileEx(file.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &stream)) ||
        FAILED(CreateXmlReader(IID_PPV_ARGS(&reader), nullptr)) ||
        FAILED(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit)) ||
        FAILED(reader->SetInput(stream.Get())))
        return {};
    XmlNodeType type;
    while (reader->Read(&type) == S_OK) {
        if (type != XmlNodeType_Element) continue;
        const wchar_t *name = nullptr, *value = nullptr;
        if (FAILED(reader->GetLocalName(&name, nullptr)) || !name || wcscmp(name, L"Application") != 0 ||
            reader->MoveToAttributeByName(L"RequestedTheme", nullptr) != S_OK ||
            FAILED(reader->GetValue(&value, nullptr)) || !value)
            return {};
        if (!wcscmp(value, L"Light")) return ApplicationTheme_Light;
        if (!wcscmp(value, L"Dark")) return ApplicationTheme_Dark;
        return {};
    }
    return {};
}
// The parser sets an authored Application theme internally, bypassing COM
// setters, in text XAML and compiled XBF alike. A changed system theme shows
// it; with our theme refused, an unchanged one may be the Windows mode itself,
// so the application's text XAML decides between that and the default.
template <typename Load>
static HRESULT LoadApplicationTheme(IInspectable* component, ABI::Windows::Foundation::IUriRuntimeClass* uri,
                                    Load load) {
    ComPtr<IUnknown> identity, application;
    ApplicationTheme before, after;
    if (!systemApplication || !component || !uri || FAILED(component->QueryInterface(IID_PPV_ARGS(&identity))) ||
        FAILED(appInstance.As(&application)) || identity != application ||
        FAILED(realGetRequestedTheme(systemApplication.Get(), &before)))
        return load();
    auto hr = load();
    if (FAILED(hr) || FAILED(realGetRequestedTheme(systemApplication.Get(), &after))) return hr;
    if (after != before || applicationThemeSet)
        requestedTheme = after;
    else if (auto authored = AuthoredTheme(uri))
        requestedTheme = authored;
    appx::Log("TRACE", "application XAML theme %d (system %d)", requestedTheme ? *requestedTheme : -1, after);
    return hr;
}
// Islands inherit the desktop preference wherever the Application theme did
// not reach. Apply the application's theme to each visual root without
// replacing an element's explicit theme.
static HRESULT ApplyRootTheme(IInspectable* element) {
    if (!element || !requestedTheme) return S_OK;
    ComPtr<IFrameworkElement2> root;
    auto hr = element->QueryInterface(IID_PPV_ARGS(&root));
    if (hr == E_NOINTERFACE) return S_OK;
    if (FAILED(hr)) return hr;
    ElementTheme theme;
    hr = root->get_RequestedTheme(&theme);
    if (SUCCEEDED(hr) && theme == ElementTheme_Default)
        hr =
            root->put_RequestedTheme(*requestedTheme == ApplicationTheme_Dark ? ElementTheme_Dark : ElementTheme_Light);
    return hr;
}
static bool initializingXaml;
static HRESULT InitXaml(const char* where) {
    if (xamlManager || initializingXaml) return S_OK;
    appx::Log("TRACE", "-> InitializeForCurrentThread (%s)", where);
    ComPtr<Hosting::IWindowsXamlManagerStatics> statics;
    HRESULT hr = GetReal(L"Windows.UI.Xaml.Hosting.WindowsXamlManager", IID_PPV_ARGS(&statics));
    if (SUCCEEDED(hr)) {
        // XAML can request managed metadata while initializing; those requests
        // may activate further XAML factories through the CLR hook.
        initializingXaml = true;
        hr = statics->InitializeForCurrentThread(&xamlManager);
        initializingXaml = false;
        if (SUCCEEDED(hr)) ApplyApplicationTheme();
        if (SUCCEEDED(hr) && !islandDispatcher) {
            ComPtr<ABI::Windows::UI::Core::ICoreDispatcher> dispatcher;
            hr = IslandDispatcher(&dispatcher);
        }
    }
    appx::Log("TRACE", "STAGE islands: WindowsXamlManager.InitializeForCurrentThread (%s) hr=0x%08lX", where, hr);
    return hr;
}

HRESULT IslandDispatcher(ABI::Windows::UI::Core::ICoreDispatcher** value) {
    if (!value) return E_POINTER;
    // CoreWindow can be retained by worker threads. Its Dispatcher belongs to
    // the island's UI thread, not whichever thread calls this getter later.
    if (islandDispatcher) return islandDispatcher.CopyTo(value);
    *value = nullptr;
    ComPtr<ABI::Windows::UI::Core::ICoreWindowStatic> statics;
    ComPtr<ABI::Windows::UI::Core::ICoreWindow> core;
    auto hr = GetReal(L"Windows.UI.Core.CoreWindow", IID_PPV_ARGS(&statics));
    if (SUCCEEDED(hr)) hr = statics->GetForCurrentThread(&core);
    if (FAILED(hr)) return hr;
    if (!core) return E_UNEXPECTED;
    hr = core->get_Dispatcher(value);
    if (SUCCEEDED(hr) && GetCurrentThreadId() == HostThread()) islandDispatcher = *value;
    return hr;
}
static ComPtr<ILaunchActivatedEventArgs> IslandLaunchArgs() {
    ComPtr<IActivatedEventArgs> args;
    ComPtr<ILaunchActivatedEventArgs> launch;
    if (SUCCEEDED(MakeLaunchArgs(&args))) args.As(&launch);
    return launch;
}
// ---- fake Window.Current backed by the island ---------------------------------
class IslandWindow final : public RuntimeClass<RuntimeClassFlags<WinRt>, IWindow> {
    InspectableClass(L"Windows.UI.Xaml.Window", BaseTrust);
    EventSource<IWindowActivatedEventHandler> activated_;
    EventSource<IWindowClosedEventHandler> closed_;
    EventSource<IWindowSizeChangedEventHandler> sizeChanged_;
    EventSource<IWindowVisibilityChangedEventHandler> visibilityChanged_;

public:
    void OnMessage(UINT message, WPARAM wparam, LPARAM) {
        if (message == WM_ACTIVATE) {
            auto args = Make<ActivationArgs>(LOWORD(wparam) == WA_INACTIVE
                                                 ? ABI::Windows::UI::Core::CoreWindowActivationState_Deactivated
                                                 : ABI::Windows::UI::Core::CoreWindowActivationState_CodeActivated);
            if (args) activated_.InvokeAll(this, args.Get());
        } else if (message == WM_SIZE && wparam != SIZE_MINIMIZED) {
            Rect bounds{};
            get_Bounds(&bounds);
            auto args = Make<WindowSizeArgs>(ABI::Windows::Foundation::Size{bounds.Width, bounds.Height});
            if (args) sizeChanged_.InvokeAll(this, args.Get());
        } else if (message == WM_SHOWWINDOW) {
            auto args = Make<VisibilityArgs>(wparam != 0);
            if (args) visibilityChanged_.InvokeAll(this, args.Get());
        }
    }
    HRESULT STDMETHODCALLTYPE get_Bounds(Rect* v) override {
        RECT r{};
        GetClientRect(HostWindow(), &r);
        float scale = GetDpiForWindow(HostWindow()) / 96.0f;
        *v = {0, 0, (r.right - r.left) / scale, (r.bottom - r.top) / scale};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Visible(boolean* v) override {
        *v = true;
        return S_OK;
    }
    ComPtr<IUIElement> content_;

public:
    HRESULT STDMETHODCALLTYPE get_Content(IUIElement** v) override { return content_.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE put_Content(IUIElement* v) override {
        // XAML runs OnLaunched inside InitializeForCurrentThread, before the
        // island exists; hold the content until it is attached.
        content_ = v;
        HRESULT hr = ConnectIslandScrolling(v);
        if (FAILED(hr)) return hr;
        if (v) {
            hr = ApplyRootTheme(v);
            if (FAILED(hr)) return hr;
            themeApplied = true;
        }
        if (xamlSource)
            hr = xamlSource->put_Content(v);
        else
            pendingContent = v;
        HSTRING n = nullptr;
        if (v) v->GetRuntimeClassName(&n);
        appx::Log("TRACE", "STAGE content: Window.Current.Content = %s hr=0x%08lX", n ? Narrow(n).c_str() : "null", hr);
        WindowsDeleteString(n);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_CoreWindow(ABI::Windows::UI::Core::ICoreWindow** v) override {
        // The island's hidden CoreWindow receives no pointer events. Game
        // subscriptions use the shared input window, fed by the real panel.
        return MakeCoreWindow(v);
    }
    HRESULT STDMETHODCALLTYPE get_Dispatcher(ABI::Windows::UI::Core::ICoreDispatcher** v) override {
        return IslandDispatcher(v);
    }
    HRESULT STDMETHODCALLTYPE add_Activated(IWindowActivatedEventHandler* h, EventRegistrationToken* t) override {
        return activated_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_Activated(EventRegistrationToken t) override { return activated_.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_Closed(IWindowClosedEventHandler* h, EventRegistrationToken* t) override {
        return closed_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_Closed(EventRegistrationToken t) override { return closed_.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_SizeChanged(IWindowSizeChangedEventHandler* h, EventRegistrationToken* t) override {
        return sizeChanged_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_SizeChanged(EventRegistrationToken t) override { return sizeChanged_.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_VisibilityChanged(IWindowVisibilityChangedEventHandler* h,
                                                    EventRegistrationToken* t) override {
        return visibilityChanged_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_VisibilityChanged(EventRegistrationToken t) override {
        return visibilityChanged_.Remove(t);
    }
    HRESULT STDMETHODCALLTYPE Activate() override {
        appx::Log("TRACE", "STAGE activate: Window.Current.Activate()");
        ShowWindow(HostWindow(), SW_SHOW);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        PostMessageW(HostWindow(), WM_CLOSE, 0, 0);
        return S_OK;
    }
};
static ComPtr<IslandWindow> currentWindow;
void IslandHostMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (currentWindow) currentWindow->OnMessage(message, wparam, lparam);
}
// Factories can't use InspectableClass (WRL static assert), so name them by hand.
#define FACTORY_NAME(text)                                                                                             \
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* v) noexcept override {                                      \
        return WindowsCreateString(text, static_cast<UINT32>(wcslen(text)), v);                                        \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* v) noexcept override {                                         \
        *v = BaseTrust;                                                                                                \
        return S_OK;                                                                                                   \
    }
class IslandWindowFactory final : public RuntimeClass<RuntimeClassFlags<WinRt>, IActivationFactory, IWindowStatics> {
public:
    FACTORY_NAME(L"Windows.UI.Xaml.Window")
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_Current(IWindow** v) override {
        if (!currentWindow) currentWindow = Make<IslandWindow>();
        return currentWindow ? currentWindow.CopyTo(v) : E_OUTOFMEMORY;
    }
};

// ---- OnLaunched interception -----------------------------------------------------
// XAML calls the app's OnLaunched itself (inside InitializeForCurrentThread) with
// the system's SplashScreen, whose Dismissed never fires in an island. Patch the
// app's IApplicationOverrides::OnLaunched slot to pass our args instead.
using OnLaunchedCall = HRESULT(STDMETHODCALLTYPE*)(IApplicationOverrides*, ILaunchActivatedEventArgs*);
static OnLaunchedCall realOnLaunched;
static bool applicationConstructed;
static bool launchPending;
static HRESULT STDMETHODCALLTYPE HookOnLaunched(IApplicationOverrides* self, ILaunchActivatedEventArgs* args) {
    // Initialization can invoke OnLaunched synchronously. Capture the real UI
    // dispatcher before application code starts any background continuations.
    ComPtr<ABI::Windows::UI::Core::ICoreDispatcher> dispatcher;
    auto hr = IslandDispatcher(&dispatcher);
    if (FAILED(hr)) return hr;
    // XAML is running, possibly still inside InitializeForCurrentThread.
    ApplyApplicationTheme();
    // Initializing XAML for controls in derived Application constructors can
    // launch the app synchronously.
    // Let the managed startup callback finish before entering game code.
    if (!applicationConstructed) {
        launchPending = true;
        return S_OK;
    }
    hr = HookIslandWebViewMetadata(appInstance.Get());
    if (FAILED(hr)) return hr;
    appx::Log("TRACE", "STAGE launched: OnLaunched intercepted (system args %p), passing portable launch args", args);
    auto argsOverride = IslandLaunchArgs();
    return argsOverride ? realOnLaunched(self, argsOverride.Get()) : E_OUTOFMEMORY;
}
static HRESULT PatchOnLaunched() {
    ComPtr<IApplicationOverrides> overrides;
    if (!appInstance) return E_UNEXPECTED;
    auto hr = appInstance.As(&overrides);
    if (FAILED(hr)) return hr;
    void** slot = *reinterpret_cast<void***>(overrides.Get()) + 7; // IInspectable (6), OnActivated, OnLaunched
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return HRESULT_FROM_WIN32(GetLastError());
    realOnLaunched = static_cast<OnLaunchedCall>(*slot);
    *slot = reinterpret_cast<void*>(&HookOnLaunched);
    VirtualProtect(slot, sizeof(void*), old, &old);
    appx::Log("TRACE", "OnLaunched slot patched");
    return S_OK;
}

// ---- CoreWindow bounds ------------------------------------------------------------
// Islands create a hidden CoreWindow per thread with a token size. Win8 games
// size their swap chains from CoreWindow.Bounds, so report the host's client
// area (in DIPs) instead.
using BoundsCall = HRESULT(STDMETHODCALLTYPE*)(ABI::Windows::UI::Core::ICoreWindow*, Rect*);
static BoundsCall realBounds;
static HRESULT STDMETHODCALLTYPE HookBounds(ABI::Windows::UI::Core::ICoreWindow* self, Rect* v) {
    if (!HostWindow() || !v) return realBounds(self, v);
    RECT r{};
    GetClientRect(HostWindow(), &r);
    float scale = GetDpiForWindow(HostWindow()) / 96.0f;
    *v = {0, 0, (r.right - r.left) / scale, (r.bottom - r.top) / scale};
    return S_OK;
}
static HRESULT PatchCoreWindowBounds() {
    ComPtr<ABI::Windows::UI::Core::ICoreWindowStatic> statics;
    ComPtr<ABI::Windows::UI::Core::ICoreWindow> core;
    if (FAILED(GetReal(L"Windows.UI.Core.CoreWindow", IID_PPV_ARGS(&statics))) ||
        FAILED(statics->GetForCurrentThread(&core)) || !core) {
        appx::Log("TRACE", "CoreWindow bounds: no CoreWindow on this thread");
        return E_UNEXPECTED;
    }
    Rect before{};
    core->get_Bounds(&before);
    void** slot = *reinterpret_cast<void***>(core.Get()) + 7; // IInspectable (6), AutomationHostProvider, Bounds
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return HRESULT_FROM_WIN32(GetLastError());
    realBounds = static_cast<BoundsCall>(*slot);
    *slot = reinterpret_cast<void*>(&HookBounds);
    VirtualProtect(slot, sizeof(void*), old, &old);
    Rect after{};
    core->get_Bounds(&after);
    appx::Log("TRACE", "CoreWindow bounds: hidden window was %.0fx%.0f, now reports %.0fx%.0f", before.Width,
              before.Height, after.Width, after.Height);
    return S_OK;
}

// Win8 apps create detached popups. Islands require their XamlRoot explicitly.
using PopupOpenCall = HRESULT(STDMETHODCALLTYPE*)(Controls::Primitives::IPopup*, boolean);
static PopupOpenCall originalPopupOpen;
static HRESULT STDMETHODCALLTYPE OpenPopup(Controls::Primitives::IPopup* popup, boolean open) {
    if (open && xamlSource) {
        ComPtr<IUIElement> child;
        auto themeHr = popup->get_Child(&child);
        if (SUCCEEDED(themeHr)) themeHr = ApplyRootTheme(child.Get());
        if (FAILED(themeHr)) return themeHr;
        ComPtr<IUIElement> content;
        ComPtr<IUIElement10> rootElement, popupElement;
        ComPtr<IXamlRoot> root;
        if (SUCCEEDED(xamlSource->get_Content(&content)) && content && SUCCEEDED(content.As(&rootElement)) &&
            SUCCEEDED(rootElement->get_XamlRoot(&root)) && root &&
            SUCCEEDED(popup->QueryInterface(IID_PPV_ARGS(&popupElement)))) {
            auto hr = popupElement->put_XamlRoot(root.Get());
            if (FAILED(hr)) return hr;
        }
    }
    return originalPopupOpen(popup, open);
}
static HRESULT InitializePopups() {
    ComPtr<IInspectable> instance;
    ComPtr<Controls::Primitives::IPopup> popup;
    auto hr =
        OsActivateInstance(Wrappers::HStringReference(L"Windows.UI.Xaml.Controls.Primitives.Popup").Get(), &instance);
    if (SUCCEEDED(hr)) hr = instance.As(&popup);
    if (FAILED(hr)) return hr;
    void** slot = *reinterpret_cast<void***>(popup.Get()) + 9;
    DWORD protection;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return HRESULT_FROM_WIN32(GetLastError());
    if (!originalPopupOpen) {
        originalPopupOpen = reinterpret_cast<PopupOpenCall>(*slot);
        InterlockedExchangePointer(slot, reinterpret_cast<void*>(&OpenPopup));
    }
    DWORD ignored;
    VirtualProtect(slot, sizeof(void*), protection, &ignored);
    return S_OK;
}
// Keep the real SettingsFlyout control and its template, but host it in a
// standard island Popup: its Windows 8 Show implementation uses CoreWindow's
// token 1x1 window for placement even when the control has an island XamlRoot.
using SettingsFlyoutHideCall = HRESULT(STDMETHODCALLTYPE*)(Controls::ISettingsFlyout*);
static SettingsFlyoutHideCall originalSettingsFlyoutHide;
static ComPtr<Controls::Primitives::IPopup> settingsPopup;
static ComPtr<Controls::ISettingsFlyout> settingsFlyout;
static EventRegistrationToken settingsBackToken;
// The popup sizes the flyout to the island. Keep the flyout's own Width and
// Height (local values or UnsetValue) to clamp against and restore on closing.
struct SettingsSize {
    ComPtr<IDependencyProperty> property;
    ComPtr<IInspectable> local;
};
static SettingsSize settingsSizes[2];
static double settingsWidth;
static HRESULT SaveSettingsSize(Controls::ISettingsFlyout* flyout) {
    ComPtr<IFrameworkElementStatics> statics;
    ComPtr<IDependencyObject> object;
    ComPtr<IFrameworkElement> element;
    auto hr = GetReal(L"Windows.UI.Xaml.FrameworkElement", IID_PPV_ARGS(&statics));
    if (SUCCEEDED(hr)) hr = statics->get_WidthProperty(&settingsSizes[0].property);
    if (SUCCEEDED(hr)) hr = statics->get_HeightProperty(&settingsSizes[1].property);
    if (SUCCEEDED(hr)) hr = flyout->QueryInterface(IID_PPV_ARGS(&object));
    for (auto& size : settingsSizes)
        if (SUCCEEDED(hr)) hr = object->ReadLocalValue(size.property.Get(), &size.local);
    if (SUCCEEDED(hr)) hr = flyout->QueryInterface(IID_PPV_ARGS(&element));
    double width = 0;
    if (SUCCEEDED(hr)) hr = element->get_Width(&width);
    settingsWidth = width > 0 ? width : 320;
    if (FAILED(hr))
        for (auto& size : settingsSizes) size = {};
    return hr;
}
static void RestoreSettingsSize(Controls::ISettingsFlyout* flyout) {
    ComPtr<IDependencyPropertyStatics> statics;
    ComPtr<IDependencyObject> object;
    ComPtr<IInspectable> unset;
    ComPtr<IUnknown> unsetIdentity;
    if (flyout && SUCCEEDED(GetReal(L"Windows.UI.Xaml.DependencyProperty", IID_PPV_ARGS(&statics))) &&
        SUCCEEDED(statics->get_UnsetValue(&unset)) && SUCCEEDED(unset.As(&unsetIdentity)) &&
        SUCCEEDED(flyout->QueryInterface(IID_PPV_ARGS(&object)))) {
        for (auto& size : settingsSizes) {
            if (!size.property || !size.local) continue;
            ComPtr<IUnknown> identity;
            if (SUCCEEDED(size.local.As(&identity)) && identity == unsetIdentity)
                object->ClearValue(size.property.Get());
            else
                object->SetValue(size.property.Get(), size.local.Get());
        }
    }
    for (auto& size : settingsSizes) size = {};
}
HRESULT ConnectSettingsAppearance(IInspectable* flyout, IInspectable* page, IInspectable* popup);
void DisconnectSettingsAppearance();
static void CloseSettingsFlyout() {
    DisconnectSettingsAppearance();
    auto popup = std::move(settingsPopup);
    auto flyout = std::move(settingsFlyout);
    if (flyout) flyout->remove_BackClick(settingsBackToken);
    if (popup) {
        popup->put_IsOpen(false);
        popup->put_Child(nullptr);
    }
    RestoreSettingsSize(flyout.Get());
}
static HRESULT SizeSettingsFlyout(double width, double height) {
    if (!settingsPopup || !settingsFlyout) return S_OK;
    ComPtr<IFrameworkElement> element;
    auto hr = settingsFlyout.As(&element);
    const double actual = (std::min)(settingsWidth, width);
    if (SUCCEEDED(hr)) hr = element->put_Width(actual);
    if (SUCCEEDED(hr)) hr = element->put_Height(height);
    if (SUCCEEDED(hr)) hr = settingsPopup->put_HorizontalOffset(width - actual);
    return hr;
}
static HRESULT OpenSettingsFlyout(Controls::ISettingsFlyout* flyout, bool independent) {
    CloseSettingsFlyout();
    if (!xamlSource) return E_UNEXPECTED;
    ComPtr<IUIElement> content, child;
    ComPtr<IUIElement10> contentElement, popupElement;
    ComPtr<IXamlRoot> root;
    auto hr = xamlSource->get_Content(&content);
    if (SUCCEEDED(hr) && !content) return E_UNEXPECTED;
    if (SUCCEEDED(hr)) hr = content.As(&contentElement);
    if (SUCCEEDED(hr)) hr = contentElement->get_XamlRoot(&root);
    if (SUCCEEDED(hr)) hr = flyout->QueryInterface(IID_PPV_ARGS(&child));
    ComPtr<IInspectable> instance;
    ComPtr<Controls::Primitives::IPopup> popup;
    if (SUCCEEDED(hr))
        hr = OsActivateInstance(Wrappers::HStringReference(L"Windows.UI.Xaml.Controls.Primitives.Popup").Get(),
                                &instance);
    if (SUCCEEDED(hr)) hr = instance.As(&popup);
    if (SUCCEEDED(hr)) hr = instance.As(&popupElement);
    if (SUCCEEDED(hr)) hr = popupElement->put_XamlRoot(root.Get());
    if (SUCCEEDED(hr)) hr = ConnectSettingsAppearance(flyout, content.Get(), popup.Get());
    if (SUCCEEDED(hr)) hr = popup->put_Child(child.Get());
    if (SUCCEEDED(hr)) hr = popup->put_IsLightDismissEnabled(true);
    if (FAILED(hr)) {
        DisconnectSettingsAppearance();
        return hr;
    }
    settingsPopup = popup;
    settingsFlyout = flyout;
    ABI::Windows::Foundation::Size size{};
    hr = SaveSettingsSize(flyout);
    if (SUCCEEDED(hr)) hr = root->get_Size(&size);
    if (SUCCEEDED(hr)) hr = SizeSettingsFlyout(size.Width, size.Height);
    auto back = Callback<Controls::IBackClickEventHandler>(
        [independent](IInspectable*, Controls::IBackClickEventArgs* args) -> HRESULT {
            boolean handled = false;
            auto result = args->get_Handled(&handled);
            if (FAILED(result) || handled) return result;
            args->put_Handled(true);
            CloseSettingsFlyout();
            return independent ? S_OK : ShowGameSettings();
        });
    if (SUCCEEDED(hr)) hr = flyout->add_BackClick(back.Get(), &settingsBackToken);
    auto closed = Callback<__FIEventHandler_1_IInspectable>([](IInspectable* sender, IInspectable*) -> HRESULT {
        ComPtr<Controls::Primitives::IPopup> popup;
        if (SUCCEEDED(sender->QueryInterface(IID_PPV_ARGS(&popup))) && popup == settingsPopup) CloseSettingsFlyout();
        return S_OK;
    });
    EventRegistrationToken closedToken{};
    if (SUCCEEDED(hr)) hr = popup->add_Closed(closed.Get(), &closedToken);
    if (SUCCEEDED(hr)) hr = originalPopupOpen(popup.Get(), true);
    if (FAILED(hr)) CloseSettingsFlyout();
    return hr;
}
static HRESULT STDMETHODCALLTYPE ShowSettingsFlyout(Controls::ISettingsFlyout* flyout) {
    return OpenSettingsFlyout(flyout, false);
}
static HRESULT STDMETHODCALLTYPE ShowIndependentSettingsFlyout(Controls::ISettingsFlyout* flyout) {
    return OpenSettingsFlyout(flyout, true);
}
static HRESULT STDMETHODCALLTYPE HideSettingsFlyout(Controls::ISettingsFlyout* flyout) {
    if (settingsFlyout.Get() == flyout) {
        CloseSettingsFlyout();
        return S_OK;
    }
    return originalSettingsFlyoutHide(flyout);
}
static HRESULT InitializeSettingsFlyouts() {
    ComPtr<Controls::ISettingsFlyoutFactory> factory;
    ComPtr<Controls::ISettingsFlyout> flyout;
    ComPtr<IInspectable> inner;
    auto hr = GetReal(L"Windows.UI.Xaml.Controls.SettingsFlyout", IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) hr = factory->CreateInstance(nullptr, &inner, &flyout);
    if (FAILED(hr)) return hr;
    void** table = *reinterpret_cast<void***>(flyout.Get());
    DWORD protection;
    if (!VirtualProtect(table + 17, 3 * sizeof(void*), PAGE_READWRITE, &protection))
        return HRESULT_FROM_WIN32(GetLastError());
    if (!originalSettingsFlyoutHide) {
        originalSettingsFlyoutHide = reinterpret_cast<SettingsFlyoutHideCall>(table[19]);
        InterlockedExchangePointer(table + 17, reinterpret_cast<void*>(&ShowSettingsFlyout));
        InterlockedExchangePointer(table + 18, reinterpret_cast<void*>(&ShowIndependentSettingsFlyout));
        InterlockedExchangePointer(table + 19, reinterpret_cast<void*>(&HideSettingsFlyout));
    }
    DWORD ignored;
    VirtualProtect(table + 17, 3 * sizeof(void*), protection, &ignored);
    return S_OK;
}
void ResizeIslandView(int width, int height) {
    // Browser HWNDs are siblings above the bridge so they receive native input.
    if (islandWindow) SetWindowPos(islandWindow, nullptr, 0, 0, width, height, SWP_NOACTIVATE | SWP_NOZORDER);
    const double scale = GetDpiForWindow(HostWindow()) / 96.0;
    SizeSettingsFlyout(width / scale, height / scale);
}
void IslandInputMessage(UINT message, WPARAM wparam, LPARAM lparam);
bool IslandPreTranslate(MSG* message) {
    if (RouteIslandWebViewInput(*message)) return true;
    // Browser children are siblings of the island. XAML must not consume
    // their native mouse/keyboard messages before Chromium receives them.
    if (IsIslandWebViewWindow(message->hwnd)) return false;
    if (message->hwnd == HostWindow() || IsChild(HostWindow(), message->hwnd)) {
        const auto kind = message->message;
        // Pointer input belongs to XAML's routed events, including capture.
        // Posted WM_MOUSE messages do not exercise the real island input path.
        if (kind >= WM_KEYFIRST && kind <= WM_KEYLAST) IslandInputMessage(kind, message->wParam, message->lParam);
    }
    BOOL handled = FALSE;
    if (xamlNative) xamlNative->PreTranslateMessage(message, &handled);
    return handled != FALSE;
}
static HRESULT RunIsland(IApplicationInitializationCallback* callback) {
    if (!callback) return E_INVALIDARG;
    return RunHost([&]() -> HRESULT {
        HRESULT hr = callback->Invoke(nullptr);
        if (FAILED(hr)) return hr;
        applicationConstructed = true;
        hr = InitXaml("after application callback");
        if (FAILED(hr)) return hr;
        if (launchPending) {
            launchPending = false;
            ComPtr<IApplicationOverrides> overrides;
            hr = appInstance.As(&overrides);
            if (SUCCEEDED(hr)) hr = HookOnLaunched(overrides.Get(), nullptr);
            if (FAILED(hr)) return hr;
        }
        ComPtr<IInspectable> source;
        hr = OsActivateInstance(Wrappers::HStringReference(L"Windows.UI.Xaml.Hosting.DesktopWindowXamlSource").Get(),
                                &source);
        if (SUCCEEDED(hr)) hr = source.As(&xamlSource);
        if (SUCCEEDED(hr)) hr = source.As(&xamlNative);
        if (SUCCEEDED(hr)) hr = xamlNative->AttachToWindow(HostWindow());
        if (SUCCEEDED(hr)) hr = xamlNative->get_WindowHandle(&islandWindow);
        if (FAILED(hr)) return hr;
        hr = PatchCoreWindowBounds();
        if (FAILED(hr)) return hr;
        RECT r{};
        GetClientRect(HostWindow(), &r);
        SetWindowPos(islandWindow, HWND_TOP, 0, 0, r.right, r.bottom, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if (pendingContent) {
            hr = xamlSource->put_Content(pendingContent.Get());
            pendingContent.Reset();
            if (FAILED(hr)) return hr;
        }
        hr = InitializePopups();
        if (FAILED(hr)) return hr;
        hr = InitializeSettingsFlyouts();
        if (FAILED(hr)) return hr;
        ActivateHostWindow();
        DismissSplashScreen();
        return S_OK;
    });
}
void CloseIslandView() {
    ComPtr<ABI::Windows::UI::Core::ICoreWindowStatic> statics;
    ComPtr<ABI::Windows::UI::Core::ICoreWindow> core;
    auto hr = GetReal(L"Windows.UI.Core.CoreWindow", IID_PPV_ARGS(&statics));
    if (SUCCEEDED(hr)) hr = statics->GetForCurrentThread(&core);
    if (SUCCEEDED(hr) && core) hr = core->Close();
    appx::Log("INFO", "island CoreWindow close hr=%08lx", hr);
}
static EventSource<ISuspendingEventHandler> suspending;
static HRESULT STDMETHODCALLTYPE AddSuspending(IApplication*, ISuspendingEventHandler* handler,
                                               EventRegistrationToken* token) {
    return suspending.Add(handler, token);
}
static HRESULT STDMETHODCALLTYPE RemoveSuspending(IApplication*, EventRegistrationToken token) {
    return suspending.Remove(token);
}
static HRESULT STDMETHODCALLTYPE RequestTheme(IApplication* application, ApplicationTheme theme) {
    if (theme != ApplicationTheme_Dark && theme != ApplicationTheme_Light) return E_INVALIDARG;
    // Constructor requests before XAML runs are applied when it starts.
    if (!xamlRunning) {
        requestedTheme = theme;
        return S_OK;
    }
    auto hr = realRequestedTheme(application, theme);
    // Before content exists, visual roots still carry a refused request.
    if (SUCCEEDED(hr) || !themeApplied) requestedTheme = theme;
    return themeApplied ? hr : S_OK;
}
static HRESULT STDMETHODCALLTYPE GetRequestedTheme(IApplication* application, ApplicationTheme* theme) {
    if (!theme) return E_POINTER;
    if (!requestedTheme) return realGetRequestedTheme(application, theme);
    *theme = *requestedTheme;
    return S_OK;
}
void RaiseIslandSuspending(ABI::Windows::ApplicationModel::ISuspendingEventArgs* args) {
    suspending.InvokeAll(appInstance.Get(), args);
}
static HRESULT CaptureSuspending(IApplication* application) {
    void** table = *reinterpret_cast<void***>(application);
    DWORD protection;
    if (!VirtualProtect(table + 9, 6 * sizeof(void*), PAGE_READWRITE, &protection))
        return HRESULT_FROM_WIN32(GetLastError());
    if (!realRequestedTheme) {
        realRequestedTheme = reinterpret_cast<RequestedThemeCall>(table[10]);
        realGetRequestedTheme = reinterpret_cast<RequestedThemeGetCall>(table[9]);
        InterlockedExchangePointer(table + 10, reinterpret_cast<void*>(&RequestTheme));
        InterlockedExchangePointer(table + 9, reinterpret_cast<void*>(&GetRequestedTheme));
    }
    InterlockedExchangePointer(table + 13, reinterpret_cast<void*>(&AddSuspending));
    InterlockedExchangePointer(table + 14, reinterpret_cast<void*>(&RemoveSuspending));
    DWORD ignored;
    VirtualProtect(table + 9, 6 * sizeof(void*), protection, &ignored);
    return S_OK;
}
// ---- Application factory wrapper ----------------------------------------------
class AppFactory final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, IActivationFactory, IApplicationStatics, IApplicationFactory> {
    ComPtr<IActivationFactory> real_;

public:
    FACTORY_NAME(L"Windows.UI.Xaml.Application")
    explicit AppFactory(IActivationFactory* real) : real_(real) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** v) override { return real_->ActivateInstance(v); }
    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* outer, IInspectable** inner, IApplication** v) override {
        ComPtr<IApplicationFactory> f;
        HRESULT hr = real_.As(&f);
        if (SUCCEEDED(hr)) hr = f->CreateInstance(outer, inner, v);
        appx::Log("TRACE", "STAGE app: real Application composed (outer=%p) hr=0x%08lX", outer, hr);
        if (SUCCEEDED(hr)) {
            appInstance = outer ? outer : *v;
            systemApplication = *v;
            if (!CurrentPackage().universal) requestedTheme = ApplicationTheme_Dark;
            hr = CaptureSuspending(*v);
            if (SUCCEEDED(hr)) hr = PatchOnLaunched();
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_Current(IApplication** v) override {
        ComPtr<IApplicationStatics> s;
        HRESULT hr = real_.As(&s);
        return SUCCEEDED(hr) ? s->get_Current(v) : hr;
    }
    HRESULT STDMETHODCALLTYPE Start(IApplicationInitializationCallback* callback) override {
        return RunIsland(callback);
    }
    HRESULT STDMETHODCALLTYPE LoadComponent(IInspectable* component,
                                            ABI::Windows::Foundation::IUriRuntimeClass* uri) override {
        HSTRING text = nullptr;
        if (uri) uri->get_AbsoluteUri(&text);
        appx::Log("TRACE", "-> LoadComponent %s", text ? Narrow(text).c_str() : "?");
        ComPtr<IApplicationStatics> s;
        HRESULT hr = HookIslandWebViewMetadata(appInstance.Get());
        if (SUCCEEDED(hr)) hr = InitXaml("LoadComponent");
        if (SUCCEEDED(hr)) hr = real_.As(&s);
        if (SUCCEEDED(hr)) hr = LoadApplicationTheme(component, uri, [&] { return s->LoadComponent(component, uri); });
        appx::Log("TRACE", "<- LoadComponent %s hr=0x%08lX", text ? Narrow(text).c_str() : "?", hr);
        WindowsDeleteString(text);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE
    LoadComponentWithResourceLocation(IInspectable* component, ABI::Windows::Foundation::IUriRuntimeClass* uri,
                                      Controls::Primitives::ComponentResourceLocation location) override {
        HSTRING text = nullptr;
        if (uri) uri->get_AbsoluteUri(&text);
        appx::Log("TRACE", "-> LoadComponent %s (location %d)", text ? Narrow(text).c_str() : "?", location);
        ComPtr<IApplicationStatics> s;
        HRESULT hr = HookIslandWebViewMetadata(appInstance.Get());
        if (SUCCEEDED(hr)) hr = InitXaml("LoadComponentWithResourceLocation");
        if (SUCCEEDED(hr)) hr = real_.As(&s);
        if (SUCCEEDED(hr))
            hr = LoadApplicationTheme(component, uri,
                                      [&] { return s->LoadComponentWithResourceLocation(component, uri, location); });
        appx::Log("TRACE", "<- LoadComponent %s hr=0x%08lX", text ? Narrow(text).c_str() : "?", hr);
        WindowsDeleteString(text);
        return hr;
    }
};

HRESULT MakeIslandFactory(const wchar_t* name, IActivationFactory** out) {
    if (wcscmp(name, L"Windows.UI.Xaml.Application") == 0) {
        ComPtr<IActivationFactory> real;
        HRESULT hr = GetReal(name, IID_PPV_ARGS(&real));
        return SUCCEEDED(hr) ? Make<AppFactory>(real.Get()).CopyTo(out) : hr;
    }
    // Wait until the base Application constructor has returned to the CLR:
    // initializing inside CreateInstance reenters an incomplete managed RCW.
    // Controls requested by the derived constructor need XAML immediately.
    if (appInstance && wcsncmp(name, L"Windows.UI.Xaml.", 16) == 0) {
        auto hr = InitXaml("XAML factory");
        if (FAILED(hr)) return hr;
    }
    if (wcscmp(name, L"Windows.UI.Xaml.Window") == 0) return Make<IslandWindowFactory>().CopyTo(out);
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.GridView") == 0) return MakeIslandGridFactory(out);
    if (wcscmp(name, L"Windows.UI.Core.CoreWindow") == 0) return MakeCoreWindowFactory(out);
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.SwapChainBackgroundPanel") == 0) return MakeIslandPanelFactory(out);
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.SwapChainPanel") == 0) return MakeIslandSwapChainPanelFactory(out);
    return CLASS_E_CLASSNOTAVAILABLE;
}
}
