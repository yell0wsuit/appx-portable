// An offline stand-in for the Xbox LIVE SDK for Windows (Microsoft.Xbox.dll),
// whose services have shut down; the real DLL also crashes unpackaged.
// Nobody is signed in and every service call fails as a disconnected client's
// would, so games carry on offline. Every other Microsoft.Xbox class reports
// "not registered", so the real DLL is never loaded.
#include "registry.h"
#include "xbox_abi.h"
#include "marketplace_abi.h"
#include "com/factory.h"
#include "com/failed_operation.h"
#include "com/pinterface.h"
#include "com/async_action.h"
#include "log.h"
#include <wrl.h>
#include <filesystem>
#include <wrl/wrappers/corewrappers.h>
#include <windows.storage.streams.h>
#include <map>
#include <mutex>
#pragma comment(lib, "version.lib")
using namespace Microsoft::WRL;
namespace appx {
namespace {
using namespace xbox;
constexpr HRESULT Offline = HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE);

// The SDK version the game shipped with, from Microsoft.Xbox.dll beside it:
// major * 100 + minor (100 for 1.0, 102 for 1.2); 0 when unknown.
int SdkVersion() {
    static const int version = [] {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        const auto dll = (std::filesystem::path(path).parent_path() / L"Microsoft.Xbox.dll").wstring();
        DWORD ignored = 0;
        const DWORD size = GetFileVersionInfoSizeW(dll.c_str(), &ignored);
        std::string data(size, '\0');
        VS_FIXEDFILEINFO* info = nullptr;
        UINT length = 0;
        int result = 0;
        if (size && GetFileVersionInfoW(dll.c_str(), 0, size, data.data()) &&
            VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) && info)
            result = static_cast<int>(HIWORD(info->dwFileVersionMS) * 100 + LOWORD(info->dwFileVersionMS));
        Log("INFO", "Xbox LIVE: offline stand-in for SDK %d.%d", result / 100, result % 100);
        return result;
    }();
    return version;
}
HRESULT FailedClass(const char* what, const char* runtimeClass, const GUID& defaultInterface, IInspectable** op) {
    Log("TRACE", "Xbox LIVE offline: %s", what);
    return MakeFailedOperation(ParameterizedIid(AsyncOperationOfClass(runtimeClass, defaultInterface)), Offline,
                               sizeof(void*), op);
}
HRESULT FailedInt32(const char* what, IInspectable** op) {
    Log("TRACE", "Xbox LIVE offline: %s", what);
    return MakeFailedOperation(ParameterizedIid("pinterface({9fc2b0bb-e446-44e2-aa61-9cab8f636af2};i4)"), Offline,
                               sizeof(INT32), op);
}
HRESULT FailedAction(const char* what, IAsyncAction** action) {
    Log("TRACE", "Xbox LIVE offline: %s", what);
    return MakeFailedAction(Offline, action);
}
HRESULT DoneAction(IAsyncAction** action) {
    if (!action) return E_POINTER;
    auto done = Make<AsyncAction>();
    if (!done) return E_OUTOFMEMORY;
    done->Finish(S_OK);
    return done.CopyTo(action);
}
HRESULT Nobody(IInspectable** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    return S_OK;
}
// SignedOut never fires: nobody signs in.
class SignedOutEvent {
    std::mutex mutex_;
    std::map<INT64, ComPtr<IUnknown>> handlers_;
    INT64 next_ = 0;

public:
    HRESULT Add(IUnknown* handler, EventRegistrationToken* token) {
        if (!handler || !token) return E_INVALIDARG;
        std::lock_guard<std::mutex> lock(mutex_);
        token->value = ++next_;
        handlers_[token->value] = handler;
        return S_OK;
    }
    HRESULT Remove(EventRegistrationToken token) {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_.erase(token.value);
        return S_OK;
    }
};

