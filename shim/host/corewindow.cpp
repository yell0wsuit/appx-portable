#include "registry.h"
#include "islands/islands.h"
#include "generated/uicore.h"
#include "host.h"
#include "log.h"
#include "event_args.h"
#include <wrl.h>
#include <wrl/event.h>
#include "host/dispatcher.h"
#include "platform/cursor.h"
#include "com/factory.h"
#include "host/pointer.h"
#include <windowsx.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Core;
namespace appx {
class CoreWindow final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultICoreWindow> {
    InspectableClass(L"Windows.UI.Core.CoreWindow", BaseTrust);
    ComPtr<ICoreCursor> cursor_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowActivatedEventArgs>
        eventActivated_;
    EventSource<
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CAutomationProviderRequestedEventArgs>
        eventAutomationProviderRequested_;
    EventSource<
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs>
        eventCharacterReceived_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCoreWindowEventArgs>
        eventClosed_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CInputEnabledEventArgs>
        eventInputEnabled_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs>
        eventKeyDown_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs>
        eventKeyUp_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs>
        eventPointerCaptureLost_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs>
        eventPointerEntered_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs>
        eventPointerExited_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs>
        eventPointerMoved_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs>
        eventPointerPressed_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs>
        eventPointerReleased_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CTouchHitTestingEventArgs>
        eventTouchHitTesting_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs>
        eventPointerWheelChanged_;
    EventSource<
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs>
        eventSizeChanged_;
    EventSource<
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs>
        eventVisibilityChanged_;
    boolean enabled_ = true;
    CoreWindowFlowDirection flow_ = CoreWindowFlowDirection_LeftToRight;

public:
    void OnMessage(UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_ACTIVATE) {
            Log("TRACE", "CoreWindow Activated deliver state=%u", LOWORD(wparam));
            auto args = Make<ActivationArgs>(LOWORD(wparam) == WA_INACTIVE ? CoreWindowActivationState_Deactivated
                                                                           : CoreWindowActivationState_CodeActivated);
            if (args) eventActivated_.InvokeAll(this, args.Get());
        }
        if (message == WM_SIZE) {
            auto args = Make<WindowSizeArgs>(
                ABI::Windows::Foundation::Size{static_cast<float>(LOWORD(lparam)), static_cast<float>(HIWORD(lparam))});
            if (args) eventSizeChanged_.InvokeAll(this, args.Get());
        }
        if (message == WM_SHOWWINDOW) {
            Log("TRACE", "CoreWindow VisibilityChanged deliver visible=%u", static_cast<unsigned>(wparam));
            auto args = Make<VisibilityArgs>(wparam != 0);
            if (args) eventVisibilityChanged_.InvokeAll(this, args.Get());
        }
    }
    void IslandPointer(UINT message, ABI::Windows::UI::Input::IPointerPoint* point,
                       ABI::Windows::System::VirtualKeyModifiers modifiers) {
        auto args = Make<CorePointerArgs>(point, modifiers);
        if (!args) return;
        if (message == WM_LBUTTONDOWN)
            eventPointerPressed_.InvokeAll(this, args.Get());
        else if (message == WM_LBUTTONUP)
            eventPointerReleased_.InvokeAll(this, args.Get());
        else if (message == WM_MOUSEWHEEL)
            eventPointerWheelChanged_.InvokeAll(this, args.Get());
        else
            eventPointerMoved_.InvokeAll(this, args.Get());
    }
    // Mouse and keyboard, raised while the message is dispatched, as a real
    // CoreWindow raises them inside ProcessEvents.
    void OnInput(UINT message, WPARAM wparam, LPARAM lparam) {
        using ABI::Windows::System::VirtualKeyModifiers;
        auto modifiers = [] {
            int keys = 0;
            if (::GetKeyState(VK_CONTROL) & 0x8000) keys |= ABI::Windows::System::VirtualKeyModifiers_Control;
            if (::GetKeyState(VK_MENU) & 0x8000) keys |= ABI::Windows::System::VirtualKeyModifiers_Menu;
            if (::GetKeyState(VK_SHIFT) & 0x8000) keys |= ABI::Windows::System::VirtualKeyModifiers_Shift;
            if ((::GetKeyState(VK_LWIN) | ::GetKeyState(VK_RWIN)) & 0x8000)
                keys |= ABI::Windows::System::VirtualKeyModifiers_Windows;
            return static_cast<VirtualKeyModifiers>(keys);
        };
        switch (message) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL: {
            POINT position{static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
            if (message == WM_MOUSEWHEEL) ScreenToClient(HostWindow(), &position);
            const bool pressed = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN;
            const bool released = message == WM_LBUTTONUP || message == WM_RBUTTONUP || message == WM_MBUTTONUP;
            // A drag keeps reporting outside the window, as with a captured pointer.
            if (pressed) SetCapture(HostWindow());
            if (released && !(wparam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON))) ReleaseCapture();
            ComPtr<ABI::Windows::UI::Input::IPointerPoint> point;
            if (FAILED(MakeMousePoint(position, message == WM_MOUSEWHEEL ? GET_KEYSTATE_WPARAM(wparam) : wparam,
                                      message, &point)))
                return;
            auto args = Make<CorePointerArgs>(point.Get(), modifiers());
            if (!args) return;
            if (pressed)
                eventPointerPressed_.InvokeAll(this, args.Get());
            else if (released)
                eventPointerReleased_.InvokeAll(this, args.Get());
            else if (message == WM_MOUSEMOVE)
                eventPointerMoved_.InvokeAll(this, args.Get());
            else
                eventPointerWheelChanged_.InvokeAll(this, args.Get());
            return;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
            auto args = Make<KeyArgs>(static_cast<ABI::Windows::System::VirtualKey>(wparam), KeyStatus(lparam, up));
            if (!args) return;
            if (up)
                eventKeyUp_.InvokeAll(this, args.Get());
            else
                eventKeyDown_.InvokeAll(this, args.Get());
            return;
        }
        case WM_CHAR: {
            auto args = Make<CharacterArgs>(static_cast<UINT32>(wparam), KeyStatus(lparam, false));
            if (args) eventCharacterReceived_.InvokeAll(this, args.Get());
            return;
        }
        }
    }
    HRESULT STDMETHODCALLTYPE get_Dispatcher(ABI::Windows::UI::Core::ICoreDispatcher** value) override {
        return IslandsMode() ? IslandDispatcher(value) : MakeDispatcher(value);
    }
    HRESULT STDMETHODCALLTYPE add_Activated(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowActivatedEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return eventActivated_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Activated(EventRegistrationToken token) override {
        return eventActivated_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_AutomationProviderRequested(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CAutomationProviderRequestedEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return eventAutomationProviderRequested_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_AutomationProviderRequested(EventRegistrationToken token) override {
        return eventAutomationProviderRequested_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_CharacterReceived(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return eventCharacterReceived_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_CharacterReceived(EventRegistrationToken token) override {
        return eventCharacterReceived_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_Closed(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCoreWindowEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventClosed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Closed(EventRegistrationToken token) override {
        return eventClosed_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_InputEnabled(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CInputEnabledEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventInputEnabled_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_InputEnabled(EventRegistrationToken token) override {
        return eventInputEnabled_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE
    add_KeyDown(__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs* handler,
                EventRegistrationToken* token) override {
        return eventKeyDown_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_KeyDown(EventRegistrationToken token) override {
        return eventKeyDown_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE
    add_KeyUp(__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs* handler,
              EventRegistrationToken* token) override {
        return eventKeyUp_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_KeyUp(EventRegistrationToken token) override { return eventKeyUp_.Remove(token); }
    HRESULT STDMETHODCALLTYPE add_PointerCaptureLost(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventPointerCaptureLost_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerCaptureLost(EventRegistrationToken token) override {
        return eventPointerCaptureLost_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerEntered(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventPointerEntered_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerEntered(EventRegistrationToken token) override {
        return eventPointerEntered_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerExited(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventPointerExited_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerExited(EventRegistrationToken token) override {
        return eventPointerExited_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerMoved(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventPointerMoved_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerMoved(EventRegistrationToken token) override {
        return eventPointerMoved_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerPressed(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventPointerPressed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerPressed(EventRegistrationToken token) override {
        return eventPointerPressed_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerReleased(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventPointerReleased_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerReleased(EventRegistrationToken token) override {
        return eventPointerReleased_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_TouchHitTesting(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CTouchHitTestingEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return eventTouchHitTesting_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_TouchHitTesting(EventRegistrationToken token) override {
        return eventTouchHitTesting_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerWheelChanged(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventPointerWheelChanged_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerWheelChanged(EventRegistrationToken token) override {
        return eventPointerWheelChanged_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_SizeChanged(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return eventSizeChanged_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_SizeChanged(EventRegistrationToken token) override {
        return eventSizeChanged_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_VisibilityChanged(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return eventVisibilityChanged_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_VisibilityChanged(EventRegistrationToken token) override {
        return eventVisibilityChanged_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE get_Bounds(ABI::Windows::Foundation::Rect* value) override {
        if (!value) return E_POINTER;
        RECT r{0, 0, 1024, 768};
        if (HostWindow()) GetClientRect(HostWindow(), &r);
        *value = {0, 0, static_cast<float>(r.right), static_cast<float>(r.bottom)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_PointerCursor(ICoreCursor** value) override {
        if (!value) return E_POINTER;
        return cursor_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_PointerCursor(ICoreCursor* value) override {
        cursor_ = value;
        // No cursor hides the pointer over the game.
        HCURSOR shown = value ? LoadCoreCursor(value) : nullptr;
        CoreCursorType type = CoreCursorType_Arrow;
        UINT32 id = 0;
        if (value) {
            value->get_Type(&type);
            value->get_Id(&id);
        }
        Log("TRACE", "PointerCursor type=%d id=%u cursor=%p", value ? static_cast<int>(type) : -1, id, shown);
        SetHostCursor(shown);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_PointerPosition(ABI::Windows::Foundation::Point* value) override {
        if (!value) return E_POINTER;
        POINT p{};
        GetCursorPos(&p);
        if (HostWindow()) ScreenToClient(HostWindow(), &p);
        *value = {static_cast<float>(p.x), static_cast<float>(p.y)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsInputEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = enabled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsInputEnabled(boolean value) override {
        enabled_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FlowDirection(CoreWindowFlowDirection* value) override {
        if (!value) return E_POINTER;
        *value = flow_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FlowDirection(CoreWindowFlowDirection value) override {
        flow_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Visible(boolean* value) override {
        if (!value) return E_POINTER;
        *value = IsWindowVisible(HostWindow()) && !IsIconic(HostWindow());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Activate() override {
        ActivateHostWindow();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        PostMessageW(HostWindow(), WM_CLOSE, 0, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPointerCapture() override {
        SetCapture(HostWindow());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ReleasePointerCapture() override {
        ReleaseCapture();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAsyncKeyState(ABI::Windows::System::VirtualKey key,
                                               CoreVirtualKeyStates* value) override {
        if (!value) return E_POINTER;
        *value = (::GetAsyncKeyState(static_cast<int>(key)) & 0x8000) ? CoreVirtualKeyStates_Down
                                                                      : CoreVirtualKeyStates_None;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetKeyState(ABI::Windows::System::VirtualKey key, CoreVirtualKeyStates* value) override {
        if (!value) return E_POINTER;
        *value =
            (::GetKeyState(static_cast<int>(key)) & 0x8000) ? CoreVirtualKeyStates_Down : CoreVirtualKeyStates_None;
        return S_OK;
    }
};
static ComPtr<CoreWindow> coreWindow;
HRESULT MakeCoreWindow(ICoreWindow** value) {
    if (!value) return E_POINTER;
    if (!coreWindow) coreWindow = Make<CoreWindow>();
    return coreWindow ? coreWindow.CopyTo(value) : E_OUTOFMEMORY;
}
void IslandPointerEvent(UINT message, ABI::Windows::UI::Input::IPointerPoint* point,
                        ABI::Windows::System::VirtualKeyModifiers modifiers) {
    if (coreWindow) coreWindow->IslandPointer(message, point, modifiers);
}
void IslandInputMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (coreWindow) coreWindow->OnInput(message, wparam, lparam);
}
void CoreHostMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (coreWindow) coreWindow->OnMessage(message, wparam, lparam);
}
// Pointer input uses either CoreWindow or the XAML panel. Keyboard events
// still reach CoreWindow in native XAML games, including UWP DirectX pages.
static bool coreInput = false;
void EnableCoreWindowInput() noexcept { coreInput = true; }
void CoreInputMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    const bool keyboard = message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN ||
                          message == WM_SYSKEYUP || message == WM_CHAR;
    if ((coreInput || keyboard) && coreWindow) coreWindow->OnInput(message, wparam, lparam);
}
// CoreWindow.GetForCurrentThread: the game's window on the UI thread, as on
// Windows, and none on any other thread.
class CoreWindowFactory final : public Factory<ICoreWindowStatic> {
public:
    CoreWindowFactory() : Factory(L"Windows.UI.Core.CoreWindow") {}
    HRESULT STDMETHODCALLTYPE GetForCurrentThread(ICoreWindow** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!HostThread() || GetCurrentThreadId() != HostThread()) return S_OK;
        return MakeCoreWindow(value);
    }
};
HRESULT MakeCoreWindowFactory(IActivationFactory** value) {
    static auto factory = Make<CoreWindowFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
const ShimClass coreWindowClass{L"Windows.UI.Core.CoreWindow", MakeCoreWindowFactory};
}
