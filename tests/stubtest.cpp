#include <windows.h>
#include <roapi.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include "stubs/vungle.h"
#include <windows.web.http.filters.h>
#include <wrl/event.h>
#include <cstdio>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"appxportable.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    if (FAILED(get(HStringReference(L"VungleSDK.AdFactory").Get(), &factory))) return 2;
    ComPtr<appx::vungle::IAdFactoryStatic> statics;
    if (FAILED(factory.As(&statics))) return 3;
    ComPtr<appx::vungle::IVungleAd> ad;
    if (FAILED(statics->GetInstance(HStringReference(L"test").Get(), &ad)) || !ad) return 4;
    boolean available = true;
    if (FAILED(ad->get_AdPlayable(&available)) || available) return 5;
    ComPtr<ABI::Windows::Foundation::IAsyncOperation<HSTRING>> operation;
    if (FAILED(ad->PlayAdAsync(nullptr, &operation)) || !operation) return 6;
    HString result;
    if (SUCCEEDED(operation->GetResults(result.GetAddressOf()))) return 7;
    ComPtr<ABI::Windows::Foundation::IAsyncInfo> info;
    operation.As(&info);
    ABI::Windows::Foundation::AsyncStatus status;
    if (!info || FAILED(info->get_Status(&status)) || status != ABI::Windows::Foundation::AsyncStatus::Error) return 8;
    if (FAILED(get(HStringReference(L"Windows.Web.Http.Filters.HttpBaseProtocolFilter").Get(), &factory))) return 9;
    ComPtr<IInspectable> filter;
    if (FAILED(factory->ActivateInstance(&filter))) return 10;
    ComPtr<ABI::Windows::Web::Http::Filters::IHttpBaseProtocolFilter> base;
    if (FAILED(filter.As(&base))) return 11;
    ComPtr<ABI::Windows::Web::Http::Filters::IHttpCacheControl> cache;
    if (FAILED(base->get_CacheControl(&cache)) || !cache) return 12;
    // Starting an offline HTTP request succeeds; its async operation then
    // reports the network error through both IAsyncInfo and Completed.
    if (FAILED(get(HStringReference(L"Windows.Web.Http.HttpClient").Get(), &factory))) return 13;
    ComPtr<IInspectable> clientObject;
    if (FAILED(factory->ActivateInstance(&clientObject))) return 14;
    ComPtr<ABI::Windows::Web::Http::IHttpClient> client;
    if (FAILED(clientObject.As(&client))) return 15;
    ComPtr<ABI::Windows::Foundation::IUriRuntimeClassFactory> uriFactory;
    if (FAILED(RoGetActivationFactory(HStringReference(L"Windows.Foundation.Uri").Get(), IID_PPV_ARGS(&uriFactory))))
        return 16;
    ComPtr<ABI::Windows::Foundation::IUriRuntimeClass> uri;
    if (FAILED(uriFactory->CreateUri(HStringReference(L"https://example.invalid/test").Get(), &uri))) return 17;
    using HttpOperation =
        ABI::Windows::Foundation::IAsyncOperationWithProgress<ABI::Windows::Web::Http::HttpResponseMessage*,
                                                              ABI::Windows::Web::Http::HttpProgress>;
    using HttpCompleted = ABI::Windows::Foundation::IAsyncOperationWithProgressCompletedHandler<
        ABI::Windows::Web::Http::HttpResponseMessage*, ABI::Windows::Web::Http::HttpProgress>;
    ComPtr<HttpOperation> request;
    if (FAILED(client->GetAsync(uri.Get(), &request)) || !request) return 18;
    ComPtr<ABI::Windows::Web::Http::IHttpResponseMessage> response;
    const auto networkError = HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE);
    if (request->GetResults(&response) != networkError || response) return 19;
    if (FAILED(request.As(&info))) return 20;
    if (FAILED(info->get_Status(&status)) || status != ABI::Windows::Foundation::AsyncStatus::Error) return 21;
    HRESULT error = S_OK;
    if (FAILED(info->get_ErrorCode(&error)) || error != networkError) return 22;
    int calls = 0;
    auto completed =
        Callback<HttpCompleted>([&](HttpOperation* sender, ABI::Windows::Foundation::AsyncStatus state) -> HRESULT {
            if (state == ABI::Windows::Foundation::AsyncStatus::Error && sender == request.Get()) ++calls;
            return S_OK;
        });
    if (FAILED(request->put_Completed(completed.Get())) || calls != 1) return 23;
    if (client->GetAsync(uri.Get(), nullptr) != E_POINTER) return 24;
    request.Reset();
    if (client->GetAsync(nullptr, &request) != E_INVALIDARG || request) return 25;
    puts("stubtest: ads unavailable; playback fails");
    return 0;
}
