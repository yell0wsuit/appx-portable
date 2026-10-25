#pragma once
#include "async_id.h"
#include "log.h"
#include "host/host.h"
#include <windows.foundation.h>
#include <wrl.h>
#include <functional>
#include <mutex>
namespace appx {
template <class T, class TResult = T>
class CompletedOperation final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>,
                                          ABI::Windows::Foundation::IAsyncOperation<T>,
                                          ABI::Windows::Foundation::IAsyncInfo, Microsoft::WRL::FtmBase> {
    using RuntimeClassT = Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>,
                                                       ABI::Windows::Foundation::IAsyncOperation<T>,
                                                       ABI::Windows::Foundation::IAsyncInfo, Microsoft::WRL::FtmBase>;
    InspectableClass(L"Windows.Foundation.IAsyncOperation`1", BaseTrust);
    using Handler = ABI::Windows::Foundation::IAsyncOperationCompletedHandler<T>;
    Microsoft::WRL::ComPtr<Handler> completed_;
    std::mutex mutex_;
    bool assigned_ = false, closed_ = false;
    std::function<HRESULT(TResult*)> result_;
    const UINT32 id_ = NextAsyncId();

public:
    explicit CompletedOperation(std::function<HRESULT(TResult*)> result) : result_(std::move(result)) {}
    HRESULT STDMETHODCALLTYPE put_Completed(Handler* handler) override {
        Microsoft::WRL::ComPtr<Handler> callback(handler);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return RO_E_CLOSED;
            if (assigned_) return E_ILLEGAL_DELEGATE_ASSIGNMENT;
            assigned_ = true;
            completed_ = callback;
        }
        Log("TRACE", "CompletedOperation %u put_Completed handler=%p", id_, callback.Get());
        if (callback) {
            Microsoft::WRL::ComPtr<CompletedOperation> self(this);
            auto invoke = [self, callback] {
                const auto hr = callback->Invoke(self.Get(), ABI::Windows::Foundation::AsyncStatus::Completed);
                Log("TRACE", "CompletedOperation %u Invoke hr=%08lx", self->id_, hr);
            };
            // Games may create their purchase progress overlay after registering
            // completion. Deliver on a later dispatcher turn so it can be dismissed.
            // ABI-only callers without a host still receive immediate completion.
            if (FAILED(PostHostWork(0, invoke))) invoke();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Completed(Handler** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_ ? RO_E_CLOSED : completed_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE GetResults(TResult* value) override {
        Log("TRACE", "CompletedOperation %u GetResults", id_);
        if (!value) return E_POINTER;
        *value = {};
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_ ? RO_E_CLOSED : result_(value);
    }
    HRESULT STDMETHODCALLTYPE get_Id(UINT32* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *value = id_;
        return closed_ ? RO_E_CLOSED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Status(ABI::Windows::Foundation::AsyncStatus* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *value = ABI::Windows::Foundation::AsyncStatus::Completed;
        return closed_ ? RO_E_CLOSED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *value = S_OK;
        return closed_ ? RO_E_CLOSED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE Cancel() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_ ? RO_E_CLOSED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        Microsoft::WRL::ComPtr<Handler> releasing;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
            releasing.Attach(completed_.Detach());
        }
        // Delegate destruction can reenter this operation; release after unlock.
        return S_OK;
    }
};
}
