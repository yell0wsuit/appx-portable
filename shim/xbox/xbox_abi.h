#pragma once
// The few Xbox LIVE SDK for Windows (Microsoft.Xbox.winmd) interfaces the
// offline stand-in implements, declared by hand from the SDK's metadata:
// exact IIDs and parameter lists, with every WinRT object parameter an
// IInspectable* (the same ABI) and enums as INT32. Parameterized results are
// returned as IInspectable* and built with MakeFailedOperation.
//
// SDK 1.0 and 1.2 share every IID, but IUser's methods differ between them
// under the same IID; both layouts are declared, and the stand-in picks one.
#include <windows.foundation.h>
namespace appx::xbox {
using ABI::Windows::Foundation::IAsyncAction;

MIDL_INTERFACE("4B00E95D-3006-4F02-BD96-3919D984D4C7")
IXboxLIVEService : public IInspectable {  // statics of Microsoft.Xbox.XboxLIVEService
    virtual HRESULT STDMETHODCALLTYPE SignInAsync(IInspectable** operation) = 0;  // IAsyncOperation<UserIdentity>
    virtual HRESULT STDMETHODCALLTYPE SignOutAsync(IAsyncAction** action) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_SignedOut(IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_SignedOut(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SignedInUserIdentity(IInspectable** identity) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ServiceClient(IInspectable** client) = 0;
    virtual HRESULT STDMETHODCALLTYPE InvalidateCacheGroup(INT32 group) = 0;
    virtual HRESULT STDMETHODCALLTYPE ShowGamesApplicationAsync(INT32 launchAction, IAsyncAction** action) = 0;
};

MIDL_INTERFACE("115CCD10-26C2-471C-B97B-5B3DAC160C6E")
IServiceClient : public IInspectable {  // Microsoft.Xbox.Foundation.ServiceClient
    virtual HRESULT STDMETHODCALLTYPE SignInAsync(IAsyncAction** action) = 0;
    virtual HRESULT STDMETHODCALLTYPE SignOutAsync(IAsyncAction** action) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_SignedOut(IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_SignedOut(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SignedInUserIdentity(IInspectable** identity) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Token(HSTRING* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TitleId(UINT32* titleId) = 0;
};

// Microsoft.Xbox.User, SDK 1.0 layout.
MIDL_INTERFACE("D7FBA214-E744-4093-9FEE-AB68795B70A0")
IUser10 : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Identity(IInspectable** identity) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(IInspectable** status) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProfileAsync(IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProfileAsync(IInspectable* profile, IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFriendsAsync(IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAchievementsAsync(UINT32 skip, UINT32 max, boolean unlockedOnly,
                                                           IInspectable* previousPage, IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnlockAchievementAsync(UINT32 achievementId, IAsyncAction** action) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAvatarManifestAsync(IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE PostUserMessageAsync(IInspectable* message, IAsyncAction** action) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetUserMessagesAsync(UINT32 skip, UINT32 max, IInspectable* previousPage,
                                                           IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetUserMessagesForTitleAsync(UINT32 titleId, UINT32 skip, UINT32 max,
                                                                   IInspectable* previousPage,
                                                                   IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteUserMessageAsync(HSTRING messageId, IAsyncAction** action) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateMultiplayerSessionAsync(IInspectable* settings, INT32 removableBy,
                                                                    IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE JoinMultiplayerSessionAsync(HSTRING sessionId, INT32 seat, IInspectable* data,
                                                                  IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMultiplayerSessionSummariesAsync(UINT32 titleId, GUID titleGroupId, INT32 mode,
                                                                          INT32 includeSessions, INT32 includeLastMessage,
                                                                          IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE CanPerformActionAsync(HSTRING action, UINT64 targetXuid,
                                                            IInspectable** operation) = 0;  // IAsyncOperation<Int32>
};

// Microsoft.Xbox.User, SDK 1.2 layout: SetProfileAsync removed,
// JoinMultiplayerSessionWithBufferAsync added, same IID.
MIDL_INTERFACE("D7FBA214-E744-4093-9FEE-AB68795B70A0")
IUser12 : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Identity(IInspectable** identity) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(IInspectable** status) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProfileAsync(IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFriendsAsync(IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAchievementsAsync(UINT32 skip, UINT32 max, boolean unlockedOnly,
                                                           IInspectable* previousPage, IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnlockAchievementAsync(UINT32 achievementId, IAsyncAction** action) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAvatarManifestAsync(IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE PostUserMessageAsync(IInspectable* message, IAsyncAction** action) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetUserMessagesAsync(UINT32 skip, UINT32 max, IInspectable* previousPage,
                                                           IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetUserMessagesForTitleAsync(UINT32 titleId, UINT32 skip, UINT32 max,
                                                                   IInspectable* previousPage,
                                                                   IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteUserMessageAsync(HSTRING messageId, IAsyncAction** action) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateMultiplayerSessionAsync(IInspectable* settings, INT32 removableBy,
                                                                    IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE JoinMultiplayerSessionAsync(HSTRING sessionId, INT32 seat, IInspectable* data,
                                                                  IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE JoinMultiplayerSessionWithBufferAsync(HSTRING sessionId, INT32 seat,
                                                                            IInspectable* buffer,
                                                                            IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMultiplayerSessionSummariesAsync(UINT32 titleId, GUID titleGroupId, INT32 mode,
                                                                          INT32 includeSessions, INT32 includeLastMessage,
                                                                          IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE CanPerformActionAsync(HSTRING action, UINT64 targetXuid,
                                                            IInspectable** operation) = 0;
};

MIDL_INTERFACE("EEBF3202-C74D-478F-B5A6-DEA04B7DCDD2")
IUserFactory : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateUser(UINT64 xuid, HSTRING gamertag, IInspectable** user) = 0;
};

MIDL_INTERFACE("70BE938F-0FB5-4CB8-BABA-C1F04F555D2A")
ILeaderboardService : public IInspectable {  // Microsoft.Xbox.Leaderboards.LeaderboardService
    virtual HRESULT STDMETHODCALLTYPE GetLeaderboardsAsync(IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLeaderboardAsync(UINT32 skip, UINT32 max, UINT32 leaderboardId,
                                                          boolean titleView, IInspectable* attributes,
                                                          IInspectable* previousPage, IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSystemLeaderboardAsync(UINT32 skip, UINT32 max, HSTRING name,
                                                                IInspectable* previousPage,
                                                                IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE PostResultAsync(UINT32 leaderboardId, INT32 aggregation, INT64 score,
                                                      IAsyncAction** action) = 0;
    virtual HRESULT STDMETHODCALLTYPE PostResultsAsync(UINT32 leaderboardId, IInspectable* attributes,
                                                       IAsyncAction** action) = 0;
};

MIDL_INTERFACE("555C64DC-3D58-4772-B14E-5824B6EF2CC3")
IResourceLoaderStatics : public IInspectable {  // statics of Microsoft.Xbox.ResourceLoader
    virtual HRESULT STDMETHODCALLTYPE GetStringById(INT32 id, HSTRING* value) = 0;
};

// Default interfaces of the runtime classes the async methods produce; their
// IIDs name the IAsyncOperation<T> instances.
namespace defaults {
constexpr GUID UserIdentity = {0xb4cac078, 0xf6c1, 0x48f2, {0xa5, 0x12, 0x3e, 0x79, 0xd1, 0xb2, 0xb6, 0xf7}};
constexpr GUID UserProfile = {0x10d2c627, 0xead4, 0x46aa, {0xb5, 0x2c, 0x36, 0xa9, 0x34, 0xdb, 0x2c, 0x29}};
constexpr GUID UserCollection = {0x3bae39a4, 0xa149, 0x49f6, {0xb2, 0xc1, 0xa3, 0xac, 0xf7, 0x97, 0x11, 0xa0}};
constexpr GUID AchievementCollection = {0xcd8ef6ea, 0xe325, 0x461d, {0x86, 0x9f, 0xcc, 0xe8, 0xbc, 0xa6, 0x86, 0x05}};
constexpr GUID AvatarManifest = {0x71773114, 0x2d4c, 0x4bad, {0x8d, 0xd7, 0x6a, 0xdf, 0x59, 0xee, 0x83, 0x35}};
constexpr GUID UserMessageCollection = {0xca4ff02c, 0xdedc, 0x4299, {0xa2, 0xf8, 0x8b, 0xd6, 0x58, 0xfe, 0x0f, 0xa1}};
constexpr GUID MultiplayerSession = {0x8a76cde3, 0x403f, 0x4be4, {0xb1, 0x37, 0x26, 0xb7, 0xdc, 0x70, 0x28, 0x05}};
constexpr GUID MultiplayerSessionSummaryCollection = {
    0xb006da2b, 0x950f, 0x4f84, {0xb9, 0x88, 0xac, 0x0e, 0x6d, 0x8e, 0xc3, 0x91}};
constexpr GUID Leaderboard = {0x8c0706ad, 0x0d21, 0x4a86, {0x88, 0x64, 0xc7, 0x17, 0x85, 0x49, 0x6d, 0xdc}};
constexpr GUID LeaderboardMetadataCollection = {
    0xf68290cc, 0x640f, 0x4724, {0x82, 0x55, 0x3f, 0xe2, 0x2c, 0xe0, 0xce, 0xf9}};
}
}
