// FreeThreadedXMLHTTP60 for Wine, whose msxml lacks it. Games' native code sends
// metrics and update checks through it; the shim keeps them offline: every
// request fails, asynchronously, through the caller's IXMLHTTPRequest2Callback.
#include "http_request.h"
#include "log.h"
#include <msxml6.h>
#include <wininet.h>
#include <wrl.h>
#include <mutex>
#include <thread>
#include <memory>
#include <objidl.h>
using namespace Microsoft::WRL;
namespace appx {
namespace {
// GIT references keep callbacks valid across the caller's and worker's apartments.
class CallbackReference {
    ComPtr<IGlobalInterfaceTable> table_;
    DWORD cookie_ = 0;

public:
    ~CallbackReference() {
        if (cookie_) table_->RevokeInterfaceFromGlobal(cookie_);
    }
    HRESULT Initialize(IXMLHTTPRequest2Callback* callback) {
        auto hr = CoCreateInstance(CLSID_StdGlobalInterfaceTable, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&table_));
        return FAILED(hr) ? hr
                          : table_->RegisterInterfaceInGlobal(callback, __uuidof(IXMLHTTPRequest2Callback), &cookie_);
    }
    HRESULT Resolve(IXMLHTTPRequest2Callback** callback) {
        return table_->GetInterfaceFromGlobal(cookie_, __uuidof(IXMLHTTPRequest2Callback),
                                              reinterpret_cast<void**>(callback));
    }
};
class OfflineRequest final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IXMLHTTPRequest2, FtmBase> {
    std::mutex mutex_;
    std::shared_ptr<CallbackReference> callback_;
    bool opened_ = false, sent_ = false, aborted_ = false;

    HRESULT CompletionError() {
        std::lock_guard<std::mutex> lock(mutex_);
        return aborted_ ? E_ABORT : HRESULT_FROM_WIN32(ERROR_INTERNET_CANNOT_CONNECT);
    }
    HRESULT StartFailed(const std::shared_ptr<CallbackReference>& callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        sent_ = false;
        if (!aborted_) callback_ = callback;
        return E_OUTOFMEMORY;
    }

public:
    HRESULT STDMETHODCALLTYPE Open(const WCHAR* method, const WCHAR* url, IXMLHTTPRequest2Callback* callback,
                                   const WCHAR*, const WCHAR*, const WCHAR*, const WCHAR*) override {
        if (!method || !url) return E_INVALIDARG;
        std::shared_ptr<CallbackReference> incoming, releasing;
        if (callback) {
            try {
                incoming = std::make_shared<CallbackReference>();
            } catch (const std::bad_alloc&) {
                return E_OUTOFMEMORY;
            }
            auto hr = incoming->Initialize(callback);
            if (FAILED(hr)) return hr;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (sent_ || aborted_) return E_UNEXPECTED;
            opened_ = true;
            releasing.swap(callback_);
            callback_ = std::move(incoming);
        }
        Log("TRACE", "offline HTTP %ls %ls", method, url);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Send(ISequentialStream*, ULONGLONG) override {
        std::shared_ptr<CallbackReference> callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!opened_ || sent_ || aborted_) return E_UNEXPECTED;
            sent_ = true;
            callback.swap(callback_);
        }
        ComPtr<OfflineRequest> self(this);
        try {
            std::thread([self, callback]() mutable {
                const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                if (SUCCEEDED(initialized) && callback) {
                    ComPtr<IXMLHTTPRequest2Callback> target;
                    const auto hr = callback->Resolve(&target);
                    if (SUCCEEDED(hr))
                        target->OnError(self.Get(), self->CompletionError());
                    else
                        Log("FAIL", "offline HTTP callback resolution hr=%08lx", hr);
                    target.Reset();
                } else if (FAILED(initialized)) {
                    Log("FAIL", "offline HTTP worker COM initialization hr=%08lx", initialized);
                }
                // Releases and callback destructors may use COM too.
                callback.reset();
                self.Reset();
                if (SUCCEEDED(initialized)) CoUninitialize();
            }).detach();
        } catch (const std::system_error&) {
            return StartFailed(callback);
        } catch (const std::bad_alloc&) {
            return StartFailed(callback);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Abort() override {
        std::shared_ptr<CallbackReference> releasing;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            aborted_ = true;
            releasing.swap(callback_);
        }
        // A sent operation still delivers its terminal callback. Revocation of an
        // unopened/unsent callback happens outside the lock to permit reentry.
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCookie(const XHR_COOKIE*, DWORD* state) override {
        if (state) *state = XHR_COOKIE_STATE_REJECT;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCustomResponseStream(ISequentialStream*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetProperty(XHR_PROPERTY, ULONGLONG) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetRequestHeader(const WCHAR* header, const WCHAR*) override {
        return header ? S_OK : E_INVALIDARG;
    }
    // There is never a response to read.
    HRESULT STDMETHODCALLTYPE GetAllResponseHeaders(WCHAR** headers) override {
        if (!headers) return E_POINTER;
        *headers = nullptr;
        return E_UNEXPECTED;
    }
    HRESULT STDMETHODCALLTYPE GetCookie(const WCHAR*, const WCHAR*, DWORD, DWORD* count,
                                        XHR_COOKIE** cookies) override {
        if (!count || !cookies) return E_POINTER;
        *count = 0;
        *cookies = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetResponseHeader(const WCHAR*, WCHAR** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return E_UNEXPECTED;
    }
};
}
HRESULT CreateOfflineHttpRequest(DWORD count, MULTI_QI* results) {
    if (!count || !results) return E_INVALIDARG;
    for (DWORD i = 0; i < count; ++i) {
        results[i].pItf = nullptr;
        results[i].hr = E_NOINTERFACE;
    }
    auto request = Make<OfflineRequest>();
    if (!request) return E_OUTOFMEMORY;
    DWORD found = 0;
    for (DWORD i = 0; i < count; ++i) {
        results[i].pItf = nullptr;
        results[i].hr =
            results[i].pIID ? request.CopyTo(*results[i].pIID, reinterpret_cast<void**>(&results[i].pItf)) : E_POINTER;
        if (SUCCEEDED(results[i].hr)) ++found;
    }
    return found == count ? S_OK : found ? CO_S_NOTALLINTERFACES : E_NOINTERFACE;
}
}
