// ConnectionProfile includes the Windows 8 usage ABI.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#else
#pragma warning(disable : 4996)
#endif
#include "generated/http_filters.h"
#include "generated/http.h"
#include "generated/network.h"
#include "com/factory.h"
#include "registry.h"
#include "options.h"
#include "com/completed_operation.h"
#include <wrl/event.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Web::Http;
using namespace ABI::Windows::Web::Http::Filters;
namespace appx {
namespace {
using HttpOperation =
    __FIAsyncOperationWithProgress_2_Windows__CWeb__CHttp__CHttpResponseMessage_Windows__CWeb__CHttp__CHttpProgress;
winrt::Windows::Foundation::IAsyncOperationWithProgress<winrt::Windows::Web::Http::HttpResponseMessage,
                                                        winrt::Windows::Web::Http::HttpProgress>
OfflineHttpRequest() {
    // A coroutine captures this exception in the operation rather than throwing
    // while the game starts a request. Its completion handler receives Error.
    co_return[]()->winrt::Windows::Web::Http::HttpResponseMessage {
        throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE));
    }
    ();
}
HRESULT MakeOfflineHttpRequest(HttpOperation** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    auto operation = OfflineHttpRequest();
    *value = reinterpret_cast<HttpOperation*>(winrt::detach_abi(operation));
    Log("TRACE", "HttpClient request completed offline");
    return S_OK;
}
}
class Cache final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIHttpCacheControl, FtmBase> {
    InspectableClass(L"Windows.Web.Http.Filters.HttpCacheControl", BaseTrust);
    HttpCacheReadBehavior read_ = HttpCacheReadBehavior_Default;
    HttpCacheWriteBehavior write_ = HttpCacheWriteBehavior_Default;

public:
    HRESULT STDMETHODCALLTYPE get_ReadBehavior(HttpCacheReadBehavior* value) override {
        if (!value) return E_POINTER;
        *value = read_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_ReadBehavior(HttpCacheReadBehavior value) override {
        read_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_WriteBehavior(HttpCacheWriteBehavior* value) override {
        if (!value) return E_POINTER;
        *value = write_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_WriteBehavior(HttpCacheWriteBehavior value) override {
        write_ = value;
        return S_OK;
    }
};
class Filter final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIHttpBaseProtocolFilter, DefaultIHttpFilter,
                                         ABI::Windows::Foundation::IClosable, FtmBase> {
    InspectableClass(L"Windows.Web.Http.Filters.HttpBaseProtocolFilter", BaseTrust);
    ComPtr<Cache> cache_ = Make<Cache>();
    boolean autoRedirect_ = true, ui_ = false, decompress_ = true, proxy_ = false;
    UINT32 connections_ = 6;

public:
#define BOOLEAN_PROPERTY(Name, field)                                                                                  \
    HRESULT STDMETHODCALLTYPE get_##Name(boolean* value) override {                                                    \
        if (!value) return E_POINTER;                                                                                  \
        *value = field;                                                                                                \
        return S_OK;                                                                                                   \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE put_##Name(boolean value) override {                                                     \
        field = value;                                                                                                 \
        return S_OK;                                                                                                   \
    }
    BOOLEAN_PROPERTY(AllowAutoRedirect, autoRedirect_)
    BOOLEAN_PROPERTY(AllowUI, ui_)
    BOOLEAN_PROPERTY(AutomaticDecompression, decompress_)
    BOOLEAN_PROPERTY(UseProxy, proxy_)
#undef BOOLEAN_PROPERTY
    HRESULT STDMETHODCALLTYPE get_MaxConnectionsPerServer(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = connections_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_MaxConnectionsPerServer(UINT32 value) override {
        connections_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CacheControl(IHttpCacheControl** value) override {
        if (!value) return E_POINTER;
        return cache_ ? cache_.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE SendRequestAsync(
        IHttpRequestMessage*,
        __FIAsyncOperationWithProgress_2_Windows__CWeb__CHttp__CHttpResponseMessage_Windows__CWeb__CHttp__CHttpProgress**
            value) override {
        return MakeOfflineHttpRequest(value);
    }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};
class Client final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIHttpClient, ABI::Windows::Foundation::IClosable, FtmBase> {
    InspectableClass(L"Windows.Web.Http.HttpClient", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE GetAsync(ABI::Windows::Foundation::IUriRuntimeClass* uri,
                                       HttpOperation** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return uri ? MakeOfflineHttpRequest(value) : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE GetWithOptionAsync(ABI::Windows::Foundation::IUriRuntimeClass* uri, HttpCompletionOption,
                                                 HttpOperation** value) override {
        return GetAsync(uri, value);
    }
    HRESULT STDMETHODCALLTYPE SendRequestAsync(IHttpRequestMessage* request, HttpOperation** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return request ? MakeOfflineHttpRequest(value) : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE SendRequestWithOptionAsync(IHttpRequestMessage* request, HttpCompletionOption,
                                                         HttpOperation** value) override {
        return SendRequestAsync(request, value);
    }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};
class HttpFactory final : public Factory<DefaultIHttpClientFactory> {
    bool filter_;

public:
    explicit HttpFactory(bool filter)
        : Factory(filter ? L"Windows.Web.Http.Filters.HttpBaseProtocolFilter" : L"Windows.Web.Http.HttpClient"),
          filter_(filter) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        if (filter_) {
            auto filter = Make<Filter>();
            return filter ? filter.CopyTo(value) : E_OUTOFMEMORY;
        }
        auto client = Make<Client>();
        return client ? client.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE Create(IHttpFilter*, IHttpClient** value) override {
        if (!value) return E_POINTER;
        auto client = Make<Client>();
        return client ? client.CopyTo(value) : E_OUTOFMEMORY;
    }
};
// Allows games' connectivity preflight to reach the local Store stub.
// HTTP clients remain offline; this profile does not create a connection.
using namespace ABI::Windows::Networking::Connectivity;
class StubNetworkAdapter final : public RuntimeClass<RuntimeClassFlags<WinRt>, INetworkAdapter, FtmBase> {
    InspectableClass(L"Windows.Networking.Connectivity.NetworkAdapter", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE get_OutboundMaxBitsPerSecond(UINT64* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_InboundMaxBitsPerSecond(UINT64* value) override {
        return get_OutboundMaxBitsPerSecond(value);
    }
    HRESULT STDMETHODCALLTYPE get_IanaInterfaceType(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 6; // Ethernet, matching the stub profile's non-WLAN/non-WWAN state.
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_NetworkItem(INetworkItem** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_NetworkAdapterId(GUID* value) override {
        if (!value) return E_POINTER;
        *value = {0xa89995c2, 0x0157, 0x4da4, {0x89, 0x97, 0x32, 0x7c, 0x08, 0xed, 0x89, 0xb7}};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetConnectedProfileAsync(
        __FIAsyncOperation_1_Windows__CNetworking__CConnectivity__CConnectionProfile** value) override;
};
class StubConnectionProfile final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, IConnectionProfile, IConnectionProfile2, FtmBase> {
    InspectableClass(L"Windows.Networking.Connectivity.ConnectionProfile", BaseTrust);
    ComPtr<StubNetworkAdapter> adapter_ = Make<StubNetworkAdapter>();
    template <class T> static HRESULT Unavailable(T** value) {
        if (!value) return E_POINTER;
        *value = nullptr;
        return E_NOTIMPL;
    }

    template <class T> static HRESULT Absent(T** value) {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }

public:
    // Model a wired connection: no WLAN/WWAN details or radio signal.
    HRESULT STDMETHODCALLTYPE get_IsWwanConnectionProfile(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsWlanConnectionProfile(boolean* value) override {
        return get_IsWwanConnectionProfile(value);
    }
    HRESULT STDMETHODCALLTYPE get_WwanConnectionProfileDetails(IWwanConnectionProfileDetails** value) override {
        return Absent(value);
    }
    HRESULT STDMETHODCALLTYPE get_WlanConnectionProfileDetails(IWlanConnectionProfileDetails** value) override {
        return Absent(value);
    }
    HRESULT STDMETHODCALLTYPE get_ServiceProviderGuid(__FIReference_1_GUID** value) override { return Absent(value); }
    HRESULT STDMETHODCALLTYPE GetSignalBars(__FIReference_1_byte** value) override { return Absent(value); }
    HRESULT STDMETHODCALLTYPE GetDomainConnectivityLevel(DomainConnectivityLevel* value) override {
        if (!value) return E_POINTER;
        *value = DomainConnectivityLevel_None;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNetworkUsageAsync(
        ABI::Windows::Foundation::DateTime, ABI::Windows::Foundation::DateTime, DataUsageGranularity,
        NetworkUsageStates,
        __FIAsyncOperation_1___FIVectorView_1_Windows__CNetworking__CConnectivity__CNetworkUsage** value) override {
        return Unavailable(value);
    }
    HRESULT STDMETHODCALLTYPE GetConnectivityIntervalsAsync(
        ABI::Windows::Foundation::DateTime, ABI::Windows::Foundation::DateTime, NetworkUsageStates,
        __FIAsyncOperation_1___FIVectorView_1_Windows__CNetworking__CConnectivity__CConnectivityInterval** value)
        override {
        return Unavailable(value);
    }
    HRESULT STDMETHODCALLTYPE get_ProfileName(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(L"Offline stub IAP", 16, value);
    }
    HRESULT STDMETHODCALLTYPE GetNetworkConnectivityLevel(NetworkConnectivityLevel* value) override {
        if (!value) return E_POINTER;
        *value = NetworkConnectivityLevel_InternetAccess;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNetworkNames(__FIVectorView_1_HSTRING** value) override { return Unavailable(value); }
    HRESULT STDMETHODCALLTYPE GetConnectionCost(IConnectionCost** value) override { return Unavailable(value); }
    HRESULT STDMETHODCALLTYPE GetDataPlanStatus(IDataPlanStatus** value) override { return Unavailable(value); }
    HRESULT STDMETHODCALLTYPE get_NetworkAdapter(INetworkAdapter** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return adapter_ ? adapter_.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetLocalUsage(ABI::Windows::Foundation::DateTime, ABI::Windows::Foundation::DateTime,
                                            IDataUsage** value) override {
        return Unavailable(value);
    }
    HRESULT STDMETHODCALLTYPE GetLocalUsagePerRoamingStates(ABI::Windows::Foundation::DateTime,
                                                            ABI::Windows::Foundation::DateTime, RoamingStates,
                                                            IDataUsage** value) override {
        return Unavailable(value);
    }
    HRESULT STDMETHODCALLTYPE get_NetworkSecuritySettings(INetworkSecuritySettings** value) override {
        return Unavailable(value);
    }
};
HRESULT StubNetworkAdapter::GetConnectedProfileAsync(
    __FIAsyncOperation_1_Windows__CNetworking__CConnectivity__CConnectionProfile** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    auto operation = Make<CompletedOperation<ConnectionProfile*, IConnectionProfile*>>(
        [](IConnectionProfile** profile) -> HRESULT {
            auto stub = Make<StubConnectionProfile>();
            return stub ? stub.CopyTo(profile) : E_OUTOFMEMORY;
        },
        true);
    return operation ? operation.CopyTo(value) : E_OUTOFMEMORY;
}
class NetworkFactory final : public Factory<ABI::Windows::Networking::Connectivity::DefaultINetworkInformationStatics> {
    EventSource<ABI::Windows::Networking::Connectivity::INetworkStatusChangedEventHandler> changed_;
    // Windows' own class works outside a package. It answers the queries about
    // the local machine; connectivity stays the shim's offline answer.
    static HRESULT Os(ComPtr<ABI::Windows::Networking::Connectivity::INetworkInformationStatics>& statics) {
        return OsGetActivationFactory(
            Microsoft::WRL::Wrappers::HStringReference(L"Windows.Networking.Connectivity.NetworkInformation").Get(),
            IID_PPV_ARGS(&statics));
    }

public:
    NetworkFactory() : Factory(L"Windows.Networking.Connectivity.NetworkInformation") {}
    HRESULT STDMETHODCALLTYPE
    GetConnectionProfiles(__FIVectorView_1_Windows__CNetworking__CConnectivity__CConnectionProfile** value) override {
        ComPtr<ABI::Windows::Networking::Connectivity::INetworkInformationStatics> statics;
        auto hr = Os(statics);
        return SUCCEEDED(hr) ? statics->GetConnectionProfiles(value) : hr;
    }
    HRESULT STDMETHODCALLTYPE
    GetLanIdentifiers(__FIVectorView_1_Windows__CNetworking__CConnectivity__CLanIdentifier** value) override {
        ComPtr<ABI::Windows::Networking::Connectivity::INetworkInformationStatics> statics;
        auto hr = Os(statics);
        return SUCCEEDED(hr) ? statics->GetLanIdentifiers(value) : hr;
    }
    // Unity's .NET networking (Dns.GetHostName) asks for these on a worker
    // thread, and a failure there ends the game.
    HRESULT STDMETHODCALLTYPE GetHostNames(__FIVectorView_1_Windows__CNetworking__CHostName** value) override {
        ComPtr<ABI::Windows::Networking::Connectivity::INetworkInformationStatics> statics;
        auto hr = Os(statics);
        return SUCCEEDED(hr) ? statics->GetHostNames(value) : hr;
    }
    HRESULT STDMETHODCALLTYPE GetProxyConfigurationAsync(
        ABI::Windows::Foundation::IUriRuntimeClass* uri,
        __FIAsyncOperation_1_Windows__CNetworking__CConnectivity__CProxyConfiguration** value) override {
        ComPtr<ABI::Windows::Networking::Connectivity::INetworkInformationStatics> statics;
        auto hr = Os(statics);
        return SUCCEEDED(hr) ? statics->GetProxyConfigurationAsync(uri, value) : hr;
    }
    HRESULT STDMETHODCALLTYPE
    GetSortedEndpointPairs(__FIIterable_1_Windows__CNetworking__CEndpointPair* destinations,
                           ABI::Windows::Networking::HostNameSortOptions options,
                           __FIVectorView_1_Windows__CNetworking__CEndpointPair** value) override {
        ComPtr<ABI::Windows::Networking::Connectivity::INetworkInformationStatics> statics;
        auto hr = Os(statics);
        return SUCCEEDED(hr) ? statics->GetSortedEndpointPairs(destinations, options, value) : hr;
    }
    HRESULT STDMETHODCALLTYPE
    GetInternetConnectionProfile(ABI::Windows::Networking::Connectivity::IConnectionProfile** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!GetOptions().stubIap) return S_OK;
        auto profile = Make<StubConnectionProfile>();
        static const bool announced = [] {
            Log("info", "Network: stub IAP connectivity preflight enabled (HTTP stays offline)");
            return true;
        }();
        (void)announced;
        return profile ? profile.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE
    add_NetworkStatusChanged(ABI::Windows::Networking::Connectivity::INetworkStatusChangedEventHandler* handler,
                             EventRegistrationToken* token) override {
        return changed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_NetworkStatusChanged(EventRegistrationToken token) override {
        return changed_.Remove(token);
    }
};
HRESULT MakeHttpFactory(bool filter, IActivationFactory** value) {
    auto factory = Make<HttpFactory>(filter);
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
HRESULT MakeNetworkFactory(IActivationFactory** value) {
    static auto factory = Make<NetworkFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
const ShimClass httpFilterClass{L"Windows.Web.Http.Filters.HttpBaseProtocolFilter",
                                [](IActivationFactory** value) { return MakeHttpFactory(true, value); }};
const ShimClass httpClientClass{L"Windows.Web.Http.HttpClient",
                                [](IActivationFactory** value) { return MakeHttpFactory(false, value); }};
const ShimClass networkClass{L"Windows.Networking.Connectivity.NetworkInformation", MakeNetworkFactory};
}
