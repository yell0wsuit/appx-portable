// Windows.System.Threading.Core.SignalNotifier for Wine, which has none. Where's
// My Mickey's FMOD wakes its mixer through one on a named event, and stopped
// once it had an audio device to mix for. Each Enable calls the handler once,
// from the thread pool, when the event or semaphore is signaled or the wait
// times out, as Windows does.
#include "com/factory.h"
#include "registry.h"
#include <windows.system.threading.core.h>
#include <wrl.h>
#include <mutex>
using namespace Microsoft::WRL;
using namespace ABI::Windows::System::Threading::Core;
namespace appx {
namespace {
class Notifier;
thread_local Notifier* running = nullptr;
// Referenced by its armed wait as well as by the game, which may drop its own
// reference once the handler has run, without Terminate.
class Notifier final : public RuntimeClass<RuntimeClassFlags<WinRt>, ISignalNotifier, FtmBase> {
    InspectableClass(L"Windows.System.Threading.Core.SignalNotifier", BaseTrust);
    const HANDLE handle_;
    const ComPtr<ISignalHandler> handler_;
    const DWORD timeout_;
    std::mutex mutex_;
    HANDLE wait_ = nullptr;
    // Whether wait_ still holds its reference: whoever clears it owns that reference.
    bool armed_ = false, terminated_ = false;

    static void CALLBACK Fire(void* context, BOOLEAN timedOut) {
        auto* raw = static_cast<Notifier*>(context);
        {
            std::lock_guard<std::mutex> lock(raw->mutex_);
            // Enable or Terminate disarmed it first, and waits for this call to return.
            if (!raw->armed_) return;
            raw->armed_ = false;
        }
        ComPtr<Notifier> self;
        self.Attach(raw);
        Notifier* outer = running;
        running = raw;
        const HRESULT hr = raw->handler_->Invoke(raw, timedOut);
        running = outer;
        if (FAILED(hr)) Log("FAIL", "SignalNotifier handler hr=%08lx", hr);
    }
    // Cancels the current wait. Handlers often re-enable or terminate their own
    // notifier, where waiting for the call to finish would deadlock; elsewhere,
    // wait out a running call, which may still be using this notifier.
    void Disarm() {
        HANDLE wait;
        bool owned;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            wait = wait_;
            owned = armed_;
            wait_ = nullptr;
            armed_ = false;
        }
        if (wait) running == this ? UnregisterWait(wait) : UnregisterWaitEx(wait, INVALID_HANDLE_VALUE);
        if (owned) Release();
    }

public:
    Notifier(HANDLE handle, ISignalHandler* handler, DWORD timeout)
        : handle_(handle), handler_(handler), timeout_(timeout) {}
    ~Notifier() override {
        if (wait_) UnregisterWait(wait_); // a one-shot wait stays registered after it fires
        CloseHandle(handle_);
    }
    HRESULT STDMETHODCALLTYPE Enable() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (terminated_) return E_ILLEGAL_METHOD_CALL;
        }
        Disarm();
        AddRef();
        HANDLE wait = nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!RegisterWaitForSingleObject(&wait, handle_, Fire, this, timeout_, WT_EXECUTEONLYONCE)) {
            const DWORD error = GetLastError();
            Release();
            return HRESULT_FROM_WIN32(error);
        }
        wait_ = wait;
        armed_ = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Terminate() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            terminated_ = true;
        }
        Disarm();
        return S_OK;
    }
};
class SignalNotifierFactory final : public Factory<ISignalNotifierStatics> {
    static HRESULT Attach(HSTRING name, HANDLE handle, ISignalHandler* handler, DWORD timeout,
                          ISignalNotifier** notifier) {
        if (!handle) {
            const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            Log("FAIL", "SignalNotifier: cannot open %ls hr=%08lx", WindowsGetStringRawBuffer(name, nullptr), hr);
            return hr;
        }
        auto made = Make<Notifier>(handle, handler, timeout);
        if (!made) {
            CloseHandle(handle);
            return E_OUTOFMEMORY;
        }
        return made.CopyTo(notifier);
    }
    // A zero timeout waits for the signal: FMOD's WASAPI feeder attaches so and
    // waits on the device's event.
    static DWORD Milliseconds(ABI::Windows::Foundation::TimeSpan timeout) {
        return timeout.Duration <= 0 ? INFINITE : static_cast<DWORD>((timeout.Duration + 9999) / 10000);
    }
    static HRESULT Check(HSTRING name, ISignalHandler* handler, ISignalNotifier** notifier) {
        if (!notifier) return E_POINTER;
        *notifier = nullptr;
        return name && handler ? S_OK : E_INVALIDARG;
    }

public:
    SignalNotifierFactory() : Factory(L"Windows.System.Threading.Core.SignalNotifier") {}
    HRESULT STDMETHODCALLTYPE AttachToEvent(HSTRING name, ISignalHandler* handler,
                                            ISignalNotifier** notifier) override {
        HRESULT hr = Check(name, handler, notifier);
        if (FAILED(hr)) return hr;
        return Attach(name, OpenEventW(SYNCHRONIZE, FALSE, WindowsGetStringRawBuffer(name, nullptr)), handler, INFINITE,
                      notifier);
    }
    HRESULT STDMETHODCALLTYPE AttachToEventWithTimeout(HSTRING name, ISignalHandler* handler,
                                                       ABI::Windows::Foundation::TimeSpan timeout,
                                                       ISignalNotifier** notifier) override {
        HRESULT hr = Check(name, handler, notifier);
        if (FAILED(hr)) return hr;
        return Attach(name, OpenEventW(SYNCHRONIZE, FALSE, WindowsGetStringRawBuffer(name, nullptr)), handler,
                      Milliseconds(timeout), notifier);
    }
    HRESULT STDMETHODCALLTYPE AttachToSemaphore(HSTRING name, ISignalHandler* handler,
                                                ISignalNotifier** notifier) override {
        HRESULT hr = Check(name, handler, notifier);
        if (FAILED(hr)) return hr;
        return Attach(name, OpenSemaphoreW(SYNCHRONIZE, FALSE, WindowsGetStringRawBuffer(name, nullptr)), handler,
                      INFINITE, notifier);
    }
    HRESULT STDMETHODCALLTYPE AttachToSemaphoreWithTimeout(HSTRING name, ISignalHandler* handler,
                                                           ABI::Windows::Foundation::TimeSpan timeout,
                                                           ISignalNotifier** notifier) override {
        HRESULT hr = Check(name, handler, notifier);
        if (FAILED(hr)) return hr;
        return Attach(name, OpenSemaphoreW(SYNCHRONIZE, FALSE, WindowsGetStringRawBuffer(name, nullptr)), handler,
                      Milliseconds(timeout), notifier);
    }
};
HRESULT MakeSignalNotifierFactory(IActivationFactory** value) {
    auto factory = Make<SignalNotifierFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
const ShimClass signalNotifierClass{L"Windows.System.Threading.Core.SignalNotifier", MakeSignalNotifierFactory,
                                    ShimPolicy::FillMissing};
}
