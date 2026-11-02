// The offline FreeThreadedXMLHTTP60 a game's native code creates with CoCreateInstanceFromApp.
#include <windows.h>
#include <roapi.h>
#include <msxml6.h>
#include <wininet.h>
#include <wrl.h>
#include <atomic>
#include <cstdio>
using namespace Microsoft::WRL;
// CLSID_FreeThreadedXMLHTTP60, as Bej3Win8Component.dll names it.
constexpr GUID freeThreadedHttp = {0x88d96a09, 0xf192, 0x11d4, {0xa6, 0x5f, 0x00, 0x40, 0x96, 0x32, 0x51, 0xe5}};
class RequestCallback final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IXMLHTTPRequest2Callback, FtmBase> {
public:
    std::atomic<int> errors{0}, responses{0};
    std::atomic<HRESULT> error{S_OK};
    std::atomic<DWORD> thread{0};
    std::atomic<HRESULT> apartment{E_FAIL};
    std::atomic<APTTYPE> apartmentType{APTTYPE_CURRENT};
    std::atomic<APTTYPEQUALIFIER> apartmentQualifier{APTTYPEQUALIFIER_IMPLICIT_MTA};
    IXMLHTTPRequest2* request = nullptr;
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE hold = nullptr, destroyed = nullptr;
    std::atomic<HRESULT>* destructionApartment = nullptr;
    bool abortOnDestruction = false, abortOnError = false;
    ~RequestCallback() {
        if (abortOnDestruction && request) request->Abort();
        if (destructionApartment) {
            APTTYPE type;
            APTTYPEQUALIFIER qualifier;
            *destructionApartment = CoGetApartmentType(&type, &qualifier);
        }
        if (destroyed) SetEvent(destroyed);
        CloseHandle(done);
    }
    HRESULT STDMETHODCALLTYPE OnRedirect(IXMLHTTPRequest2*, const WCHAR*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnHeadersAvailable(IXMLHTTPRequest2*, DWORD, const WCHAR*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDataAvailable(IXMLHTTPRequest2*, ISequentialStream*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnResponseReceived(IXMLHTTPRequest2*, ISequentialStream*) override {
        ++responses;
        SetEvent(done);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnError(IXMLHTTPRequest2* sender, HRESULT value) override {
        if (sender != request)
            error = E_FAIL;
        else
            error = value;
        APTTYPE type = APTTYPE_CURRENT;
        APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
        apartment = CoGetApartmentType(&type, &qualifier);
        apartmentType = type;
        apartmentQualifier = qualifier;
        thread = GetCurrentThreadId();
        ++errors;
        if (abortOnError) sender->Abort();
        SetEvent(done);
        if (hold) WaitForSingleObject(hold, 5000);
        return S_OK;
    }
};
static int Run() {
    if (!LoadLibraryW(L"appxportable.dll")) return 1;
    // A game's call, through the import the shim patches in this folder's modules.
    MULTI_QI result{&__uuidof(IXMLHTTPRequest2), nullptr, E_FAIL};
    if (FAILED(CoCreateInstanceFromApp(freeThreadedHttp, nullptr, CLSCTX_INPROC_SERVER, nullptr, 1, &result)) ||
        FAILED(result.hr) || !result.pItf)
        return 2;
    ComPtr<IXMLHTTPRequest2> request;
    request.Attach(static_cast<IXMLHTTPRequest2*>(result.pItf));
    auto callback = Make<RequestCallback>();
    callback->request = request.Get();
    callback->abortOnError = true;
    if (request->Open(nullptr, L"http://example.invalid/metrics", callback.Get(), nullptr, nullptr, nullptr, nullptr) !=
        E_INVALIDARG)
        return 3;
    if (FAILED(request->Open(L"POST", L"http://example.invalid/metrics", callback.Get(), nullptr, nullptr, nullptr,
                             nullptr)))
        return 4;
    if (FAILED(request->SetRequestHeader(L"Content-Type", L"application/json"))) return 5;
    // Offline: Send succeeds and the request fails later, once, through the callback.
    if (FAILED(request->Send(nullptr, 0))) return 6;
    if (WaitForSingleObject(callback->done, 5000) != WAIT_OBJECT_0) return 7;
    Sleep(100);
    if (callback->errors != 1 || callback->responses != 0 ||
        callback->error != HRESULT_FROM_WIN32(ERROR_INTERNET_CANNOT_CONNECT))
        return 8;
    if (callback->thread == GetCurrentThreadId()) return 9;
    if (FAILED(callback->apartment) || callback->apartmentType != APTTYPE_MTA ||
        callback->apartmentQualifier != APTTYPEQUALIFIER_NONE)
        return 11;
    // Aborting an unsent request invalidates it and releases callbacks outside the lock.
    MULTI_QI abortResult{&__uuidof(IXMLHTTPRequest2), nullptr, E_FAIL};
    if (FAILED(CoCreateInstanceFromApp(freeThreadedHttp, nullptr, CLSCTX_INPROC_SERVER, nullptr, 1, &abortResult)))
        return 12;
    ComPtr<IXMLHTTPRequest2> aborted;
    aborted.Attach(static_cast<IXMLHTTPRequest2*>(abortResult.pItf));
    HANDLE destroyed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    auto reentrant = Make<RequestCallback>();
    reentrant->request = aborted.Get();
    reentrant->abortOnDestruction = true;
    reentrant->destroyed = destroyed;
    if (FAILED(aborted->Open(L"POST", L"http://example.invalid/abort", reentrant.Get(), nullptr, nullptr, nullptr,
                             nullptr)))
        return 13;
    reentrant.Reset();
    if (FAILED(aborted->Abort()) || WaitForSingleObject(destroyed, 5000) != WAIT_OBJECT_0 ||
        aborted->Send(nullptr, 0) != E_UNEXPECTED ||
        aborted->Open(L"GET", L"http://example.invalid/reopen", nullptr, nullptr, nullptr, nullptr, nullptr) !=
            E_UNEXPECTED)
        return 14;
    CloseHandle(destroyed);
    // Callback ownership and COM initialization outlive the caller's references.
    MULTI_QI lifetimeResult{&__uuidof(IXMLHTTPRequest2), nullptr, E_FAIL};
    if (FAILED(CoCreateInstanceFromApp(freeThreadedHttp, nullptr, CLSCTX_INPROC_SERVER, nullptr, 1, &lifetimeResult)))
        return 15;
    ComPtr<IXMLHTTPRequest2> pending;
    pending.Attach(static_cast<IXMLHTTPRequest2*>(lifetimeResult.pItf));
    auto retained = Make<RequestCallback>();
    retained->request = pending.Get();
    HANDLE hold = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    destroyed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic<HRESULT> destructionApartment{E_FAIL};
    retained->hold = hold;
    retained->destroyed = destroyed;
    retained->destructionApartment = &destructionApartment;
    if (FAILED(pending->Open(L"POST", L"http://example.invalid/lifetime", retained.Get(), nullptr, nullptr, nullptr,
                             nullptr)) ||
        FAILED(pending->Send(nullptr, 0)) || WaitForSingleObject(retained->done, 5000) != WAIT_OBJECT_0)
        return 16;
    pending.Reset();
    retained.Reset();
    SetEvent(hold);
    if (WaitForSingleObject(destroyed, 5000) != WAIT_OBJECT_0 || FAILED(destructionApartment)) return 17;
    CloseHandle(hold);
    CloseHandle(destroyed);
    // Callback-free requests are permitted by Open's optional callback contract.
    MULTI_QI silentResult{&__uuidof(IXMLHTTPRequest2), nullptr, E_FAIL};
    if (FAILED(CoCreateInstanceFromApp(freeThreadedHttp, nullptr, CLSCTX_INPROC_SERVER, nullptr, 1, &silentResult)))
        return 18;
    ComPtr<IXMLHTTPRequest2> silent;
    silent.Attach(static_cast<IXMLHTTPRequest2*>(silentResult.pItf));
    if (FAILED(silent->Open(L"GET", L"http://example.invalid/silent", nullptr, nullptr, nullptr, nullptr, nullptr)) ||
        FAILED(silent->Send(nullptr, 0)))
        return 19;
    // CoCreateInstanceFromApp reports each requested interface and partial success.
    MULTI_QI multiple[] = {{&IID_IUnknown, nullptr, E_FAIL},
                           {&__uuidof(IXMLHTTPRequest2), nullptr, E_FAIL},
                           {&__uuidof(ISequentialStream), nullptr, E_FAIL}};
    if (CoCreateInstanceFromApp(freeThreadedHttp, nullptr, CLSCTX_INPROC_SERVER, nullptr, 3, multiple) !=
            CO_S_NOTALLINTERFACES ||
        FAILED(multiple[0].hr) || FAILED(multiple[1].hr) || multiple[2].hr != E_NOINTERFACE || multiple[2].pItf)
        return 20;
    multiple[0].pItf->Release();
    multiple[1].pItf->Release();
    if (CoCreateInstanceFromApp(freeThreadedHttp, nullptr, CLSCTX_INPROC_SERVER, nullptr, 0, nullptr) != E_INVALIDARG ||
        CoCreateInstanceFromApp(freeThreadedHttp, nullptr, CLSCTX_INPROC_SERVER, reinterpret_cast<void*>(1), 0,
                                nullptr) != E_INVALIDARG)
        return 21;
    // Other classes reach COM as before.
    MULTI_QI other{&IID_IUnknown, nullptr, E_FAIL};
    constexpr GUID unknown = {0x1d2a5e8b, 0x0f6c, 0x4a77, {0x9b, 0x3e, 0x42, 0x5c, 0x61, 0x7d, 0x80, 0x93}};
    if (CoCreateInstanceFromApp(unknown, nullptr, CLSCTX_INPROC_SERVER, nullptr, 1, &other) != REGDB_E_CLASSNOTREG ||
        other.pItf)
        return 10;
    return 0;
}
int main() {
    // Only Wine installs this interception; native Windows must keep its own COM class.
    if (!GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version")) {
        puts("httptest: offline interception is Wine-only");
        return 77;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int failure = Run();
    if (failure) {
        printf("FAIL http %d\n", failure);
        return failure;
    }
    puts("httptest: 0 failure(s)");
    return 0;
}
