#include "dispatcher.h"
#include "host.h"
#include "log.h"
#include <atomic>
#include <cstdio>
#include <memory>
#include "com/async_action.h"
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
class Dispatcher final : public RuntimeClass<RuntimeClassFlags<WinRt>, ICoreDispatcher, FtmBase> {
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

public:
    explicit Dispatcher(DWORD thread = 0) : thread_(thread) {}
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
        auto hr = Post(priority, [action, callback] {
            ABI::Windows::Foundation::AsyncStatus status;
            action->get_Status(&status);
            if (status == ABI::Windows::Foundation::AsyncStatus::Started) action->Finish(callback->Invoke());
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
HRESULT MakeDispatcher(ICoreDispatcher** value) {
    if (!value) return E_POINTER;
    static auto dispatcher = Make<Dispatcher>();
    return dispatcher ? dispatcher.CopyTo(value) : E_OUTOFMEMORY;
}
HRESULT MakeThreadDispatcher(ICoreDispatcher** value) {
    if (!value) return E_POINTER;
    MSG message{};
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    auto dispatcher = Make<Dispatcher>(GetCurrentThreadId());
    return dispatcher ? dispatcher.CopyTo(value) : E_OUTOFMEMORY;
}
}
