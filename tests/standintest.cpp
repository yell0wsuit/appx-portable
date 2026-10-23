// Stand-ins for classes Windows cannot serve outside a package: parameterized
// IIDs, failed async operations, the offline Xbox LIVE SDK and HttpClientCSharp,
// Package.Current, and CoreWindow.GetForCurrentThread off the UI thread.
#include "com/pinterface.h"
#include "xbox/xbox_abi.h"
#include <windows.applicationmodel.h>
#include <windows.ui.core.h>
#include <roapi.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <atomic>
#include <cstdio>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

static int failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

using GetFactoryCall = HRESULT(WINAPI*)(HSTRING, IActivationFactory**);
static GetFactoryCall get;
static HRESULT Factory(const wchar_t* name, ComPtr<IActivationFactory>& factory) {
    return get(HStringReference(name).Get(), &factory);
}
// An IAsyncOperationCompletedHandler<T> for any T: Invoke follows IUnknown.
struct Handler {
    void** vtable;
    std::atomic<LONG> refs{1};
    ABI::Windows::Foundation::AsyncStatus status{};
    int calls = 0;
    static HRESULT STDMETHODCALLTYPE QueryInterface(Handler* self, REFIID, void** value) {
        *value = self;
        ++self->refs;
        return S_OK;
    }
    static ULONG STDMETHODCALLTYPE AddRef(Handler* self) { return ++self->refs; }
    static ULONG STDMETHODCALLTYPE Release(Handler* self) { return --self->refs; }
    static HRESULT STDMETHODCALLTYPE Invoke(Handler* self, IInspectable*, ABI::Windows::Foundation::AsyncStatus status) {
        self->status = status;
        ++self->calls;
        return S_OK;
    }
};
static void* handlerTable[] = {reinterpret_cast<void*>(&Handler::QueryInterface), reinterpret_cast<void*>(&Handler::AddRef),
                               reinterpret_cast<void*>(&Handler::Release), reinterpret_cast<void*>(&Handler::Invoke)};
struct OperationAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(void* results) = 0;
};