class ServiceClient final : public RuntimeClass<RuntimeClassFlags<WinRt>, IServiceClient, FtmBase> {
    InspectableClass(L"Microsoft.Xbox.Foundation.ServiceClient", BaseTrust);
    SignedOutEvent signedOut_;

public:
    HRESULT STDMETHODCALLTYPE SignInAsync(IAsyncAction** action) override {
        return FailedAction("ServiceClient.SignInAsync", action);
    }
    HRESULT STDMETHODCALLTYPE SignOutAsync(IAsyncAction** action) override { return DoneAction(action); }
    HRESULT STDMETHODCALLTYPE add_SignedOut(IUnknown* handler, EventRegistrationToken* token) override {
        return signedOut_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_SignedOut(EventRegistrationToken token) override {
        return signedOut_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE get_SignedInUserIdentity(IInspectable** identity) override { return Nobody(identity); }
    HRESULT STDMETHODCALLTYPE get_Token(HSTRING* token) override {
        if (!token) return E_POINTER;
        *token = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_TitleId(UINT32* titleId) override {
        if (!titleId) return E_POINTER;
        *titleId = 0;
        return S_OK;
    }
};

// Microsoft.Xbox.User in both SDK layouts; each method fails offline.
#define APPX_XBOX_USER_COMMON                                                                                          \
    HRESULT STDMETHODCALLTYPE get_Identity(IInspectable** identity) override { return Nobody(identity); }              \
    HRESULT STDMETHODCALLTYPE get_Status(IInspectable** status) override { return Nobody(status); }                    \
    HRESULT STDMETHODCALLTYPE GetProfileAsync(IInspectable** op) override {                                            \
        return FailedClass("User.GetProfileAsync", "Microsoft.Xbox.UserProfile", defaults::UserProfile, op);           \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE GetFriendsAsync(IInspectable** op) override {                                            \
        return FailedClass("User.GetFriendsAsync", "Microsoft.Xbox.UserCollection", defaults::UserCollection, op);     \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE GetAchievementsAsync(UINT32, UINT32, boolean, IInspectable*, IInspectable** op)          \
        override {                                                                                                     \
        return FailedClass("User.GetAchievementsAsync", "Microsoft.Xbox.AchievementCollection",                        \
                           defaults::AchievementCollection, op);                                                       \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE UnlockAchievementAsync(UINT32, IAsyncAction** action) override {                         \
        return FailedAction("User.UnlockAchievementAsync", action);                                                    \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE GetAvatarManifestAsync(IInspectable** op) override {                                     \
        return FailedClass("User.GetAvatarManifestAsync", "Microsoft.Xbox.AvatarManifest", defaults::AvatarManifest,   \
                           op);                                                                                        \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE PostUserMessageAsync(IInspectable*, IAsyncAction** action) override {                    \
        return FailedAction("User.PostUserMessageAsync", action);                                                      \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE GetUserMessagesAsync(UINT32, UINT32, IInspectable*, IInspectable** op) override {        \
        return FailedClass("User.GetUserMessagesAsync", "Microsoft.Xbox.UserMessageCollection",                        \
                           defaults::UserMessageCollection, op);                                                       \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE GetUserMessagesForTitleAsync(UINT32, UINT32, UINT32, IInspectable*, IInspectable** op)   \
        override {                                                                                                     \
        return FailedClass("User.GetUserMessagesForTitleAsync", "Microsoft.Xbox.UserMessageCollection",                \
                           defaults::UserMessageCollection, op);                                                       \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE DeleteUserMessageAsync(HSTRING, IAsyncAction** action) override {                        \
        return FailedAction("User.DeleteUserMessageAsync", action);                                                    \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE CreateMultiplayerSessionAsync(IInspectable*, INT32, IInspectable** op) override {        \
        return FailedClass("User.CreateMultiplayerSessionAsync", "Microsoft.Xbox.Multiplayer.MultiplayerSession",      \
                           defaults::MultiplayerSession, op);                                                          \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE JoinMultiplayerSessionAsync(HSTRING, INT32, IInspectable*, IInspectable** op) override { \
        return FailedClass("User.JoinMultiplayerSessionAsync", "Microsoft.Xbox.Multiplayer.MultiplayerSession",        \
                           defaults::MultiplayerSession, op);                                                          \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE GetMultiplayerSessionSummariesAsync(UINT32, GUID, INT32, INT32, INT32,                   \
                                                                  IInspectable** op) override {                        \
        return FailedClass("User.GetMultiplayerSessionSummariesAsync",                                                 \
                           "Microsoft.Xbox.Multiplayer.MultiplayerSessionSummaryCollection",                           \
                           defaults::MultiplayerSessionSummaryCollection, op);                                         \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE CanPerformActionAsync(HSTRING, UINT64, IInspectable** op) override {                     \
        return FailedInt32("User.CanPerformActionAsync", op);                                                          \
    }

class User10 final : public RuntimeClass<RuntimeClassFlags<WinRt>, IUser10, FtmBase> {
    InspectableClass(L"Microsoft.Xbox.User", BaseTrust);

public:
    APPX_XBOX_USER_COMMON
    HRESULT STDMETHODCALLTYPE SetProfileAsync(IInspectable*, IInspectable** op) override {
        return FailedClass("User.SetProfileAsync", "Microsoft.Xbox.UserProfile", defaults::UserProfile, op);
    }
};
class User12 final : public RuntimeClass<RuntimeClassFlags<WinRt>, IUser12, FtmBase> {
    InspectableClass(L"Microsoft.Xbox.User", BaseTrust);

public:
    APPX_XBOX_USER_COMMON
    HRESULT STDMETHODCALLTYPE JoinMultiplayerSessionWithBufferAsync(HSTRING, INT32, IInspectable*,
                                                                    IInspectable** op) override {
        return FailedClass("User.JoinMultiplayerSessionWithBufferAsync",
                           "Microsoft.Xbox.Multiplayer.MultiplayerSession", defaults::MultiplayerSession, op);
    }
};
#undef APPX_XBOX_USER_COMMON
// The layout must match the game's SDK exactly; refuse versions not known.
HRESULT MakeUser(IInspectable** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    const int version = SdkVersion();
    // SDK 1.1 retains the 1.0 IUser slots (including SetProfileAsync).
    if (version == 100 || version == 101 || version == 0) return Make<User10>().CopyTo(value);
    // Hitman GO's SDK 2.0 metadata retains the 1.2 IID and complete slot layout.
    if (version == 102 || version == 200) return Make<User12>().CopyTo(value);
    Log("FAIL", "Xbox LIVE SDK %d.%d: Microsoft.Xbox.User layout unknown; not provided", version / 100, version % 100);
    return REGDB_E_CLASSNOTREG;
}

class LeaderboardService final : public RuntimeClass<RuntimeClassFlags<WinRt>, ILeaderboardService, FtmBase> {
    InspectableClass(L"Microsoft.Xbox.Leaderboards.LeaderboardService", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE GetLeaderboardsAsync(IInspectable** op) override {
        return FailedClass("LeaderboardService.GetLeaderboardsAsync",
                           "Microsoft.Xbox.Leaderboards.LeaderboardMetadataCollection",
                           defaults::LeaderboardMetadataCollection, op);
    }
    HRESULT STDMETHODCALLTYPE GetLeaderboardAsync(UINT32, UINT32, UINT32, boolean, IInspectable*, IInspectable*,
                                                  IInspectable** op) override {
        return FailedClass("LeaderboardService.GetLeaderboardAsync", "Microsoft.Xbox.Leaderboards.Leaderboard",
                           defaults::Leaderboard, op);
    }
    HRESULT STDMETHODCALLTYPE GetSystemLeaderboardAsync(UINT32, UINT32, HSTRING, IInspectable*,
                                                        IInspectable** op) override {
        return FailedClass("LeaderboardService.GetSystemLeaderboardAsync", "Microsoft.Xbox.Leaderboards.Leaderboard",
                           defaults::Leaderboard, op);
    }
    HRESULT STDMETHODCALLTYPE PostResultAsync(UINT32, INT32, INT64, IAsyncAction** action) override {
        return FailedAction("LeaderboardService.PostResultAsync", action);
    }
    HRESULT STDMETHODCALLTYPE PostResultsAsync(UINT32, IInspectable*, IAsyncAction** action) override {
        return FailedAction("LeaderboardService.PostResultsAsync", action);
    }
};

class LiveFactory final : public Factory<IXboxLIVEService> {
    SignedOutEvent signedOut_;

public:
    LiveFactory() : Factory(L"Microsoft.Xbox.XboxLIVEService") {}
    HRESULT STDMETHODCALLTYPE SignInAsync(IInspectable** op) override {
        return FailedClass("XboxLIVEService.SignInAsync", "Microsoft.Xbox.Foundation.UserIdentity",
                           defaults::UserIdentity, op);
    }
    HRESULT STDMETHODCALLTYPE SignOutAsync(IAsyncAction** action) override { return DoneAction(action); }
    HRESULT STDMETHODCALLTYPE add_SignedOut(IUnknown* handler, EventRegistrationToken* token) override {
        return signedOut_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_SignedOut(EventRegistrationToken token) override {
        return signedOut_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE get_SignedInUserIdentity(IInspectable** identity) override { return Nobody(identity); }
    HRESULT STDMETHODCALLTYPE get_ServiceClient(IInspectable** client) override {
        if (!client) return E_POINTER;
        static auto shared = Make<ServiceClient>();
        return shared ? shared.CopyTo(client) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE InvalidateCacheGroup(INT32) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ShowGamesApplicationAsync(INT32, IAsyncAction** action) override {
        return FailedAction("XboxLIVEService.ShowGamesApplicationAsync", action);
    }
};
class UserFactory final : public Factory<IUserFactory> {
public:
    UserFactory() : Factory(L"Microsoft.Xbox.User") {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override { return MakeUser(value); }
    HRESULT STDMETHODCALLTYPE CreateUser(UINT64, HSTRING, IInspectable** value) override { return MakeUser(value); }
};
class LeaderboardFactory final : public Factory<> {
public:
    LeaderboardFactory() : Factory(L"Microsoft.Xbox.Leaderboards.LeaderboardService") {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return Make<LeaderboardService>().CopyTo(value);
    }
};
class ResourceLoaderFactory final : public Factory<IResourceLoaderStatics> {
public:
    ResourceLoaderFactory() : Factory(L"Microsoft.Xbox.ResourceLoader") {}
    // Localized Xbox LIVE UI strings; offline there is nothing to show by id.
    HRESULT STDMETHODCALLTYPE GetStringById(INT32 id, HSTRING* value) override {
        if (!value) return E_POINTER;
        Log("TRACE", "Xbox LIVE offline: ResourceLoader.GetStringById(%d)", id);
        return WindowsCreateString(L"Xbox LIVE", 9, value);
    }
};
class TitleStorageFile final : public RuntimeClass<RuntimeClassFlags<WinRt>, ITitleStorageFile, FtmBase> {
    InspectableClass(L"Microsoft.Xbox.TitleStorage.TitleStorageFile", BaseTrust);
    UINT64 xuid_;
    GUID group_;
    Wrappers::HString filename_, etag_, display_;
    ABI::Windows::Foundation::DateTime time_{};

public:
    TitleStorageFile(UINT64 xuid, GUID group, HSTRING path) : xuid_(xuid), group_(group) { filename_.Set(path); }
    HRESULT STDMETHODCALLTYPE get_TitleGroupId(GUID* v) override {
        if (!v) return E_POINTER;
        *v = group_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Xuid(UINT64* v) override {
        if (!v) return E_POINTER;
        *v = xuid_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Filename(HSTRING* v) override { return filename_.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_Size(UINT64* v) override {
        if (!v) return E_POINTER;
        *v = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Etag(HSTRING* v) override { return etag_.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE put_Etag(HSTRING v) override { return etag_.Set(v); }
    HRESULT STDMETHODCALLTYPE get_ClientFileTime(ABI::Windows::Foundation::DateTime* v) override {
        if (!v) return E_POINTER;
        *v = time_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_ClientFileTime(ABI::Windows::Foundation::DateTime v) override {
        time_ = v;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_DisplayName(HSTRING* v) override { return display_.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE put_DisplayName(HSTRING v) override { return display_.Set(v); }
    HRESULT STDMETHODCALLTYPE get_SmartBlobType(HSTRING* v) override {
        if (!v) return E_POINTER;
        *v = nullptr;
        return S_OK;
    }
};
class TitleFileFactory final : public Factory<ITitleStorageFileStatics> {
public:
    TitleFileFactory() : Factory(L"Microsoft.Xbox.TitleStorage.TitleStorageFile") {}
    HRESULT STDMETHODCALLTYPE GetUserFileFromPath(UINT64 xuid, GUID group, HSTRING path, IInspectable** v) override {
        if (!v) return E_POINTER;
        return Make<TitleStorageFile>(xuid, group, path).CopyTo(v);
    }
    HRESULT STDMETHODCALLTYPE GetGlobalFileFromPath(GUID group, HSTRING path, IInspectable** v) override {
        return GetUserFileFromPath(0, group, path, v);
    }
};
class TitleStorageFactory final : public Factory<ITitleStorageServiceStatics> {
    HRESULT Files(IInspectable** op) {
        const GUID iid = {0x7828ef7d, 0x938c, 0x4fc1, {0x83, 0xa4, 0x0d, 0x20, 0xa2, 0xbe, 0xf7, 0xa8}};
        return FailedClass("TitleStorage.GetFilesAsync", "Microsoft.Xbox.TitleStorage.TitleStorageFileCollection", iid,
                           op);
    }

public:
    TitleStorageFactory() : Factory(L"Microsoft.Xbox.TitleStorage.TitleStorageService") {}
    HRESULT STDMETHODCALLTYPE GetUserQuotaAsync(UINT64, GUID, IInspectable** op) override {
        const GUID iid = {0xe185f19d, 0x8a6d, 0x4c4f, {0xa0, 0xe4, 0x92, 0x58, 0x7c, 0xc8, 0x10, 0x65}};
        return FailedClass("TitleStorage.GetUserQuotaAsync", "Microsoft.Xbox.TitleStorage.UserTitleStorageQuota", iid,
                           op);
    }
    HRESULT STDMETHODCALLTYPE GetUserFilesAsync(UINT32, UINT32, UINT64, GUID, HSTRING, IInspectable*,
                                                IInspectable** op) override {
        return Files(op);
    }
    HRESULT STDMETHODCALLTYPE GetGlobalFilesAsync(UINT32, UINT32, GUID, HSTRING, IInspectable*,
                                                  IInspectable** op) override {
        return Files(op);
    }
    HRESULT STDMETHODCALLTYPE ReadToStreamAsync(IInspectable*, INT32, HSTRING, IInspectable** op) override {
        return MakeFailedOperation(
            ParameterizedIid(
                "pinterface({9fc2b0bb-e446-44e2-aa61-9cab8f636af2};{905a0fe1-bc53-11df-8c49-001e4fc686da})"),
            Offline, sizeof(void*), op);
    }
    HRESULT STDMETHODCALLTYPE WriteFromStreamAsync(IInspectable*, IInspectable*, INT32, UINT32,
                                                   IAsyncAction** op) override {
        return FailedAction("TitleStorage.WriteFromStreamAsync", op);
    }
    HRESULT STDMETHODCALLTYPE ReadToBufferAsync(IInspectable*, INT32, HSTRING, IInspectable** op) override {
        return MakeFailedOperation(
            ParameterizedIid(
                "pinterface({9fc2b0bb-e446-44e2-aa61-9cab8f636af2};{905a0fe0-bc53-11df-8c49-001e4fc686da})"),
            Offline, sizeof(void*), op);
    }
    HRESULT STDMETHODCALLTYPE WriteFromBufferAsync(IInspectable*, IInspectable*, INT32, UINT32,
                                                   IAsyncAction** op) override {
        return FailedAction("TitleStorage.WriteFromBufferAsync", op);
    }
    HRESULT STDMETHODCALLTYPE DeleteAsync(IInspectable*, INT32, IAsyncAction** op) override {
        return FailedAction("TitleStorage.DeleteAsync", op);
    }
};
template <class T> HRESULT Shared(IActivationFactory** value) {
    static auto factory = Make<T>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
HRESULT MakeXboxFactory(const wchar_t* name, IActivationFactory** value) {
    if (wcscmp(name, L"Microsoft.Xbox.Marketplace.MarketplaceService") == 0) return xbox::MakeMarketplaceFactory(value);
    if (wcscmp(name, L"Microsoft.Xbox.Marketplace.AssetBalance") == 0) return xbox::MakeAssetBalanceFactory(value);
    if (!value) return E_POINTER;
    *value = nullptr;
    SdkVersion(); // logs the stand-in once
    if (wcscmp(name, L"Microsoft.Xbox.TitleStorage.TitleStorageService") == 0)
        return Shared<TitleStorageFactory>(value);
    if (wcscmp(name, L"Microsoft.Xbox.TitleStorage.TitleStorageFile") == 0) return Shared<TitleFileFactory>(value);
    if (wcscmp(name, L"Microsoft.Xbox.XboxLIVEService") == 0) return Shared<LiveFactory>(value);
    if (wcscmp(name, L"Microsoft.Xbox.User") == 0) return Shared<UserFactory>(value);
    if (wcscmp(name, L"Microsoft.Xbox.Leaderboards.LeaderboardService") == 0) return Shared<LeaderboardFactory>(value);
    if (wcscmp(name, L"Microsoft.Xbox.ResourceLoader") == 0) return Shared<ResourceLoaderFactory>(value);
    Log("TRACE", "Xbox LIVE offline: %ls not provided", name);
    return REGDB_E_CLASSNOTREG; // never the real Microsoft.Xbox.dll
}
const ShimClass xboxClass{L"Microsoft.Xbox.", MakeXboxFactory, ShimPolicy::Replace, InIslands};
}
