#include "generated/xaml.h"
#include "com/factory.h"
#include "host/host.h"
#include "host/pointer.h"
#include <windows.ui.xaml.media.dxinterop.h>
#include "host/event_args.h"
#include "registry.h"
#include <wrl/event.h>
#include "host/dispatcher.h"
#include "xaml/key.h"
#include <windows.ui.xaml.controls.h>
#include <vector>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
namespace appx {
HRESULT QueueElementLoaded(IInspectable* value);
HRESULT DeliverElementLoaded(IInspectable* value);
HRESULT RaiseElementSizeChanged();
HRESULT MakeCoreWindow(ABI::Windows::UI::Core::ICoreWindow** value);
class Window final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIWindow, IWindow2> {
    APPX_LOG_MISSING_INTERFACES("Window")
    InspectableClass(L"Windows.UI.Xaml.Window", BaseTrust);
    ComPtr<IUIElement> content_;
    ComPtr<IUIElement> titleBar_;
    EventSource<IWindowSizeChangedEventHandler> resized_;
    EventSource<IWindowActivatedEventHandler> activated_;
    EventSource<IWindowClosedEventHandler> closed_;
    EventSource<IWindowVisibilityChangedEventHandler> visibility_;

public:
    HRESULT STDMETHODCALLTYPE SetTitleBar(IUIElement* value) override {
        // Retain the custom drag surface; the desktop host owns caption dragging.
        titleBar_ = value;
        return S_OK;
    }
    void OnMessage(UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_ACTIVATE) {
            auto args = Make<ActivationArgs>(LOWORD(wparam) == WA_INACTIVE
                                                 ? ABI::Windows::UI::Core::CoreWindowActivationState_Deactivated
                                                 : ABI::Windows::UI::Core::CoreWindowActivationState_CodeActivated);
            if (args) activated_.InvokeAll(static_cast<IWindow*>(this), args.Get());
        }
        if (message == WM_SIZE) {
            auto args = Make<WindowSizeArgs>(
                ABI::Windows::Foundation::Size{static_cast<float>(LOWORD(lparam)), static_cast<float>(HIWORD(lparam))});
            if (args) resized_.InvokeAll(static_cast<IWindow*>(this), args.Get());
            auto hr = RaiseElementSizeChanged();
            if (FAILED(hr)) Log("FAIL", "SizeChanged handler hr=%08lx", hr);
        }
        if (message == WM_SHOWWINDOW) {
            auto args = Make<VisibilityArgs>(wparam != 0);
            if (args) visibility_.InvokeAll(static_cast<IWindow*>(this), args.Get());
        }
    }
    HRESULT STDMETHODCALLTYPE get_Dispatcher(ABI::Windows::UI::Core::ICoreDispatcher** value) override {
        return MakeDispatcher(value);
    }
    HRESULT STDMETHODCALLTYPE get_CoreWindow(ABI::Windows::UI::Core::ICoreWindow** value) override {
        return MakeCoreWindow(value);
    }
    HRESULT STDMETHODCALLTYPE get_Bounds(ABI::Windows::Foundation::Rect* value) override {
        if (!value) return E_POINTER;
        RECT r{0, 0, 1024, 768};
        if (HostWindow()) GetClientRect(HostWindow(), &r);
        *value = {0, 0, static_cast<float>(r.right), static_cast<float>(r.bottom)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Visible(boolean* value) override {
        if (!value) return E_POINTER;
        *value = IsWindowVisible(HostWindow()) != FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Content(IUIElement** value) override {
        if (!value) return E_POINTER;
        return content_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Content(IUIElement* value) override {
        SetPointerTarget(value);
        content_ = value;
        return S_OK;
    }
#define WINDOW_EVENT(Name, Handler, field)                                                                             \
    HRESULT STDMETHODCALLTYPE add_##Name(Handler* handler, EventRegistrationToken* token) override {                   \
        return field.Add(handler, token);                                                                              \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE remove_##Name(EventRegistrationToken token) override { return field.Remove(token); }
    WINDOW_EVENT(SizeChanged, IWindowSizeChangedEventHandler, resized_)
    WINDOW_EVENT(Activated, IWindowActivatedEventHandler, activated_)
    WINDOW_EVENT(Closed, IWindowClosedEventHandler, closed_)
    WINDOW_EVENT(VisibilityChanged, IWindowVisibilityChangedEventHandler, visibility_)
#undef WINDOW_EVENT
    HRESULT STDMETHODCALLTYPE Activate() override {
        if (!HostWindow()) return E_ILLEGAL_METHOD_CALL;
        Log("TRACE", "Window Activate begin");
        ActivateHostWindow();
        UpdateWindow(HostWindow());
        Log("TRACE", "Window Activate returned");
        return QueueElementLoaded(content_.Get());
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        PostMessageW(HostWindow(), WM_CLOSE, 0, 0);
        return S_OK;
    }
};
static ComPtr<Window> xamlWindow;
void XamlInputMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    const bool released = message == WM_KEYUP || message == WM_SYSKEYUP;
    if (!released && message != WM_KEYDOWN && message != WM_SYSKEYDOWN) return;
    if (!xamlWindow) return;
    ComPtr<IUIElement> content;
    if (FAILED(xamlWindow->get_Content(&content)) || !content) return;
    // The shim's DirectX views use a content chain ending at a graphics panel.
    // Snapshot and retain the route before invoking handlers, which can replace it.
    std::vector<ComPtr<IInspectable>> route;
    ComPtr<IInspectable> current;
    content.As(&current);
    while (current && route.size() < 64) {
        ComPtr<IUnknown> identity;
        current.As(&identity);
        for (auto& element : route) {
            ComPtr<IUnknown> previous;
            element.As(&previous);
            if (previous == identity) return; // Malformed cyclic content.
        }
        route.push_back(current);
        ComPtr<IInspectable> child;
        ComPtr<Controls::IContentControl> container;
        ComPtr<Controls::IUserControl> userControl;
        ComPtr<IPointerTargetProvider> panel;
        if (SUCCEEDED(current.As(&container))) container->get_Content(&child);
        if (!child && SUCCEEDED(current.As(&userControl))) {
            ComPtr<IUIElement> element;
            userControl->get_Content(&element);
            if (element) element.As(&child);
        }
        if (!child && SUCCEEDED(current.As(&panel))) panel->GetPointerTarget(&child);
        current = child;
    }
    auto args = Make<XamlKeyArgs>(route.back().Get(), wparam, lparam, released);
    if (!args) return;
    for (auto element = route.rbegin(); element != route.rend(); ++element) {
        ComPtr<IXamlKeyTarget> target;
        if (SUCCEEDED(element->As(&target))) {
            const auto hr = target->RaiseKey(released, args.Get());
            if (FAILED(hr)) {
                Log("FAIL", "XAML key handler hr=%08lx", hr);
                return;
            }
        }
        boolean handled = false;
        args->get_Handled(&handled);
        if (handled) break;
    }
}
bool HasXamlWindow() { return xamlWindow != nullptr; }
HRESULT DeliverWindowLoaded() {
    if (!xamlWindow || !IsWindowVisible(HostWindow())) return S_OK;
    ComPtr<IUIElement> content;
    auto hr = xamlWindow->get_Content(&content);
    return FAILED(hr) ? hr : DeliverElementLoaded(content.Get());
}
void XamlHostMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (xamlWindow) xamlWindow->OnMessage(message, wparam, lparam);
}
class WindowFactory final : public Factory<DefaultIWindowStatics> {

public:
    WindowFactory() : Factory(L"Windows.UI.Xaml.Window") {}
    HRESULT STDMETHODCALLTYPE get_Current(IWindow** value) override {
        if (!value) return E_POINTER;
        if (!xamlWindow) xamlWindow = Make<Window>();
        return xamlWindow ? xamlWindow.CopyTo(value) : E_OUTOFMEMORY;
    }
};
HRESULT MakeWindowFactory(IActivationFactory** value) {
    static auto factory = Make<WindowFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
const ShimClass windowClass{L"Windows.UI.Xaml.Window", MakeWindowFactory};
}