int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    // Reference IIDs: IAsyncOperation<Int32> from the Windows SDK, and two Xbox
    // instances from midl's output for Microsoft.Xbox.winmd.
    auto iid = appx::ParameterizedIid("pinterface({9fc2b0bb-e446-44e2-aa61-9cab8f636af2};i4)");
    CHECK(appx::GuidSignature(iid) == "{968b9665-06ed-5774-8f53-8edeabd5f7b5}");
    iid = appx::ParameterizedIid(appx::AsyncOperationOfClass("Microsoft.Xbox.UserProfile", appx::xbox::defaults::UserProfile));
    CHECK(appx::GuidSignature(iid) == "{31961233-ebde-54d4-841a-6d8a0eb239bf}");
    const auto identityOperation = appx::ParameterizedIid(
        appx::AsyncOperationOfClass("Microsoft.Xbox.Foundation.UserIdentity", appx::xbox::defaults::UserIdentity));
    CHECK(appx::GuidSignature(identityOperation) == "{3073e088-9b68-5869-9991-4271e9dab42b}");

    auto* dll = LoadLibraryW(L"appxportable.dll");
    if (!dll) return 1;
    get = reinterpret_cast<GetFactoryCall>(GetProcAddress(dll, "ShimGetFactory"));
    if (!get) return 2;

    // Xbox LIVE: nobody signed in; sign-in fails offline through its exact operation type.
    ComPtr<IActivationFactory> factory;
    ComPtr<appx::xbox::IXboxLIVEService> live;
    CHECK(SUCCEEDED(Factory(L"Microsoft.Xbox.XboxLIVEService", factory)) && SUCCEEDED(factory.As(&live)));
    if (live) {
        ComPtr<IInspectable> identity, signIn;
        CHECK(SUCCEEDED(live->get_SignedInUserIdentity(&identity)) && !identity);
        CHECK(SUCCEEDED(live->SignInAsync(&signIn)) && signIn);
        ComPtr<IUnknown> typed;
        CHECK(SUCCEEDED(signIn->QueryInterface(identityOperation, reinterpret_cast<void**>(typed.GetAddressOf()))));
        ComPtr<ABI::Windows::Foundation::IAsyncInfo> info;
        ABI::Windows::Foundation::AsyncStatus status{};
        HRESULT error = S_OK;
        CHECK(SUCCEEDED(signIn.As(&info)) && SUCCEEDED(info->get_Status(&status)) &&
              status == ABI::Windows::Foundation::AsyncStatus::Error);
        CHECK(info && SUCCEEDED(info->get_ErrorCode(&error)) && error == HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE));
        Handler handler{handlerTable};
        auto* operation = static_cast<OperationAbi*>(static_cast<IInspectable*>(typed.Get()));
        CHECK(SUCCEEDED(operation->put_Completed(reinterpret_cast<IUnknown*>(&handler))));
        CHECK(handler.calls == 1 && handler.status == ABI::Windows::Foundation::AsyncStatus::Error);
        void* result = reinterpret_cast<void*>(1);
        CHECK(operation->GetResults(&result) == error && !result);
        CHECK(info && SUCCEEDED(info->Close()));
        ComPtr<IInspectable> client;
        CHECK(SUCCEEDED(live->get_ServiceClient(&client)) && client);
    }
    ComPtr<appx::xbox::IUser10> user;  // no Microsoft.Xbox.dll beside the test: the SDK 1.0 layout
    CHECK(SUCCEEDED(Factory(L"Microsoft.Xbox.User", factory)));
    if (factory) {
        ComPtr<IInspectable> instance;
        CHECK(SUCCEEDED(factory->ActivateInstance(&instance)) && SUCCEEDED(instance.As(&user)));
        ComPtr<IInspectable> friends;
        CHECK(user && SUCCEEDED(user->GetFriendsAsync(&friends)) && friends);
    }
    // Xbox classes without a stand-in are "not registered", never the real DLL.
    factory.Reset();
    CHECK(Factory(L"Microsoft.Xbox.Multiplayer.MatchmakingService", factory) == REGDB_E_CLASSNOTREG && !factory);

    // HttpClientCSharp: requests complete at once, unsuccessfully.
    CHECK(SUCCEEDED(Factory(L"HttpClientCSharp.Request", factory)));
    if (factory) {
        ComPtr<IInspectable> request;
        CHECK(SUCCEEDED(factory->ActivateInstance(&request)) && request);
        // IRequestClass: Submit is slot 15, IsComplete 11, GetResponse 10.
        auto** table = *reinterpret_cast<void***>(request.Get());
        using Submit = HRESULT(STDMETHODCALLTYPE*)(IInspectable*);
        using IsComplete = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, boolean*);
        using GetResponse = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, IInspectable**);
        boolean complete = false;
        CHECK(SUCCEEDED(reinterpret_cast<Submit>(table[15])(request.Get())));
        CHECK(SUCCEEDED(reinterpret_cast<IsComplete>(table[11])(request.Get(), &complete)) && complete);
        ComPtr<IInspectable> response;
        CHECK(SUCCEEDED(reinterpret_cast<GetResponse>(table[10])(request.Get(), &response)) && response);
        if (response) {
            using Success = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, boolean*);
            boolean success = true;
            CHECK(SUCCEEDED(reinterpret_cast<Success>((*reinterpret_cast<void***>(response.Get()))[6])(response.Get(), &success)) &&
                  !success);
        }
    }

    // Package.Current answers without package identity.
    ComPtr<ABI::Windows::ApplicationModel::IPackageStatics> packages;
    CHECK(SUCCEEDED(Factory(L"Windows.ApplicationModel.Package", factory)) && SUCCEEDED(factory.As(&packages)));
    if (packages) {
        ComPtr<ABI::Windows::ApplicationModel::IPackage> package;
        ComPtr<ABI::Windows::ApplicationModel::IPackageId> id;
        CHECK(SUCCEEDED(packages->get_Current(&package)) && SUCCEEDED(package->get_Id(&id)));
        HString name;
        CHECK(id && SUCCEEDED(id->get_Name(name.GetAddressOf())));
    }

    // No CoreWindow on threads other than the UI thread (and no UI thread here).
    ComPtr<ABI::Windows::UI::Core::ICoreWindowStatic> windows;
    CHECK(SUCCEEDED(Factory(L"Windows.UI.Core.CoreWindow", factory)) && SUCCEEDED(factory.As(&windows)));
    if (windows) {
        ComPtr<ABI::Windows::UI::Core::ICoreWindow> window;
        CHECK(SUCCEEDED(windows->GetForCurrentThread(&window)) && !window);
    }
    printf("standintest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
