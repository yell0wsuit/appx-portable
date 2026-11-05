// The packaged C# Facebook bridge cannot use CLRHost outside a package.
// Supply its exact ABI offline: no authentication or network requests, and
// requested operations complete through the game's callback with an error.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#else
#pragma warning(disable : 4996)
#endif
#include "stubs/facebook_abi.h"
#include "registry.h"
#include "com/factory.h"
#include "xaml/element.h"
#include "generated/controls.h"
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <mutex>
#include <string>
using namespace Microsoft::WRL;
namespace appx {
namespace {
using namespace facebook;
std::mutex appIdMutex;
std::wstring appId;
class FacebookHelper final : public RuntimeClass<RuntimeClassFlags<WinRt>, IFacebookHelperClass, FtmBase> {
    InspectableClass(L"FacebookRuntimeComponent.FacebookHelper", BaseTrust);
    std::mutex mutex_;
    ComPtr<IRequestFinished> callback_;
    HRESULT Offline() {
        ComPtr<IRequestFinished> callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = callback_;
        }
        Log("TRACE", "FacebookHelper request completed offline");
        // Retain the delegate and release the lock before invoking game code.
        return callback ? callback->Invoke(
                              true, Wrappers::HStringReference(L"{\"error\":{\"message\":\"Offline\"}}").Get(), false)
                        : S_OK;
    }

public:
    HRESULT STDMETHODCALLTYPE BackButtonPressed() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE CachePermissions(boolean triggerCallback) override {
        return triggerCallback ? Offline() : S_OK;
    }
    HRESULT STDMETHODCALLTYPE ComputeFacebookLoginUri(boolean, HSTRING,
                                                      ABI::Windows::Foundation::IUriRuntimeClass** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE);
    }
    HRESULT STDMETHODCALLTYPE DeleteScore() override { return Offline(); }
    HRESULT STDMETHODCALLTYPE ExtendPermissions(HSTRING) override { return Offline(); }
    HRESULT STDMETHODCALLTYPE GetAccessToken(HSTRING* value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAppID(HSTRING* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(appIdMutex);
        return WindowsCreateString(appId.data(), static_cast<UINT32>(appId.size()), value);
    }
    HRESULT STDMETHODCALLTYPE GetCoverImage() override { return Offline(); }
    HRESULT STDMETHODCALLTYPE GetFriendData(INT32, INT32, INT32) override { return Offline(); }
    HRESULT STDMETHODCALLTYPE GetFriends() override { return Offline(); }
    HRESULT STDMETHODCALLTYPE GetMyInfo() override { return Offline(); }
    HRESULT STDMETHODCALLTYPE GetUserLocale() override { return Offline(); }
    HRESULT STDMETHODCALLTYPE GetUsersData(HSTRING, INT32) override { return Offline(); }
    HRESULT STDMETHODCALLTYPE GetUsersNames(HSTRING) override { return Offline(); }
    HRESULT STDMETHODCALLTYPE HasPermission() override { return Offline(); }
    HRESULT STDMETHODCALLTYPE InitFacebookHelper(HSTRING, HSTRING, UINT32,
                                                 IRequestFinished* requestFinishedFunction) override {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = requestFinishedFunction;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsLoggedIn(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Login() override { return Offline(); }
    HRESULT STDMETHODCALLTYPE Logout(boolean) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE NavigationFailed(INT32) override { return Offline(); }
    HRESULT STDMETHODCALLTYPE NavigationStarting(ABI::Windows::Foundation::IUriRuntimeClass*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OpenGraphAction(HSTRING, HSTRING, HSTRING, HSTRING) override { return Offline(); }
    HRESULT STDMETHODCALLTYPE PostPhotoWoWall(ABI::Windows::Foundation::Collections::IVector<BYTE>*, HSTRING) override {
        return Offline();
    }
    HRESULT STDMETHODCALLTYPE PostToWall(boolean, HSTRING, HSTRING, HSTRING, HSTRING, HSTRING, HSTRING,
                                         HSTRING) override {
        return Offline();
    }
    HRESULT STDMETHODCALLTYPE RequestAppDetails() override { return Offline(); }
    HRESULT STDMETHODCALLTYPE RequestAppScore(HSTRING) override { return Offline(); }
    HRESULT STDMETHODCALLTYPE RequestGetUserAchievements(HSTRING) override { return Offline(); }
    HRESULT STDMETHODCALLTYPE RequestGotAchievement(HSTRING) override { return Offline(); }
    HRESULT STDMETHODCALLTYPE RequestPictureURL(HSTRING) override { return Offline(); }
    HRESULT STDMETHODCALLTYPE RequestResetAchievement(HSTRING) override { return Offline(); }
    HRESULT STDMETHODCALLTYPE RequestUserLikes(HSTRING) override { return Offline(); }
    HRESULT STDMETHODCALLTYPE RequestUserScore(HSTRING) override { return Offline(); }
    HRESULT STDMETHODCALLTYPE RetrieveUserID() override { return Offline(); }
    HRESULT STDMETHODCALLTYPE SendGameRequestToFriends(HSTRING, HSTRING) override { return Offline(); }
    HRESULT STDMETHODCALLTYPE UpdateScore(UINT32) override { return Offline(); }
    HRESULT STDMETHODCALLTYPE UploadVideo(HSTRING, HSTRING, HSTRING, HSTRING, HSTRING) override { return Offline(); }
};
class FacebookFactory final : public Factory<IFacebookHelperStatic> {
public:
    FacebookFactory() : Factory(L"FacebookRuntimeComponent.FacebookHelper") {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto helper = Make<FacebookHelper>();
        return helper ? helper.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE CancelLogin() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPermissionStatus(HSTRING, UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 2; // Unknown: the original bridge has no cached permission.
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPictureData(HSTRING, UINT32* size, BYTE** value) override {
        if (!size || !value) return E_POINTER;
        *size = 0;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetAppID(HSTRING value) override {
        std::lock_guard<std::mutex> lock(appIdMutex);
        appId = WindowsGetStringRawBuffer(value, nullptr);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPermissions(HSTRING) override { return S_OK; }
};
HRESULT MakeFacebookFactory(IActivationFactory** value) {
    static auto factory = Make<FacebookFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
class FacebookWebView final
    : public Element<FacebookWebView, IFacebookWebViewClass, ABI::Windows::UI::Xaml::Controls::DefaultIPage,
                     ABI::Windows::UI::Xaml::Controls::DefaultIContentControl,
                     ABI::Windows::UI::Xaml::Controls::DefaultIControl> {
    ComPtr<IFacebookHelperClass> helper_;

public:
    static constexpr const wchar_t* Name = L"FacebookRuntimeComponent.FacebookWebView";
    FacebookWebView(IInspectable* outer, IFacebookHelperClass* helper) : Element(outer), helper_(helper) {}
    HRESULT STDMETHODCALLTYPE GoToPage(ABI::Windows::Foundation::IUriRuntimeClass* uri) override {
        if (!uri) return E_INVALIDARG;
        // Report through the helper's existing offline error callback.
        return helper_->NavigationFailed(0);
    }
    HRESULT STDMETHODCALLTYPE HideBrowser() override { return S_OK; }
};
class FacebookWebViewFactory final : public Factory<IFacebookWebViewStatic, IFacebookWebViewFactory> {
public:
    FacebookWebViewFactory() : Factory(FacebookWebView::Name) {}
    HRESULT STDMETHODCALLTYPE SetGamePanel(ABI::Windows::UI::Xaml::Controls::IPanel* panel, HSTRING* errorMessage,
                                           boolean* result) override {
        if (!errorMessage || !result) return E_POINTER;
        *errorMessage = nullptr;
        *result = panel != nullptr;
        if (panel) return S_OK;
        return WindowsCreateString(L"Missing game panel", 18, errorMessage);
    }
    HRESULT STDMETHODCALLTYPE CreateFacebookWebView(IFacebookHelperClass* helper,
                                                    IFacebookWebViewClass** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!helper) return E_INVALIDARG;
        auto view = new (std::nothrow) FacebookWebView(nullptr, helper);
        if (!view) return E_OUTOFMEMORY;
        *value = static_cast<IFacebookWebViewClass*>(view);
        return S_OK;
    }
};
HRESULT MakeFacebookWebViewFactory(IActivationFactory** value) {
    static auto factory = Make<FacebookWebViewFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
const ShimClass facebookClass{L"FacebookRuntimeComponent.FacebookHelper", MakeFacebookFactory, ShimPolicy::Replace,
                              InIslands};
const ShimClass facebookWebViewClass{L"FacebookRuntimeComponent.FacebookWebView", MakeFacebookWebViewFactory,
                                     ShimPolicy::Replace, InIslands};
}
