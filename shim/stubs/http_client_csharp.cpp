// HttpClientCSharp: a small C# WinRT component Halfbrick games (Fruit Ninja)
// ship for HTTP requests, hosted by Windows' CLRHost.dll, which needs the
// package to run. Stood in offline, like the Xbox LIVE services: every
// request completes at once, unsuccessfully, and nothing is sent.
// Interfaces declared from HttpClientCSharp.winmd (same ABI for object
// parameters as IInspectable*).
#include "registry.h"
#include "com/factory.h"
#include "log.h"
#include <wrl.h>
#include <mutex>
#include <string>
using namespace Microsoft::WRL;
namespace appx {
namespace {
MIDL_INTERFACE("39AEA9EF-AACA-5971-72C7-29FE81EA8EF0")
IClientClass : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE BeginRequest(IInspectable* request) = 0;
};
MIDL_INTERFACE("D34558D3-8600-520E-66A2-A6F79EFE120D")
IRequestClass : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE AddHeader(HSTRING header) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ContentBody(UINT32* size, BYTE** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_RequestMethod(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Url(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResponse(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsComplete(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ContentBody(UINT32 size, BYTE* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_RequestMethod(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Url(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Submit() = 0;
};
MIDL_INTERFACE("858A05E2-947E-5551-6068-745B8DC97278")
IResponseClass : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_IsSuccessfulResponse(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ResponseBody(UINT32* size, BYTE** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ResponseCode(INT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsSuccessfulResponse(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ResponseBody(UINT32 size, BYTE* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ResponseCode(INT32 value) = 0;
};
// WinRT byte arrays out: the callee allocates with CoTaskMemAlloc.
HRESULT CopyBytes(const std::string& bytes, UINT32* size, BYTE** value) {
    if (!size || !value) return E_POINTER;
    *size = 0;
    *value = nullptr;
    if (bytes.empty()) return S_OK;
    *value = static_cast<BYTE*>(CoTaskMemAlloc(bytes.size()));
    if (!*value) return E_OUTOFMEMORY;
    memcpy(*value, bytes.data(), bytes.size());
    *size = static_cast<UINT32>(bytes.size());
    return S_OK;
}
HRESULT CopyText(const std::wstring& text, HSTRING* value) {
    if (!value) return E_POINTER;
    return WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), value);
}
std::wstring Text(HSTRING value) { return WindowsGetStringRawBuffer(value, nullptr); }

class Response final : public RuntimeClass<RuntimeClassFlags<WinRt>, IResponseClass, FtmBase> {
    InspectableClass(L"HttpClientCSharp.Response", BaseTrust);
    std::mutex mutex_;
    bool success_ = false;
    INT32 code_ = 0;
    std::string body_;

public:
    HRESULT STDMETHODCALLTYPE get_IsSuccessfulResponse(boolean* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *value = success_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ResponseBody(UINT32* size, BYTE** value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return CopyBytes(body_, size, value);
    }
    HRESULT STDMETHODCALLTYPE get_ResponseCode(INT32* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *value = code_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsSuccessfulResponse(boolean value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        success_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_ResponseBody(UINT32 size, BYTE* value) override {
        if (size && !value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        body_.assign(reinterpret_cast<const char*>(value), size);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_ResponseCode(INT32 value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        code_ = value;
        return S_OK;
    }
};
class Request final : public RuntimeClass<RuntimeClassFlags<WinRt>, IRequestClass, FtmBase> {
    InspectableClass(L"HttpClientCSharp.Request", BaseTrust);
    std::mutex mutex_;
    std::wstring url_, method_ = L"GET";
    std::string body_;
    bool complete_ = false;
    ComPtr<Response> response_;

public:
    HRESULT STDMETHODCALLTYPE AddHeader(HSTRING) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ContentBody(UINT32* size, BYTE** value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return CopyBytes(body_, size, value);
    }
    HRESULT STDMETHODCALLTYPE get_RequestMethod(HSTRING* value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return CopyText(method_, value);
    }
    HRESULT STDMETHODCALLTYPE get_Url(HSTRING* value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return CopyText(url_, value);
    }
    HRESULT STDMETHODCALLTYPE GetResponse(IInspectable** value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *value = nullptr;
        return response_ ? response_.CopyTo(value) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsComplete(boolean* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *value = complete_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_ContentBody(UINT32 size, BYTE* value) override {
        if (size && !value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        body_.assign(reinterpret_cast<const char*>(value), size);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_RequestMethod(HSTRING value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        method_ = Text(value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Url(HSTRING value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        url_ = Text(value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Submit() override {
        auto response = Make<Response>();  // unsuccessful, code 0, empty
        if (!response) return E_OUTOFMEMORY;
        std::lock_guard<std::mutex> lock(mutex_);
        Log("TRACE", "HttpClientCSharp offline: %ls %ls not sent", method_.c_str(), url_.c_str());
        response_ = response;
        complete_ = true;
        return S_OK;
    }
};
class Client final : public RuntimeClass<RuntimeClassFlags<WinRt>, IClientClass, FtmBase> {
    InspectableClass(L"HttpClientCSharp.Client", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE BeginRequest(IInspectable* request) override {
        ComPtr<IRequestClass> typed;
        if (!request || FAILED(request->QueryInterface(IID_PPV_ARGS(&typed)))) return E_INVALIDARG;
        return typed->Submit();
    }
};
template <class T> class ConstructFactory final : public Factory<> {
public:
    explicit ConstructFactory(const wchar_t* name) : Factory(name) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return Make<T>().CopyTo(value);
    }
};
}
HRESULT MakeHttpClientCSharpFactory(const wchar_t* name, IActivationFactory** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    ComPtr<IActivationFactory> factory;
    if (wcscmp(name, L"HttpClientCSharp.Request") == 0) factory = Make<ConstructFactory<Request>>(name);
    else if (wcscmp(name, L"HttpClientCSharp.Response") == 0) factory = Make<ConstructFactory<Response>>(name);
    else if (wcscmp(name, L"HttpClientCSharp.Client") == 0) factory = Make<ConstructFactory<Client>>(name);
    else return REGDB_E_CLASSNOTREG;
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
const ShimClass httpClientCSharpClass{L"HttpClientCSharp.", MakeHttpClientCSharpFactory};
}
