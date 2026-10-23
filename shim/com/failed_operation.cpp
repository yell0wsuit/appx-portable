#include "failed_operation.h"
#include "async_action.h"
#include "async_id.h"
#include <atomic>
#include <mutex>
#include <new>
namespace appx {
namespace {
// IAsyncOperation<T>'s layout after IInspectable, for any T.
struct OperationAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(void* results) = 0;
};
using InvokeCall = HRESULT(STDMETHODCALLTYPE*)(IUnknown* handler, IInspectable* operation,
                                              ABI::Windows::Foundation::AsyncStatus status);
class FailedOperation final : public OperationAbi, public ABI::Windows::Foundation::IAsyncInfo {
    std::atomic<ULONG> refs_{1};
    const GUID iid_;
    const HRESULT error_;
    const size_t resultSize_;
    const UINT32 id_ = NextAsyncId();
    std::mutex mutex_;
    IUnknown* completed_ = nullptr;
    bool assigned_ = false, closed_ = false;
    OperationAbi* Operation() { return this; }

public:
    FailedOperation(const GUID& iid, HRESULT error, size_t resultSize)
        : iid_(iid), error_(FAILED(error) ? error : E_FAIL), resultSize_(resultSize) {}
    ~FailedOperation() {
        if (completed_) completed_->Release();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable) || iid == __uuidof(IAgileObject) || iid == iid_)
            *value = Operation();
        else if (iid == __uuidof(ABI::Windows::Foundation::IAsyncInfo))
            *value = static_cast<ABI::Windows::Foundation::IAsyncInfo*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --refs_;
        if (!left) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** iids) override {
        if (!count || !iids) return E_POINTER;
        *iids = static_cast<IID*>(CoTaskMemAlloc(2 * sizeof(IID)));
        if (!*iids) return E_OUTOFMEMORY;
        (*iids)[0] = iid_;
        (*iids)[1] = __uuidof(ABI::Windows::Foundation::IAsyncInfo);
        *count = 2;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(L"Windows.Foundation.IAsyncOperation`1", 36, value);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* value) override {
        if (!value) return E_POINTER;
        *value = BaseTrust;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Completed(IUnknown* handler) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return RO_E_CLOSED;
            if (assigned_) return E_ILLEGAL_DELEGATE_ASSIGNMENT;
            assigned_ = true;
            completed_ = handler;
            if (handler) handler->AddRef();
        }
        // IAsyncOperationCompletedHandler<T>::Invoke follows IUnknown.
        if (handler) reinterpret_cast<InvokeCall>((*reinterpret_cast<void***>(handler))[3])(
            handler, Operation(), ABI::Windows::Foundation::AsyncStatus::Error);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Completed(IUnknown** handler) override {
        if (!handler) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *handler = completed_;
        if (completed_) completed_->AddRef();
        return closed_ ? RO_E_CLOSED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetResults(void* results) override {
        if (!results) return E_POINTER;
        memset(results, 0, resultSize_);
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_ ? RO_E_CLOSED : error_;
    }
    HRESULT STDMETHODCALLTYPE get_Id(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = id_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Status(ABI::Windows::Foundation::AsyncStatus* value) override {
        if (!value) return E_POINTER;
        *value = ABI::Windows::Foundation::AsyncStatus::Error;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* value) override {
        if (!value) return E_POINTER;
        *value = error_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Cancel() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close() override {
        IUnknown* releasing = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
            releasing = completed_;
            completed_ = nullptr;
        }
        // Delegate destruction can reenter this operation; release after unlocking.
        if (releasing) releasing->Release();
        return S_OK;
    }
};
}
HRESULT MakeFailedOperation(const GUID& iid, HRESULT error, size_t resultSize, IInspectable** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    auto* operation = new (std::nothrow) FailedOperation(iid, error, resultSize);
    if (!operation) return E_OUTOFMEMORY;
    *value = static_cast<OperationAbi*>(operation);
    return S_OK;
}
HRESULT MakeFailedAction(HRESULT error, ABI::Windows::Foundation::IAsyncAction** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    auto action = Make<AsyncAction>();
    if (!action) return E_OUTOFMEMORY;
    action->Finish(FAILED(error) ? error : E_FAIL);
    return action.CopyTo(value);
}
}
