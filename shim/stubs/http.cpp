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
#include <wrl/event.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Web::Http;
using namespace ABI::Windows::Web::Http::Filters;
namespace appx {
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
        if (!value) return E_POINTER;
        *value = nullptr;
        return E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};
class Client final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIHttpClient, ABI::Windows::Foundation::IClosable, FtmBase> {
    InspectableClass(L"Windows.Web.Http.HttpClient", BaseTrust);

public:
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
class StubConnectionProfile final : public RuntimeClass<RuntimeClassFlags<WinRt>, IConnectionProfile, FtmBase> {
    InspectableClass(L"Windows.Networking.Connectivity.ConnectionProfile", BaseTrust);
    template <class T> static HRESULT Unavailable(T** value) {
        if (!value) return E_POINTER;
        *value = nullptr;
        return E_NOTIMPL;
    }

public:
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
    HRESULT STDMETHODCALLTYPE get_NetworkAdapter(INetworkAdapter** value) override { return Unavailable(value); }
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
class NetworkFactory final : public Factory<ABI::Windows::Networking::Connectivity::DefaultINetworkInformationStatics> {
    EventSource<ABI::Windows::Networking::Connectivity::INetworkStatusChangedEventHandler> changed_;

public:
    NetworkFactory() : Factory(L"Windows.Networking.Connectivity.NetworkInformation") {}
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
}
