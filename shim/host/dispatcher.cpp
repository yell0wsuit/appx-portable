#include "dispatcher.h"
#include "host.h"
#include "log.h"
#include <atomic>
#include <cstdio>
#include <memory>
#include "com/async_action.h"
#include "event_args.h"
#include <wrl/event.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Core;
namespace appx {
class IdleArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>, IIdleDispatchedHandlerArgs> {
    InspectableClass(L"Windows.UI.Core.IdleDispatchedHandlerArgs", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE get_IsDispatcherIdle(boolean* value) override {
        if (!value) return E_POINTER;
        *value = true;
        return S_OK;
    }
};
using AcceleratorHandler =
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreDispatcher_Windows__CUI__CCore__CAcceleratorKeyEventArgs;
class Dispatcher final : public RuntimeClass<RuntimeClassFlags<WinRt>, ICoreDispatcher, ICoreAcceleratorKeys, FtmBase> {
    APPX_LOG_MISSING_INTERFACES("CoreDispatcher")
    InspectableClass(L"Windows.UI.Core.CoreDispatcher", BaseTrust);
    DWORD thread_;
    DWORD Owner() const { return thread_ ? thread_ : HostThread(); }
    static constexpr UINT WorkMessage = WM_APP + 0x42;
    HRESULT Post(int priority, std::function<void()> work) {
        if (!thread_) return PostHostWork(priority, std::move(work));
        auto task = std::make_unique<std::function<void()>>(std::move(work));
        if (!PostThreadMessageW(thread_, WorkMessage, reinterpret_cast<WPARAM>(task.get()), 0))
            return HRESULT_FROM_WIN32(GetLastError());
        task.release();
        return S_OK;
    }
    void Dispatch(MSG& message) {
        if (thread_ && !message.hwnd && message.message == WorkMessage) {
            std::unique_ptr<std::function<void()>> task(reinterpret_cast<std::function<void()>*>(message.wParam));
            (*task)();
        } else {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    EventSource<AcceleratorHandler> accelerators_;

public:
    explicit Dispatcher(DWORD thread = 0) : thread_(thread) {}
    HRESULT STDMETHODCALLTYPE add_AcceleratorKeyActivated(AcceleratorHandler* handler, EventRegistrationToken* token) override {
        return accelerators_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_AcceleratorKeyActivated(EventRegistrationToken token) override {
        return accelerators_.Remove(token);
    }
    // True when a handler marked the key handled; CoreWindow then does not see it.
    bool RaiseAccelerator(CoreAcceleratorKeyEventType type, WPARAM key, LPARAM lparam, bool released) {
        if (!accelerators_.GetSize()) return false;
        auto args = Make<AcceleratorArgs>(type, static_cast<ABI::Windows::System::VirtualKey>(key), KeyStatus(lparam, released));
        if (!args) return false;
        auto hr = accelerators_.InvokeAll(this, args.Get());
        if (FAILED(hr)) Log("FAIL", "AcceleratorKeyActivated handler hr=%08lx", hr);
        boolean handled = false;
        args->get_Handled(&handled);
        return handled != 0;
    }
    HRESULT STDMETHODCALLTYPE get_HasThreadAccess(boolean* value) override {
        if (!value) return E_POINTER;
        *value = Owner() == GetCurrentThreadId();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ProcessEvents(CoreProcessEventsOption options) override {
        if (Owner() != GetCurrentThreadId()) return RPC_E_WRONG_THREAD;
        static unsigned calls = 0;
        if (++calls % 30 == 0) {
            char after[48];
            snprintf(after, sizeof(after), "CoreDispatcher.ProcessEvents call %u", calls);
            CheckHeaps(after);
        }
        MSG message{};
        if (options == CoreProcessEventsOption_ProcessUntilQuit) {
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                Dispatch(message);
            }
        } else {
            // ProcessOneAndAllPending sleeps until the first event, as on
            // Windows: game loops call it while hidden instead of drawing.
            if (options == CoreProcessEventsOption_ProcessOneAndAllPending)
                MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_ALERTABLE | MWMO_INPUTAVAILABLE);
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    PostQuitMessage(static_cast<int>(message.wParam));
                    break;
                }
                Dispatch(message);
                if (options == CoreProcessEventsOption_ProcessOneIfPresent) break;
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RunAsync(CoreDispatcherPriority priority, IDispatchedHandler* handler,
                                       ABI::Windows::Foundation::IAsyncAction** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!handler || priority < CoreDispatcherPriority_Idle || priority > CoreDispatcherPriority_High)
            return E_INVALIDARG;
        auto action = Make<AsyncAction>();
        if (!action) return E_OUTOFMEMORY;
        ComPtr<IDispatchedHandler> callback(handler);
        static std::atomic<unsigned> posted{0};
        const unsigned count = ++posted;
        if (count <= 5 || count % 500 == 0) Log("TRACE", "CoreDispatcher RunAsync %u priority=%d", count, priority);
        auto hr = Post(priority, [action, callback, count] {
            ABI::Windows::Foundation::AsyncStatus status;
            action->get_Status(&status);
            if (status != ABI::Windows::Foundation::AsyncStatus::Started) return;
            auto result = callback->Invoke();
            if (count <= 5 || FAILED(result)) Log("TRACE", "CoreDispatcher RunAsync %u ran hr=%08lx", count, result);
            action->Finish(result);
        });
        return FAILED(hr) ? hr : action.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE RunIdleAsync(IIdleDispatchedHandler* handler,
                                           ABI::Windows::Foundation::IAsyncAction** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!handler) return E_INVALIDARG;
        auto action = Make<AsyncAction>();
        if (!action) return E_OUTOFMEMORY;
        ComPtr<IIdleDispatchedHandler> callback(handler);
        auto hr = Post(CoreDispatcherPriority_Idle, [action, callback] {
            auto args = Make<IdleArgs>();
            action->Finish(args ? callback->Invoke(args.Get()) : E_OUTOFMEMORY);
        });
        return FAILED(hr) ? hr : action.CopyTo(value);
    }
};
static ComPtr<Dispatcher>& HostDispatcher() {
    static auto dispatcher = Make<Dispatcher>();
    return dispatcher;
}
HRESULT MakeDispatcher(ICoreDispatcher** value) {
    if (!value) return E_POINTER;
    auto& dispatcher = HostDispatcher();
    return dispatcher ? dispatcher.CopyTo(value) : E_OUTOFMEMORY;
}
bool AcceleratorKeyMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    CoreAcceleratorKeyEventType type;
    switch (message) {
    case WM_KEYDOWN: type = CoreAcceleratorKeyEventType_KeyDown; break;
    case WM_KEYUP: type = CoreAcceleratorKeyEventType_KeyUp; break;
    case WM_SYSKEYDOWN: type = CoreAcceleratorKeyEventType_SystemKeyDown; break;
    case WM_SYSKEYUP: type = CoreAcceleratorKeyEventType_SystemKeyUp; break;
    case WM_CHAR: type = CoreAcceleratorKeyEventType_Character; break;
    case WM_SYSCHAR: type = CoreAcceleratorKeyEventType_SystemCharacter; break;
    default: return false;
    }
    auto& dispatcher = HostDispatcher();
    return dispatcher && dispatcher->RaiseAccelerator(type, wparam, lparam, message == WM_KEYUP || message == WM_SYSKEYUP);
}
HRESULT MakeThreadDispatcher(ICoreDispatcher** value) {
    if (!value) return E_POINTER;
    MSG message{};
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    auto dispatcher = Make<Dispatcher>(GetCurrentThreadId());
    return dispatcher ? dispatcher.CopyTo(value) : E_OUTOFMEMORY;
}
}
