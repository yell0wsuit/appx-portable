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
#include <string>
#include <vector>
#include <map>
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
static ComPtr<IUIElement> pendingContent;
static ComPtr<ABI::Windows::UI::Core::ICoreDispatcher> islandDispatcher;
using RequestedThemeCall = HRESULT(STDMETHODCALLTYPE*)(IApplication*, ApplicationTheme);
static RequestedThemeCall realRequestedTheme;
static ApplicationTheme requestedTheme = ApplicationTheme_Dark;
static bool themeApplied;
static void ReadApplicationTheme() {
    // The native parser sets Application properties internally, bypassing COM
    // setters. Read an authored theme with the SDK XML reader before startup;
    // subsequent constructor requests can still override it.
    const auto path = CurrentPackage().directory / L"App.xaml";
    ComPtr<IStream> stream;
    ComPtr<IXmlReader> reader;
    if (FAILED(SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &stream)) ||
        FAILED(CreateXmlReader(IID_PPV_ARGS(&reader), nullptr)) ||
        FAILED(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit)) ||
        FAILED(reader->SetInput(stream.Get())))
        return;
    XmlNodeType type;
    while (reader->Read(&type) == S_OK) {
        if (type != XmlNodeType_Element) continue;
        const wchar_t* name = nullptr;
        reader->GetLocalName(&name, nullptr);
        if (name && !wcscmp(name, L"Application") &&
            reader->MoveToAttributeByName(L"RequestedTheme", nullptr) == S_OK) {
            const wchar_t* value = nullptr;
            reader->GetValue(&value, nullptr);
            if (value && !wcscmp(value, L"Light")) requestedTheme = ApplicationTheme_Light;
            if (value && !wcscmp(value, L"Dark")) requestedTheme = ApplicationTheme_Dark;
        }
        break;
    }
}
// Windows 8 defaulted to dark. Modern islands inherit the desktop preference.
// Apply the compatibility default to each visual root without replacing an
// element's explicit theme or changing the system Application after resources load.
static HRESULT ApplyRootTheme(IInspectable* element) {
    if (!element) return S_OK;
    ComPtr<IFrameworkElement2> root;
    auto hr = element->QueryInterface(IID_PPV_ARGS(&root));
    if (hr == E_NOINTERFACE) return S_OK;
    if (FAILED(hr)) return hr;
    ElementTheme theme;
    hr = root->get_RequestedTheme(&theme);
    if (SUCCEEDED(hr) && theme == ElementTheme_Default)
        hr = root->put_RequestedTheme(requestedTheme == ApplicationTheme_Dark ? ElementTheme_Dark : ElementTheme_Light);
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
        HRESULT hr = S_OK;
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
    // Initializing XAML for controls in derived Application constructors can
    // launch the app synchronously.
    // Let the managed startup callback finish before entering game code.
    if (!applicationConstructed) {
        launchPending = true;
        return S_OK;
    }
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
void ResizeIslandView(int width, int height) {
    if (islandWindow) SetWindowPos(islandWindow, HWND_TOP, 0, 0, width, height, SWP_NOACTIVATE);
}
void IslandInputMessage(UINT message, WPARAM wparam, LPARAM lparam);
bool IslandPreTranslate(MSG* message) {
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
    if (!themeApplied) {
        requestedTheme = theme;
        return S_OK;
    }
    auto hr = realRequestedTheme(application, theme);
    if (SUCCEEDED(hr)) requestedTheme = theme;
    return hr;
}
static HRESULT STDMETHODCALLTYPE GetRequestedTheme(IApplication*, ApplicationTheme* theme) {
    if (!theme) return E_POINTER;
    *theme = requestedTheme;
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
            ReadApplicationTheme();
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
        HRESULT hr = InitXaml("LoadComponent");
        if (SUCCEEDED(hr)) hr = real_.As(&s);
        if (SUCCEEDED(hr)) hr = s->LoadComponent(component, uri);
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
        HRESULT hr = InitXaml("LoadComponentWithResourceLocation");
        if (SUCCEEDED(hr)) hr = real_.As(&s);
        if (SUCCEEDED(hr)) hr = s->LoadComponentWithResourceLocation(component, uri, location);
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
    if (wcscmp(name, L"Windows.UI.Core.CoreWindow") == 0) return MakeCoreWindowFactory(out);
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.SwapChainBackgroundPanel") == 0) return MakeIslandPanelFactory(out);
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.SwapChainPanel") == 0) return MakeIslandSwapChainPanelFactory(out);
    return CLASS_E_CLASSNOTAVAILABLE;
}
}
