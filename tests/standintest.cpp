// Stand-ins for classes Windows cannot serve outside a package: parameterized
// IIDs, failed async operations, the offline Xbox LIVE SDK, HttpClientCSharp,
// NotificationsExtensions tiles, GoogleAnalytics,
// Package.Current, and CoreWindow.GetForCurrentThread off the UI thread.
#include "com/pinterface.h"
#include "xbox/xbox_abi.h"
#include "xbox/achievements_abi.h"
#include <windows.applicationmodel.h>
#include <windows.applicationmodel.background.h>
#include <windows.ui.core.h>
#include <windows.ui.notifications.h>
#include <windows.storage.streams.h>
#include <roapi.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <atomic>
#include <cstdio>
#include <string>
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
    static HRESULT STDMETHODCALLTYPE Invoke(Handler* self, IInspectable*,
                                            ABI::Windows::Foundation::AsyncStatus status) {
        self->status = status;
        ++self->calls;
        return S_OK;
    }
};
static void* handlerTable[] = {reinterpret_cast<void*>(&Handler::QueryInterface),
                               reinterpret_cast<void*>(&Handler::AddRef), reinterpret_cast<void*>(&Handler::Release),
                               reinterpret_cast<void*>(&Handler::Invoke)};
struct OperationAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(void* results) = 0;
};

