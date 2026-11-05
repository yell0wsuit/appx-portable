#pragma once
// ABI from FacebookRuntimeComponent.winmd shipped with Minion Rush 4.1.4.1.
// Keep the metadata method order: C++/CX calls these slots directly.
#include <windows.foundation.h>
#include <windows.foundation.collections.h>
#include <windows.ui.xaml.controls.h>
namespace appx::facebook {
struct IFacebookHelperClass;
MIDL_INTERFACE("a66cf8d7-660c-532e-7202-cd6d3fd05fdb") IFacebookWebViewClass : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GoToPage(ABI::Windows::Foundation::IUriRuntimeClass * uri) = 0;
    virtual HRESULT STDMETHODCALLTYPE HideBrowser() = 0;
};
MIDL_INTERFACE("7545e2c9-8ece-59d3-622c-3690a30c66c5") IFacebookWebViewStatic : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE SetGamePanel(ABI::Windows::UI::Xaml::Controls::IPanel * panel,
                                                   HSTRING * errorMessage, boolean * result) = 0;
};
MIDL_INTERFACE("aee719b7-71ea-5638-7b19-261492e4f530") IFacebookWebViewFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateFacebookWebView(IFacebookHelperClass * helper,
                                                            IFacebookWebViewClass * *value) = 0;
};
MIDL_INTERFACE("cc66efa9-ed02-5572-4963-d67feac8252c") IRequestFinished : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(boolean errorOccurred, HSTRING response, boolean userCanceled) = 0;
};
MIDL_INTERFACE("05e201ee-9209-5357-4b3c-39b4ffe918ae") IFacebookHelperStatic : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CancelLogin() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPermissionStatus(HSTRING permission, UINT32 * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPictureData(HSTRING urlString, UINT32 * size, BYTE * *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetAppID(HSTRING newAppID) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPermissions(HSTRING newPermissions) = 0;
};
MIDL_INTERFACE("bd86a2fc-2ffa-56c1-5a6d-3becdb700f7a") IFacebookHelperClass : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE BackButtonPressed() = 0;
    virtual HRESULT STDMETHODCALLTYPE CachePermissions(boolean triggerCallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE ComputeFacebookLoginUri(boolean reaskPermissions, HSTRING permission,
                                                              ABI::Windows::Foundation::IUriRuntimeClass * *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteScore() = 0;
    virtual HRESULT STDMETHODCALLTYPE ExtendPermissions(HSTRING newPermissions) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAccessToken(HSTRING * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAppID(HSTRING * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCoverImage() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFriendData(INT32 offset, INT32 limit, INT32 imageSize) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFriends() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMyInfo() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetUserLocale() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetUsersData(HSTRING uids, INT32 imageSize) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetUsersNames(HSTRING uids) = 0;
    virtual HRESULT STDMETHODCALLTYPE HasPermission() = 0;
    virtual HRESULT STDMETHODCALLTYPE InitFacebookHelper(HSTRING permissions, HSTRING canvasUrl, UINT32 apiVersion,
                                                         IRequestFinished * requestFinishedFunction) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsLoggedIn(boolean * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Login() = 0;
    virtual HRESULT STDMETHODCALLTYPE Logout(boolean clearCookies) = 0;
    virtual HRESULT STDMETHODCALLTYPE NavigationFailed(INT32 status) = 0;
    virtual HRESULT STDMETHODCALLTYPE NavigationStarting(ABI::Windows::Foundation::IUriRuntimeClass * uri) = 0;
    virtual HRESULT STDMETHODCALLTYPE OpenGraphAction(HSTRING appNamespace, HSTRING action, HSTRING objectUrl,
                                                      HSTRING objectType) = 0;
    virtual HRESULT STDMETHODCALLTYPE PostPhotoWoWall(ABI::Windows::Foundation::Collections::IVector<BYTE> * photo,
                                                      HSTRING msg) = 0;
    virtual HRESULT STDMETHODCALLTYPE PostToWall(boolean withDialog, HSTRING msg, HSTRING link, HSTRING title,
                                                 HSTRING pictureUrl, HSTRING description, HSTRING actionLabel,
                                                 HSTRING gameName) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestAppDetails() = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestAppScore(HSTRING appId) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestGetUserAchievements(HSTRING userId) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestGotAchievement(HSTRING achievement) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestPictureURL(HSTRING avatarSize) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestResetAchievement(HSTRING achievement) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestUserLikes(HSTRING userId) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestUserScore(HSTRING userId) = 0;
    virtual HRESULT STDMETHODCALLTYPE RetrieveUserID() = 0;
    virtual HRESULT STDMETHODCALLTYPE SendGameRequestToFriends(HSTRING uids, HSTRING message) = 0;
    virtual HRESULT STDMETHODCALLTYPE UpdateScore(UINT32 score) = 0;
    virtual HRESULT STDMETHODCALLTYPE UploadVideo(HSTRING path, HSTRING title, HSTRING description, HSTRING contentType,
                                                  HSTRING privacy) = 0;
};
}
