#include <windowsx.h>
#include <algorithm>
#include "input.h"
#include "generated/win10_input.h"
#include "host/dispatcher.h"
#include "host/event_args.h"
#include "host/host.h"
#include "host/pointer.h"
#include "platform/cursor.h"
#include <wrl/event.h>
#include <mutex>
#include <vector>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Core;
namespace appx {
namespace {
std::mutex windowsMutex;
std::vector<HWND> inputWindows;
class InputSource final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultICoreInputSourceBase,
                                              DefaultICorePointerInputSource, FtmBase> {
    APPX_LOG_MISSING_INTERFACES("CoreIndependentInputSource")
    InspectableClass(L"Windows.UI.Core.CoreIndependentInputSource", BaseTrust);
    using PointerHandler = ABI::Windows::Foundation::ITypedEventHandler<IInspectable*, PointerEventArgs*>;
    EventSource<PointerHandler> pressed_, released_, moved_, entered_, exited_, wheel_, captureLost_;
    EventSource<ABI::Windows::Foundation::ITypedEventHandler<IInspectable*, InputEnabledEventArgs*>> enabledChanged_;
    ComPtr<ICoreDispatcher> dispatcher_;
    ComPtr<ICoreCursor> cursor_;
    std::atomic<boolean> enabled_{true};
    HWND window_{};
    POINT lastPosition_{};
    WPARAM lastButtons_{};
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<InputSource*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<InputSource*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && message >= WM_APP && message < WM_APP + 0x400) {
            self->Deliver(message - WM_APP, wparam, lparam);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    void Deliver(UINT message, WPARAM buttons, LPARAM position) {
        if (!enabled_) return;
        POINT point{GET_X_LPARAM(position), GET_Y_LPARAM(position)};
        if (message == WM_CAPTURECHANGED) {
            point = lastPosition_;
            buttons = lastButtons_;
        } else {
            lastPosition_ = point;
            lastButtons_ = buttons;
        }
        ComPtr<ABI::Windows::UI::Input::IPointerPoint> mouse;
        if (FAILED(MakeMousePoint(point, buttons, message, &mouse))) return;
        auto modifiers = static_cast<ABI::Windows::System::VirtualKeyModifiers>(
            ((buttons & MK_SHIFT) ? ABI::Windows::System::VirtualKeyModifiers_Shift : 0) |
            ((buttons & MK_CONTROL) ? ABI::Windows::System::VirtualKeyModifiers_Control : 0));
        auto args = Make<CorePointerArgs>(mouse.Get(), modifiers);
        if (!args) return;
        auto* sender = static_cast<ICoreInputSourceBase*>(this);
        HRESULT hr = S_OK;
        if (message == WM_LBUTTONDOWN) hr = pressed_.InvokeAll(sender, args.Get());
        if (message == WM_LBUTTONUP) hr = released_.InvokeAll(sender, args.Get());
        if (message == WM_MOUSEMOVE) hr = moved_.InvokeAll(sender, args.Get());
        if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) hr = wheel_.InvokeAll(sender, args.Get());
        if (message == WM_CAPTURECHANGED) hr = captureLost_.InvokeAll(sender, args.Get());
        if (FAILED(hr)) Log("FAIL", "independent input handler hr=%08lx", hr);
    }

public:
    HRESULT Initialize() {
        auto hr = MakeThreadDispatcher(&dispatcher_);
        if (FAILED(hr)) return hr;
        WNDCLASSW cls{};
        cls.lpfnWndProc = WindowProc;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"AppxPortableIndependentInput";
        if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return HRESULT_FROM_WIN32(GetLastError());
        window_ = CreateWindowW(cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, cls.hInstance, this);
        if (!window_) return HRESULT_FROM_WIN32(GetLastError());
        std::lock_guard<std::mutex> lock(windowsMutex);
        inputWindows.push_back(window_);
        return S_OK;
    }
    ~InputSource() {
        std::lock_guard<std::mutex> lock(windowsMutex);
        inputWindows.erase(std::remove(inputWindows.begin(), inputWindows.end(), window_), inputWindows.end());
        if (window_) DestroyWindow(window_);
    }
    HRESULT STDMETHODCALLTYPE get_Dispatcher(ICoreDispatcher** value) override {
        if (!value) return E_POINTER;
        return dispatcher_.CopyTo(value);
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
    HRESULT STDMETHODCALLTYPE get_PointerCursor(ICoreCursor** value) override {
        if (!value) return E_POINTER;
        return cursor_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_PointerCursor(ICoreCursor* value) override {
        cursor_ = value;
        SetHostCursor(value ? LoadCoreCursor(value) : nullptr);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPointerCapture() override {
        return PostHostWork(0, [] { SetCapture(HostWindow()); });
    }
    HRESULT STDMETHODCALLTYPE ReleasePointerCapture() override {
        return PostHostWork(0, [] {
            if (GetCapture() == HostWindow()) ReleaseCapture();
        });
    }
    HRESULT STDMETHODCALLTYPE get_HasCapture(boolean* value) override {
        if (!value) return E_POINTER;
        GUITHREADINFO gui{sizeof(gui)};
        *value = HostThread() && GetGUIThreadInfo(HostThread(), &gui) && gui.hwndCapture == HostWindow();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_PointerPosition(ABI::Windows::Foundation::Point* value) override {
        if (!value) return E_POINTER;
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(HostWindow(), &point);
        *value = {static_cast<float>(point.x), static_cast<float>(point.y)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE
    add_InputEnabled(__FITypedEventHandler_2_IInspectable_Windows__CUI__CCore__CInputEnabledEventArgs* h,
                     EventRegistrationToken* t) override {
        return enabledChanged_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_InputEnabled(EventRegistrationToken t) override {
        return enabledChanged_.Remove(t);
    }
#define INPUT_EVENT(Name, Field)                                                                                       \
    HRESULT STDMETHODCALLTYPE add_##Name(PointerHandler* h, EventRegistrationToken* t) override {                      \
        return Field.Add(h, t);                                                                                        \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE remove_##Name(EventRegistrationToken t) override { return Field.Remove(t); }
    INPUT_EVENT(PointerPressed, pressed_)
    INPUT_EVENT(PointerReleased, released_)
    INPUT_EVENT(PointerMoved, moved_)
    INPUT_EVENT(PointerEntered, entered_)
    INPUT_EVENT(PointerExited, exited_)
    INPUT_EVENT(PointerWheelChanged, wheel_)
    INPUT_EVENT(PointerCaptureLost, captureLost_)
#undef INPUT_EVENT
};
}
HRESULT MakeIndependentInputSource(CoreInputDeviceTypes devices, ICoreInputSourceBase** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    if (!devices) return E_INVALIDARG;
    auto source = Make<InputSource>();
    if (!source) return E_OUTOFMEMORY;
    auto hr = source->Initialize();
    return FAILED(hr) ? hr : source.CopyTo(value);
}
void IndependentInputPanelMessage(UINT message, WPARAM buttons, LPARAM position) {
    std::lock_guard<std::mutex> lock(windowsMutex);
    for (HWND window : inputWindows) PostMessageW(window, WM_APP + message, buttons, position);
}
void IndependentInputHostMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message != WM_LBUTTONDOWN && message != WM_LBUTTONUP && message != WM_MOUSEMOVE && message != WM_MOUSEWHEEL &&
        message != WM_MOUSEHWHEEL && message != WM_CAPTURECHANGED)
        return;
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
        POINT position{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(HostWindow(), &position);
        lparam = MAKELPARAM(position.x, position.y);
    }
    bool sources = false;
    {
        std::lock_guard<std::mutex> lock(windowsMutex);
        sources = !inputWindows.empty();
        for (HWND window : inputWindows) PostMessageW(window, WM_APP + message, wparam, lparam);
    }
    // Capture belongs to the host's GUI thread. Keep drag events coming even
    // when the pointer leaves the client area; release after the button-up.
    if (sources && HostWindow()) {
        if (message == WM_LBUTTONDOWN) SetCapture(HostWindow());
        if (message == WM_LBUTTONUP && GetCapture() == HostWindow()) ReleaseCapture();
    }
}
}