int main(int argc, char**) {
    RoInitialize(RO_INIT_MULTITHREADED);
    // Reference IIDs: IAsyncOperation<Int32> from the Windows SDK, and two Xbox
    // instances from midl's output for Microsoft.Xbox.winmd.
    auto iid = appx::ParameterizedIid("pinterface({9fc2b0bb-e446-44e2-aa61-9cab8f636af2};i4)");
    CHECK(appx::GuidSignature(iid) == "{968b9665-06ed-5774-8f53-8edeabd5f7b5}");
    iid = appx::ParameterizedIid(
        appx::AsyncOperationOfClass("Microsoft.Xbox.UserProfile", appx::xbox::defaults::UserProfile));
    CHECK(appx::GuidSignature(iid) == "{31961233-ebde-54d4-841a-6d8a0eb239bf}");
    const auto identityOperation = appx::ParameterizedIid(
        appx::AsyncOperationOfClass("Microsoft.Xbox.Foundation.UserIdentity", appx::xbox::defaults::UserIdentity));
    CHECK(appx::GuidSignature(identityOperation) == "{3073e088-9b68-5869-9991-4271e9dab42b}");

    auto* dll = LoadLibraryW(L"appxportable.dll");
    if (!dll) return 1;
    get = reinterpret_cast<GetFactoryCall>(GetProcAddress(dll, "ShimGetFactory"));
    if (!get) return 2;

    // Xbox LIVE: a local player is signed in from the start, as Windows 8 signed
    // in the Microsoft account. Sign-in completes with that player through its
    // exact operation type; ilomilo plus waits for it at its loading screen.
    ComPtr<IActivationFactory> factory;
    ComPtr<appx::xbox::IXboxLIVEService> live;
    CHECK(SUCCEEDED(Factory(L"Microsoft.Xbox.XboxLIVEService", factory)) && SUCCEEDED(factory.As(&live)));
    if (live) {
        ComPtr<IInspectable> identity, signIn;
        CHECK(SUCCEEDED(live->get_SignedInUserIdentity(&identity)) && identity);
        CHECK(SUCCEEDED(live->SignInAsync(&signIn)) && signIn);
        ComPtr<IUnknown> typed;
        CHECK(signIn &&
              SUCCEEDED(signIn->QueryInterface(identityOperation, reinterpret_cast<void**>(typed.GetAddressOf()))));
        ComPtr<ABI::Windows::Foundation::IAsyncInfo> info;
        ABI::Windows::Foundation::AsyncStatus status{};
        HRESULT error = E_FAIL;
        CHECK(signIn && SUCCEEDED(signIn.As(&info)) && SUCCEEDED(info->get_Status(&status)) &&
              status == ABI::Windows::Foundation::AsyncStatus::Completed);
        CHECK(info && SUCCEEDED(info->get_ErrorCode(&error)) && error == S_OK);
        if (typed) {
            Handler handler{handlerTable};
            auto* operation = static_cast<OperationAbi*>(static_cast<IInspectable*>(typed.Get()));
            CHECK(SUCCEEDED(operation->put_Completed(reinterpret_cast<IUnknown*>(&handler))));
            CHECK(handler.calls == 1 && handler.status == ABI::Windows::Foundation::AsyncStatus::Completed);
            ComPtr<appx::xbox::IUserIdentity> player;
            HString gamertag;
            CHECK(SUCCEEDED(operation->GetResults(player.GetAddressOf())) && player);
            CHECK(player && SUCCEEDED(player->get_Gamertag(gamertag.GetAddressOf())) &&
                  wcscmp(gamertag.GetRawBuffer(nullptr), L"Local Player") == 0);
        }
        ComPtr<IInspectable> client;
        CHECK(SUCCEEDED(live->get_ServiceClient(&client)) && client);
    }
    CHECK(SUCCEEDED(Factory(L"Microsoft.Xbox.User", factory)));
    if (factory) {
        ComPtr<IInspectable> instance;
        CHECK(SUCCEEDED(factory->ActivateInstance(&instance)));
        auto checkUser = [&](auto* user) {
            ComPtr<IInspectable> friends;
            CHECK(user && SUCCEEDED(user->GetFriendsAsync(&friends)) && friends);
            // Exercise both SDK layouts: SetProfileAsync precedes this slot
            // only in 1.0/1.1. A wrong layout also produces the wrong async IID.
            ComPtr<IInspectable> achievements;
            CHECK(user && SUCCEEDED(user->GetAchievementsAsync(0, 100, false, nullptr, &achievements)) && achievements);
            ComPtr<IUnknown> typed;
            const auto achievementOperation = appx::ParameterizedIid(appx::AsyncOperationOfClass(
                "Microsoft.Xbox.AchievementCollection", appx::xbox::defaults::AchievementCollection));
            CHECK(achievements && SUCCEEDED(achievements->QueryInterface(achievementOperation, &typed)));
            ComPtr<ABI::Windows::Foundation::IAsyncInfo> info;
            CHECK(achievements && SUCCEEDED(achievements.As(&info)));
            HRESULT error{};
            CHECK(info && SUCCEEDED(info->get_ErrorCode(&error)) && error == S_OK);
            ABI::Windows::Foundation::AsyncStatus achievementStatus{};
            CHECK(info && SUCCEEDED(info->get_Status(&achievementStatus)) &&
                  achievementStatus == ABI::Windows::Foundation::AsyncStatus::Completed);
            ComPtr<appx::xbox::IAchievementCollection> collection;
            auto* achievementAsync = static_cast<OperationAbi*>(static_cast<IInspectable*>(typed.Get()));
            CHECK(achievementAsync && SUCCEEDED(achievementAsync->GetResults(collection.GetAddressOf())) && collection);
            Handler achievementHandler{handlerTable};
            CHECK(achievementAsync &&
                  SUCCEEDED(achievementAsync->put_Completed(reinterpret_cast<IUnknown*>(&achievementHandler))));
            CHECK(achievementHandler.calls == 1 &&
                  achievementHandler.status == ABI::Windows::Foundation::AsyncStatus::Completed);
            UINT32 count = 99;
            CHECK(collection && SUCCEEDED(collection->get_TotalRecords(&count)) && count == 0);
            ComPtr<IInspectable> items;
            ComPtr<appx::xbox::IAchievementVectorView> view;
            ComPtr<appx::xbox::IAchievementIterable> iterable;
            CHECK(collection && SUCCEEDED(collection->get_Items(&items)) && items && SUCCEEDED(items.As(&view)) &&
                  SUCCEEDED(items.As(&iterable)));
            CHECK(view && SUCCEEDED(view->get_Size(&count)) && count == 0);
            ComPtr<IInspectable> missing;
            CHECK(view && view->GetAt(0, &missing) == E_BOUNDS && !missing);
            CHECK(view && view->GetAt(0, nullptr) == E_POINTER);
            CHECK(view && SUCCEEDED(view->GetMany(0, 0, nullptr, &count)) && count == 0);
            CHECK(view && view->GetMany(1, 0, nullptr, &count) == E_BOUNDS);
            ComPtr<appx::xbox::IAchievementIterator> iterator;
            boolean hasCurrent = true;
            CHECK(iterable && SUCCEEDED(iterable->First(&iterator)) && iterator);
            CHECK(iterator && SUCCEEDED(iterator->get_HasCurrent(&hasCurrent)) && !hasCurrent);
            CHECK(iterator && SUCCEEDED(iterator->MoveNext(&hasCurrent)) && !hasCurrent);
            CHECK(iterator && iterator->get_Current(&missing) == E_BOUNDS && !missing);
            const std::string achievementClass =
                "rc(Microsoft.Xbox.Achievement;{843a65e0-8ed2-45a3-8524-6983a9ad7fdd})";
            CHECK(appx::ParameterizedIid("pinterface({bbe1fa4c-b0e3-4583-baef-1f1b2e483e56};" + achievementClass +
                                         ")") == __uuidof(appx::xbox::IAchievementVectorView));
            CHECK(appx::ParameterizedIid("pinterface({faa585ea-6214-4217-afda-7f46de5869b3};" + achievementClass +
                                         ")") == __uuidof(appx::xbox::IAchievementIterable));
            CHECK(appx::ParameterizedIid("pinterface({6a79e863-4300-459a-9966-cbb660963ee1};" + achievementClass +
                                         ")") == __uuidof(appx::xbox::IAchievementIterator));
            // The local player's profile loads offline.
            ComPtr<IInspectable> profile;
            ComPtr<appx::xbox::IUserProfile> loaded;
            CHECK(user && SUCCEEDED(user->GetProfileAsync(&profile)) && profile);
            const auto profileOperation = appx::ParameterizedIid(
                appx::AsyncOperationOfClass("Microsoft.Xbox.UserProfile", appx::xbox::defaults::UserProfile));
            CHECK(profile && SUCCEEDED(profile->QueryInterface(
                                 profileOperation, reinterpret_cast<void**>(typed.ReleaseAndGetAddressOf()))));
            if (typed)
                CHECK(SUCCEEDED(static_cast<OperationAbi*>(static_cast<IInspectable*>(typed.Get()))
                                    ->GetResults(loaded.GetAddressOf())) &&
                      loaded);
            // Signed-out requests retain the normal offline error path.
            ComPtr<ABI::Windows::Foundation::IAsyncAction> signOut;
            CHECK(live && SUCCEEDED(live->SignOutAsync(&signOut)));
            ComPtr<IInspectable> signedOutAchievements, signIn;
            CHECK(SUCCEEDED(user->GetAchievementsAsync(0, 100, false, nullptr, &signedOutAchievements)));
            CHECK(signedOutAchievements && SUCCEEDED(signedOutAchievements.As(&info)) &&
                  SUCCEEDED(info->get_ErrorCode(&error)) && error == HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE));
            CHECK(live && SUCCEEDED(live->SignInAsync(&signIn)));
        };
        if (argc > 1) {
            ComPtr<appx::xbox::IUser12> user;
            CHECK(SUCCEEDED(instance.As(&user)));
            checkUser(user.Get());
            ComPtr<IInspectable> operation;
            CHECK(user && SUCCEEDED(user->JoinMultiplayerSessionWithBufferAsync(nullptr, 0, nullptr, &operation)) &&
                  operation);
        } else {
            ComPtr<appx::xbox::IUser10> user;
            CHECK(SUCCEEDED(instance.As(&user)));
            checkUser(user.Get());
        }
    }
    // Xbox classes without a stand-in are "not registered", never the real DLL.
    // Marketplace activation must work even with StubIap enabled. Xbox purchases
    // fail through their async callback, which iStunt 2 handles, instead of
    // throwing an uncaught TypeLoadException at ShowPurchaseAsync.
    ComPtr<appx::xbox::IMarketplaceStatics> marketplace;
    CHECK(SUCCEEDED(Factory(L"Microsoft.Xbox.Marketplace.MarketplaceService", factory)) &&
          SUCCEEDED(factory.As(&marketplace)));
    if (marketplace) {
        const GUID offer = {0x12345678, 0x1234, 0x5678, {1, 2, 3, 4, 5, 6, 7, 8}};
        ComPtr<ABI::Windows::Foundation::IAsyncAction> purchase;
        CHECK(marketplace->ShowPurchaseAsync(offer, nullptr) == E_POINTER);
        CHECK(SUCCEEDED(marketplace->ShowPurchaseAsync(offer, &purchase)) && purchase);
        if (purchase) {
            ComPtr<ABI::Windows::Foundation::IAsyncInfo> info;
            ABI::Windows::Foundation::AsyncStatus status{};
            HRESULT error{};
            CHECK(SUCCEEDED(purchase.As(&info)) && SUCCEEDED(info->get_Status(&status)) &&
                  status == ABI::Windows::Foundation::AsyncStatus::Error);
            CHECK(info && SUCCEEDED(info->get_ErrorCode(&error)) &&
                  error == HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE));
            Handler handler{handlerTable};
            CHECK(SUCCEEDED(purchase->put_Completed(
                reinterpret_cast<ABI::Windows::Foundation::IAsyncActionCompletedHandler*>(&handler))));
            CHECK(handler.calls == 1 && handler.status == ABI::Windows::Foundation::AsyncStatus::Error);
            CHECK(purchase->GetResults() == HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE));
            CHECK(info && SUCCEEDED(info->Close()));
        }
        auto checkMarketplaceResult = [&](HRESULT hr, ComPtr<IInspectable>& operation, const char* runtimeClass,
                                          const GUID& defaultInterface) {
            CHECK(SUCCEEDED(hr) && operation);
            ComPtr<IUnknown> typed;
            const auto iid = appx::ParameterizedIid(appx::AsyncOperationOfClass(runtimeClass, defaultInterface));
            CHECK(operation && SUCCEEDED(operation->QueryInterface(iid, &typed)));
            if (typed) {
                void* result = reinterpret_cast<void*>(1);
                CHECK(static_cast<OperationAbi*>(static_cast<IInspectable*>(typed.Get()))->GetResults(&result) ==
                      HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE));
                CHECK(!result);
            }
            operation.Reset();
        };
        ComPtr<IInspectable> operation;
        auto hr = marketplace->EnumerateOffersByOfferIdsAsync(nullptr, &operation);
        checkMarketplaceResult(hr, operation, "Microsoft.Xbox.Marketplace.OfferCollection",
                               appx::xbox::defaults::OfferCollection);
        hr = marketplace->EnumerateOffersAsync(0, 0, 0, 0, 0, 0, false, &operation);
        checkMarketplaceResult(hr, operation, "Microsoft.Xbox.Marketplace.OfferCollection",
                               appx::xbox::defaults::OfferCollection);
        hr = marketplace->GetReceiptsAsync(0, 0, 0, nullptr, &operation);
        checkMarketplaceResult(hr, operation, "Microsoft.Xbox.Marketplace.ReceiptCollection",
                               appx::xbox::defaults::ReceiptCollection);
        hr = marketplace->GetReceiptsWithDateFilterAsync(0, 0, 0, {}, nullptr, &operation);
        checkMarketplaceResult(hr, operation, "Microsoft.Xbox.Marketplace.ReceiptCollection",
                               appx::xbox::defaults::ReceiptCollection);
        hr = marketplace->GetReceiptAsync(0, offer, &operation);
        checkMarketplaceResult(hr, operation, "Microsoft.Xbox.Marketplace.Receipt", appx::xbox::defaults::Receipt);
        hr = marketplace->GetAssetsAsync(0, 0, 0, nullptr, &operation);
        checkMarketplaceResult(hr, operation, "Microsoft.Xbox.Marketplace.AssetBalanceCollection",
                               appx::xbox::defaults::AssetBalanceCollection);
        ComPtr<ABI::Windows::Foundation::IAsyncAction> consume;
        CHECK(SUCCEEDED(marketplace->ConsumeAssetsAsync(0, nullptr, &consume)) && consume &&
              consume->GetResults() == HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE));
        boolean retype = false;
        CHECK(SUCCEEDED(marketplace->put_RetypeCredentialsAtPurchase(true)));
        CHECK(SUCCEEDED(marketplace->get_RetypeCredentialsAtPurchase(&retype)) && retype);
        ComPtr<ABI::Windows::Foundation::IAsyncAction> setting;
        CHECK(SUCCEEDED(marketplace->SetRetypeCredentialsAtPurchaseAsync(false, &setting)) && setting &&
              SUCCEEDED(setting->GetResults()));
        CHECK(SUCCEEDED(marketplace->get_RetypeCredentialsAtPurchase(&retype)) && !retype);
    }
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
            CHECK(SUCCEEDED(reinterpret_cast<Success>((*reinterpret_cast<void***>(response.Get()))[6])(response.Get(),
                                                                                                       &success)) &&
                  !success);
        }
    }

    // NotificationsExtensions tile content, as Asphalt 7: Heat builds its live
    // tile: wide and square image templates, combined into one notification.
    CHECK(SUCCEEDED(Factory(L"NotificationsExtensions.TileContent.TileContentFactory", factory)));
    if (factory) {
        auto slot = [](IInspectable* object, int index) { return (*reinterpret_cast<void***>(object))[index]; };
        using Get = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, IInspectable**);
        using PutText = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, HSTRING);
        using PutObject = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, IInspectable*);
        using PutFlag = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, boolean);
        using GetText = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, HSTRING*);
        const GUID statics = {0xd77b1b1e, 0x207f, 0x536b, {0x7d, 0x0a, 0x91, 0xe9, 0xcd, 0xdb, 0x8c, 0xd5}};
        const GUID wideContent = {0xb803960c, 0x2950, 0x54e8, {0x49, 0xc7, 0x4b, 0x00, 0x33, 0x74, 0x2a, 0xe4}};
        const GUID squareContent = {0x2537e592, 0x94ff, 0x5dcc, {0x62, 0x79, 0x03, 0x66, 0xc5, 0x49, 0x98, 0xc5}};
        const GUID tileContent = {0x4f5a7235, 0x6bb2, 0x553d, {0x40, 0x00, 0x4a, 0x58, 0x56, 0x2b, 0x70, 0x72}};
        const GUID content = {0x2c0d137a, 0x1fc8, 0x5db6, {0x4c, 0x9a, 0x58, 0x02, 0x21, 0xee, 0xf1, 0xf4}};
        ComPtr<IInspectable> creator, wide, square;
        CHECK(SUCCEEDED(factory->QueryInterface(statics, &creator)));
        // ITileContentFactoryStatic: CreateTileSquareImage is slot 7, CreateTileWideImage 18.
        CHECK(creator && SUCCEEDED(reinterpret_cast<Get>(slot(creator.Get(), 18))(creator.Get(), &wide)) && wide);
        CHECK(creator && SUCCEEDED(reinterpret_cast<Get>(slot(creator.Get(), 7))(creator.Get(), &square)) && square);
        // ITileWideImage/ITileSquareImage.Image is slot 6; its Src slot 7, Alt 9.
        for (auto* tile : {wide.Get(), square.Get()}) {
            ComPtr<IInspectable> image;
            CHECK(tile && SUCCEEDED(reinterpret_cast<Get>(slot(tile, 6))(tile, &image)) && image);
            if (!image) continue;
            CHECK(SUCCEEDED(reinterpret_cast<PutText>(slot(image.Get(), 7))(
                image.Get(),
                HStringReference(tile == wide.Get() ? L"ms-appx:///wide.png" : L"ms-appx:///sq.png").Get())));
            CHECK(SUCCEEDED(
                reinterpret_cast<PutText>(slot(image.Get(), 9))(image.Get(), HStringReference(L"A&7").Get())));
        }
        ComPtr<IInspectable> wideAs, squareAs, notificationContent, xmlContent;
        CHECK(wide && SUCCEEDED(wide->QueryInterface(wideContent, &wideAs)));
        CHECK(square && SUCCEEDED(square->QueryInterface(squareContent, &squareAs)));
        CHECK(wide && FAILED(wide->QueryInterface(squareContent, &xmlContent)) && !xmlContent);
        // IWideTileNotificationContent: SquareContent is slot 7, RequireSquareContent 9.
        CHECK(wideAs && SUCCEEDED(reinterpret_cast<PutObject>(slot(wideAs.Get(), 7))(wideAs.Get(), squareAs.Get())));
        CHECK(wideAs && SUCCEEDED(reinterpret_cast<PutFlag>(slot(wideAs.Get(), 9))(wideAs.Get(), true)));
        // INotificationContent.GetContent (slot 6) holds both bindings.
        HString xml;
        CHECK(wide && SUCCEEDED(wide->QueryInterface(content, &xmlContent)));
        CHECK(xmlContent &&
              SUCCEEDED(reinterpret_cast<GetText>(slot(xmlContent.Get(), 6))(xmlContent.Get(), xml.GetAddressOf())));
        const std::wstring text = xml.GetRawBuffer(nullptr);
        CHECK(text.find(L"template=\"TileWideImage\"") != std::wstring::npos &&
              text.find(L"template=\"TileSquareImage\"") != std::wstring::npos &&
              text.find(L"src=\"ms-appx:///wide.png\"") != std::wstring::npos &&
              text.find(L"A&amp;7") != std::wstring::npos);
        // ITileNotificationContent.CreateNotification is slot 16; the game sets the Tag.
        ComPtr<IInspectable> created;
        CHECK(wide && SUCCEEDED(wide->QueryInterface(tileContent, &notificationContent)));
        CHECK(notificationContent &&
              SUCCEEDED(
                  reinterpret_cast<Get>(slot(notificationContent.Get(), 16))(notificationContent.Get(), &created)) &&
              created);
        ComPtr<ABI::Windows::UI::Notifications::ITileNotification> notification;
        CHECK(created && SUCCEEDED(created.As(&notification)));
        HString tag;
        CHECK(notification && SUCCEEDED(notification->put_Tag(HStringReference(L"race").Get())) &&
              SUCCEEDED(notification->get_Tag(tag.GetAddressOf())) && wcscmp(tag.GetRawBuffer(nullptr), L"race") == 0);
        // ilomilo plus: TileWideImageAndText02 (slot 20), an image and two captions.
        ComPtr<IInspectable> captioned;
        CHECK(creator && SUCCEEDED(reinterpret_cast<Get>(slot(creator.Get(), 20))(creator.Get(), &captioned)) &&
              captioned);
        if (captioned) {
            // ITileWideImageAndText02: Image slot 6, TextCaption1 7, TextCaption2 8; Text's put_Text is slot 7.
            ComPtr<IInspectable> image, first, second, captionedContent;
            CHECK(SUCCEEDED(reinterpret_cast<Get>(slot(captioned.Get(), 6))(captioned.Get(), &image)) && image);
            CHECK(SUCCEEDED(reinterpret_cast<Get>(slot(captioned.Get(), 7))(captioned.Get(), &first)) && first);
            CHECK(SUCCEEDED(reinterpret_cast<Get>(slot(captioned.Get(), 8))(captioned.Get(), &second)) && second);
            if (image)
                CHECK(SUCCEEDED(reinterpret_cast<PutText>(slot(image.Get(), 7))(
                    image.Get(), HStringReference(L"ms-appx:///level.png").Get())));
            if (first)
                CHECK(SUCCEEDED(
                    reinterpret_cast<PutText>(slot(first.Get(), 7))(first.Get(), HStringReference(L"Safka").Get())));
            if (second)
                CHECK(SUCCEEDED(
                    reinterpret_cast<PutText>(slot(second.Get(), 7))(second.Get(), HStringReference(L"1 < 2").Get())));
            HString captionedXml;
            CHECK(SUCCEEDED(captioned->QueryInterface(content, &captionedContent)) &&
                  SUCCEEDED(reinterpret_cast<GetText>(slot(captionedContent.Get(), 6))(captionedContent.Get(),
                                                                                       captionedXml.GetAddressOf())));
            const std::wstring body = captionedXml.GetRawBuffer(nullptr);
            CHECK(body.find(L"template=\"TileWideImageAndText02\"><image id=\"1\" src=\"ms-appx:///level.png\"/>"
                            L"<text id=\"1\">Safka</text><text id=\"2\">1 &lt; 2</text></binding>") !=
                  std::wstring::npos);
            ComPtr<IInspectable> captionedWide;
            CHECK(SUCCEEDED(captioned->QueryInterface(wideContent, &captionedWide)));
            // Its square tile is TileSquareText04 (slot 15): TextBodyWrap is slot 6.
            ComPtr<IInspectable> wrapped, bodyText, wrappedSquare;
            CHECK(creator && SUCCEEDED(reinterpret_cast<Get>(slot(creator.Get(), 15))(creator.Get(), &wrapped)) &&
                  wrapped);
            if (wrapped) {
                CHECK(SUCCEEDED(reinterpret_cast<Get>(slot(wrapped.Get(), 6))(wrapped.Get(), &bodyText)) && bodyText);
                if (bodyText)
                    CHECK(SUCCEEDED(reinterpret_cast<PutText>(slot(bodyText.Get(), 7))(
                        bodyText.Get(), HStringReference(L"World 1 & 2").Get())));
                CHECK(SUCCEEDED(wrapped->QueryInterface(squareContent, &wrappedSquare)));
                CHECK(captionedWide && wrappedSquare &&
                      SUCCEEDED(reinterpret_cast<PutObject>(slot(captionedWide.Get(), 7))(captionedWide.Get(),
                                                                                          wrappedSquare.Get())));
                HString both;
                CHECK(captionedContent && SUCCEEDED(reinterpret_cast<GetText>(slot(captionedContent.Get(), 6))(
                                              captionedContent.Get(), both.GetAddressOf())));
                CHECK(std::wstring(both.GetRawBuffer(nullptr))
                          .find(L"<binding template=\"TileSquareText04\"><text id=\"1\">World 1 &amp; 2</text>"
                                L"</binding></visual></tile>") != std::wstring::npos);
            }
        }
        // Every Windows 8 template (slots 6 to 50) is stood in.
        for (int index = 6; creator && index <= 50; ++index) {
            ComPtr<IInspectable> made;
            CHECK(SUCCEEDED(reinterpret_cast<Get>(slot(creator.Get(), index))(creator.Get(), &made)) && made);
        }
        // TileWideText01 (slot 41): a heading and four lines, each a text field from slot 6.
        ComPtr<IInspectable> text01, heading, line, text01Content;
        CHECK(creator && SUCCEEDED(reinterpret_cast<Get>(slot(creator.Get(), 41))(creator.Get(), &text01)) && text01);
        if (text01) {
            HString className, text01Xml;
            CHECK(SUCCEEDED(text01->GetRuntimeClassName(className.GetAddressOf())) &&
                  wcscmp(className.GetRawBuffer(nullptr), L"NotificationsExtensions.TileContent.TileWideText01") == 0);
            CHECK(SUCCEEDED(reinterpret_cast<Get>(slot(text01.Get(), 6))(text01.Get(), &heading)) && heading);
            CHECK(SUCCEEDED(reinterpret_cast<Get>(slot(text01.Get(), 10))(text01.Get(), &line)) && line);
            ComPtr<IInspectable> beyond;
            CHECK(FAILED(reinterpret_cast<Get>(slot(text01.Get(), 11))(text01.Get(), &beyond)) && !beyond);
            if (heading)
                CHECK(SUCCEEDED(reinterpret_cast<PutText>(slot(heading.Get(), 7))(heading.Get(),
                                                                                  HStringReference(L"Scores").Get())));
            if (line)
                CHECK(SUCCEEDED(
                    reinterpret_cast<PutText>(slot(line.Get(), 7))(line.Get(), HStringReference(L"Safka 3").Get())));
            CHECK(SUCCEEDED(text01->QueryInterface(content, &text01Content)) &&
                  SUCCEEDED(reinterpret_cast<GetText>(slot(text01Content.Get(), 6))(text01Content.Get(),
                                                                                    text01Xml.GetAddressOf())));
            CHECK(std::wstring(text01Xml.GetRawBuffer(nullptr))
                      .find(L"<binding template=\"TileWideText01\"><text id=\"1\">Scores</text><text id=\"2\"></text>"
                            L"<text id=\"3\"></text><text id=\"4\"></text><text id=\"5\">Safka 3</text></binding>") !=
                  std::wstring::npos);
        }
        // A template's known Windows 8 IID answers with the pointer the factory returned.
        const GUID wideImage = {0x4dca931b, 0x2d24, 0x5435, {0x78, 0xf2, 0x49, 0xf4, 0xc6, 0x84, 0x9a, 0x0d}};
        ComPtr<IInspectable> asWideImage;
        CHECK(wide && SUCCEEDED(wide->QueryInterface(wideImage, &asWideImage)) && asWideImage.Get() == wide.Get());
    }

    // Outside a package there are no background tasks to enumerate or cancel.
    CHECK(SUCCEEDED(Factory(L"Windows.ApplicationModel.Background.BackgroundTaskRegistration", factory)));
    if (factory) {
        using namespace ABI::Windows::ApplicationModel::Background;
        ComPtr<IBackgroundTaskRegistrationStatics> background;
        ComPtr<__FIMapView_2_GUID_Windows__CApplicationModel__CBackground__CIBackgroundTaskRegistration> tasks;
        CHECK(SUCCEEDED(factory.As(&background)));
        CHECK(background && SUCCEEDED(background->get_AllTasks(&tasks)) && tasks);
        UINT32 size = 1;
        boolean found = true;
        ComPtr<IBackgroundTaskRegistration> task;
        CHECK(tasks && SUCCEEDED(tasks->get_Size(&size)) && size == 0);
        CHECK(tasks && SUCCEEDED(tasks->HasKey(GUID{}, &found)) && !found);
        CHECK(tasks && tasks->Lookup(GUID{}, &task) == E_BOUNDS && !task);
        ComPtr<
            __FIIterable_1___FIKeyValuePair_2_GUID_Windows__CApplicationModel__CBackground__CIBackgroundTaskRegistration>
            iterable;
        ComPtr<
            __FIIterator_1___FIKeyValuePair_2_GUID_Windows__CApplicationModel__CBackground__CIBackgroundTaskRegistration>
            iterator;
        CHECK(tasks && SUCCEEDED(tasks.As(&iterable)));
        CHECK(iterable && SUCCEEDED(iterable->First(&iterator)) && iterator);
        CHECK(iterator && SUCCEEDED(iterator->get_HasCurrent(&found)) && !found);
        CHECK(iterator && SUCCEEDED(iterator->MoveNext(&found)) && !found);
    }

    // Resize/pause callbacks must be able to register notification tasks offline.
    CHECK(SUCCEEDED(Factory(L"Windows.ApplicationModel.Background.BackgroundTaskBuilder", factory)));
    if (factory) {
        using namespace ABI::Windows::ApplicationModel::Background;
        ComPtr<IInspectable> instance;
        ComPtr<IBackgroundTaskBuilder> builder;
        ComPtr<IBackgroundTaskRegistration> task;
        CHECK(SUCCEEDED(factory->ActivateInstance(&instance)) && instance);
        CHECK(instance && SUCCEEDED(instance.As(&builder)));
        CHECK(builder && builder->Register(&task) == E_INVALIDARG && !task);
        CHECK(builder && SUCCEEDED(builder->put_Name(HStringReference(L"resize-notification").Get())));
        CHECK(builder && SUCCEEDED(builder->put_TaskEntryPoint(HStringReference(L"Game.Notification").Get())));
        ComPtr<ITimeTriggerFactory> timeFactory;
        CHECK(SUCCEEDED(RoGetActivationFactory(
            HStringReference(L"Windows.ApplicationModel.Background.TimeTrigger").Get(), IID_PPV_ARGS(&timeFactory))));
        ComPtr<ITimeTrigger> time;
        ComPtr<IBackgroundTrigger> trigger;
        CHECK(timeFactory && SUCCEEDED(timeFactory->Create(15, true, &time)));
        CHECK(time && SUCCEEDED(time.As(&trigger)));
        CHECK(builder && SUCCEEDED(builder->SetTrigger(trigger.Get())));
        CHECK(builder && SUCCEEDED(builder->Register(&task)) && task);
        GUID id{};
        HString name;
        CHECK(task && SUCCEEDED(task->get_TaskId(&id)) && id != GUID{});
        CHECK(task && SUCCEEDED(task->get_Name(name.GetAddressOf())) &&
              wcscmp(name.GetRawBuffer(nullptr), L"resize-notification") == 0);
        CHECK(task && SUCCEEDED(task->Unregister(true)));
    }

    // GA.GoogleAnalytics (Gameloft): calls succeed and send nothing.
    CHECK(SUCCEEDED(Factory(L"GA.GoogleAnalytics", factory)));
    if (factory) {
        const GUID statics = {0x5f781e60, 0xc3ce, 0x5c08, {0x5a, 0x4c, 0xcf, 0x5f, 0xaa, 0x09, 0xee, 0x8e}};
        ComPtr<IInspectable> analytics;
        CHECK(SUCCEEDED(factory->QueryInterface(statics, &analytics)));
        if (analytics) {
            // IGoogleAnalyticsStatic: TrackingID get 6, Init 7, SendEvent 10, SetVideoResolution 18.
            auto** table = *reinterpret_cast<void***>(analytics.Get());
            using Init = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, HSTRING);
            using GetId = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, HSTRING*);
            using SendEvent = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, HSTRING, HSTRING, HSTRING, IInspectable*);
            using Resolution = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, INT64, INT64);
            HString id;
            CHECK(SUCCEEDED(reinterpret_cast<Init>(table[7])(analytics.Get(), HStringReference(L"UA-1").Get())));
            CHECK(SUCCEEDED(reinterpret_cast<GetId>(table[6])(analytics.Get(), id.GetAddressOf())) &&
                  wcscmp(id.GetRawBuffer(nullptr), L"UA-1") == 0);
            CHECK(SUCCEEDED(reinterpret_cast<SendEvent>(table[10])(analytics.Get(), HStringReference(L"race").Get(),
                                                                   nullptr, nullptr, nullptr)));
            CHECK(SUCCEEDED(reinterpret_cast<Resolution>(table[18])(analytics.Get(), 1920, 1080)));
            const wchar_t embedded[] = {L'U', L'A', L'\0', L'1'};
            HString tracking;
            CHECK(SUCCEEDED(tracking.Set(embedded, 4)));
            CHECK(SUCCEEDED(reinterpret_cast<Init>(table[7])(analytics.Get(), tracking.Get())));
            CHECK(SUCCEEDED(reinterpret_cast<GetId>(table[6])(analytics.Get(), id.ReleaseAndGetAddressOf())));
            UINT32 length = 0;
            const auto* raw = id.GetRawBuffer(&length);
            CHECK(length == 4 && std::wstring(raw, length) == std::wstring(embedded, 4));
        }
    }

    // SDK 1.2 TitleStorage preserves x86 GUID-by-value slots and typed async IIDs.
    ComPtr<appx::xbox::ITitleStorageFileStatics> files;
    CHECK(SUCCEEDED(Factory(L"Microsoft.Xbox.TitleStorage.TitleStorageFile", factory)) &&
          SUCCEEDED(factory.As(&files)));
    ComPtr<IInspectable> descriptor;
    const GUID group = {0x12345678, 0x1234, 0x5678, {1, 2, 3, 4, 5, 6, 7, 8}};
    CHECK(files &&
          SUCCEEDED(files->GetUserFileFromPath(123456789, group, HStringReference(L"save.dat").Get(), &descriptor)));
    ComPtr<appx::xbox::ITitleStorageFile> file;
    CHECK(descriptor && SUCCEEDED(descriptor.As(&file)));
    if (file) {
        UINT64 xuid{};
        GUID restored{};
        HString path;
        CHECK(SUCCEEDED(file->get_Xuid(&xuid)) && xuid == 123456789);
        CHECK(SUCCEEDED(file->get_TitleGroupId(&restored)) && restored == group);
        CHECK(SUCCEEDED(file->get_Filename(path.GetAddressOf())) &&
              wcscmp(path.GetRawBuffer(nullptr), L"save.dat") == 0);
    }
    ComPtr<appx::xbox::ITitleStorageServiceStatics> storage;
    CHECK(SUCCEEDED(Factory(L"Microsoft.Xbox.TitleStorage.TitleStorageService", factory)) &&
          SUCCEEDED(factory.As(&storage)));
    if (storage) {
        ComPtr<IInspectable> op;
        CHECK(SUCCEEDED(storage->ReadToStreamAsync(descriptor.Get(), 0, nullptr, &op)) && op);
        ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::Streams::IRandomAccessStream*>> typed;
        CHECK(op && SUCCEEDED(op.As(&typed)));
        ComPtr<ABI::Windows::Storage::Streams::IRandomAccessStream> stream;
        CHECK(typed && typed->GetResults(&stream) == HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE) && !stream);
        ComPtr<ABI::Windows::Foundation::IAsyncAction> action;
        CHECK(SUCCEEDED(storage->WriteFromStreamAsync(descriptor.Get(), nullptr, 0, 0, &action)));
        CHECK(action && action->GetResults() == HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE));
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
