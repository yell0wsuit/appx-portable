// The game uses the Windows 8.1 Store purchase ABI.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#else
#pragma warning(disable : 4996)
#endif
#include <windows.h>
#include <roapi.h>
#include <windows.applicationmodel.store.h>
#include <windows.networking.connectivity.h>
#undef GetCurrentTime
#include <windows.ui.xaml.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <thread>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::ApplicationModel::Store;
using namespace ABI::Windows::Foundation;
using Map = __FIMapView_2_HSTRING_Windows__CApplicationModel__CStore__CProductLicense;
using Pair = __FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductLicense;
using Iterable = __FIIterable_1___FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductLicense;
using Iterator = __FIIterator_1___FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductLicense;
class SelfClosingCompletion final
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IAsyncOperationCompletedHandler<HSTRING>> {
    ComPtr<SelfClosingCompletion>* owner_;
    std::atomic<int>* released_;
    bool* releasedDuringInvoke_;

public:
    SelfClosingCompletion(ComPtr<SelfClosingCompletion>* owner, std::atomic<int>* released, bool* during)
        : owner_(owner), released_(released), releasedDuringInvoke_(during) {}
    ~SelfClosingCompletion() override { ++*released_; }
    HRESULT STDMETHODCALLTYPE Invoke(IAsyncOperation<HSTRING>* operation, AsyncStatus) override {
        // Keep observation pointers on the stack, so the failing baseline can
        // report early destruction without dereferencing this after Close.
        auto* owner = owner_;
        auto* released = released_;
        auto* during = releasedDuringInvoke_;
        ComPtr<IAsyncInfo> info;
        operation->QueryInterface(IID_PPV_ARGS(&info));
        owner->Reset();
        info->Close();
        *during = released->load() != 0;
        return S_OK;
    }
};
class ReentrantCompletion final
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IAsyncOperationCompletedHandler<HSTRING>> {
    ComPtr<IAsyncInfo> operation_;
    std::atomic<int>& released_;

public:
    ReentrantCompletion(IAsyncInfo* operation, std::atomic<int>& released)
        : operation_(operation), released_(released) {}
    ~ReentrantCompletion() override {
        AsyncStatus status{};
        if (operation_->get_Status(&status) == RO_E_CLOSED && SUCCEEDED(operation_->Close())) ++released_;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IAsyncOperation<HSTRING>*, AsyncStatus) override { return S_OK; }
};
int main(int argc, char** argv) {
    const boolean unlocked = argc > 1 && strcmp(argv[1], "--expect-unlocked") == 0;
    const bool stub = argc > 1 && strcmp(argv[1], "--expect-stub-iap") == 0;
    const bool catalog = argc > 2 && strcmp(argv[2], "--expect-catalog") == 0;
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"appxportable.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    auto hr = get(HStringReference(L"Windows.ApplicationModel.Store.CurrentApp").Get(), &factory);
    if (FAILED(hr)) {
        printf("FAIL CurrentApp factory hr=%08lx\n", hr);
        return 2;
    }
    ComPtr<IActivationFactory> networkFactory;
    ComPtr<ABI::Windows::Networking::Connectivity::INetworkInformationStatics> network;
    ComPtr<ABI::Windows::Networking::Connectivity::IConnectionProfile> profile;
    if (FAILED(get(HStringReference(L"Windows.Networking.Connectivity.NetworkInformation").Get(), &networkFactory)) ||
        FAILED(networkFactory.As(&network)) || network->GetInternetConnectionProfile(nullptr) != E_POINTER ||
        FAILED(network->GetInternetConnectionProfile(&profile)) || bool(profile) != stub)
        return 41;
    if (stub) {
        ABI::Windows::Networking::Connectivity::NetworkConnectivityLevel level{};
        if (FAILED(profile->GetNetworkConnectivityLevel(&level)) ||
            level != ABI::Windows::Networking::Connectivity::NetworkConnectivityLevel_InternetAccess ||
            profile->GetNetworkConnectivityLevel(nullptr) != E_POINTER)
            return 42;
        // Cut the Rope 2 queries the Windows 8.1 profile while initializing.
        ComPtr<ABI::Windows::Networking::Connectivity::IConnectionProfile2> profile2;
        boolean wireless = true;
        if (FAILED(profile.As(&profile2)) || FAILED(profile2->get_IsWwanConnectionProfile(&wireless)) || wireless ||
            FAILED(profile2->get_IsWlanConnectionProfile(&wireless)) || wireless ||
            profile2->get_IsWlanConnectionProfile(nullptr) != E_POINTER)
            return 62;
        ComPtr<ABI::Windows::Networking::Connectivity::IWlanConnectionProfileDetails> wlan;
        ComPtr<ABI::Windows::Networking::Connectivity::IWwanConnectionProfileDetails> wwan;
        ComPtr<__FIReference_1_byte> bars;
        if (FAILED(profile2->get_WlanConnectionProfileDetails(&wlan)) || wlan ||
            FAILED(profile2->get_WwanConnectionProfileDetails(&wwan)) || wwan ||
            FAILED(profile2->GetSignalBars(&bars)) || bars || profile2->GetSignalBars(nullptr) != E_POINTER)
            return 63;
    }
    ComPtr<ICurrentApp> app;
    ComPtr<ICurrentAppWithConsumables> consumables;
    if (FAILED(factory.As(&app)) || FAILED(factory.As(&consumables))) return 3;
    ComPtr<ILicenseInformation> license;
    if (app->get_LicenseInformation(nullptr) != E_POINTER || FAILED(app->get_LicenseInformation(&license)) || !license)
        return 4;
    boolean active = true, trial = true;
    if (FAILED(license->get_IsActive(&active)) || active != unlocked || FAILED(license->get_IsTrial(&trial)) || trial)
        return 5;
    if (license->get_IsActive(nullptr) != E_POINTER || license->get_IsTrial(nullptr) != E_POINTER ||
        license->get_ExpirationDate(nullptr) != E_POINTER)
        return 32;
    DateTime expiration{1};
    if (FAILED(license->get_ExpirationDate(&expiration)) || expiration.UniversalTime != 0) return 6;
    EventRegistrationToken event{};
    auto changed = Callback<ILicenseChangedEventHandler>([]() -> HRESULT { return E_FAIL; });
    if (FAILED(license->add_LicenseChanged(changed.Get(), &event)) || !event.value ||
        FAILED(license->remove_LicenseChanged(event)))
        return 7;
    ComPtr<Map> products;
    if (FAILED(license->get_ProductLicenses(&products)) || !products) return 8;
    UINT32 size = 99;
    boolean found = true;
    ComPtr<IProductLicense> product;
    auto sizeHr = products->get_Size(&size);
    auto keyHr = products->HasKey(HStringReference(L"anything").Get(), &found);
    auto lookupHr = products->Lookup(HStringReference(L"anything").Get(), &product);
    if (FAILED(sizeHr) || size != (catalog ? 2u : 0u) || FAILED(keyHr) || found || lookupHr != E_BOUNDS || product) {
        printf("FAIL empty map sizehr=%08lx size=%u keyhr=%08lx found=%u lookup=%08lx expected=%08lx product=%p\n",
               sizeHr, size, keyHr, found, lookupHr, (E_BOUNDS), product.Get());
        return 9;
    }
    if (!catalog) {
        ComPtr<Map> left, right;
        if (FAILED(products->Split(&left, &right)) || left || right) return 10;
        ComPtr<Iterable> iterable;
        ComPtr<Iterator> iterator;
        if (FAILED(products.As(&iterable)) || FAILED(iterable->First(&iterator)) || !iterator) return 11;
        ComPtr<Pair> current;
        found = true;
        if (FAILED(iterator->get_HasCurrent(&found)) || found || iterator->get_Current(&current) != E_BOUNDS ||
            current || FAILED(iterator->MoveNext(&found)) || found)
            return 12;
        Pair* items[2] = {};
        UINT32 actual = 99;
        if (FAILED(iterator->GetMany(2, items, &actual)) || actual) return 13;
    }
    int licenseChanges = 0;
    auto purchaseChanged = Callback<ILicenseChangedEventHandler>([&]() -> HRESULT {
        ComPtr<Map> state;
        ComPtr<IProductLicense> owned;
        boolean isOwned = false;
        if (FAILED(license->get_ProductLicenses(&state)) ||
            FAILED(state->Lookup(HStringReference(L"anything").Get(), &owned)) ||
            FAILED(owned->get_IsActive(&isOwned)) || !isOwned)
            return E_FAIL;
        ++licenseChanges;
        return S_OK;
    });
    if (FAILED(license->add_LicenseChanged(purchaseChanged.Get(), &event))) return 54;
    ComPtr<IAsyncOperation<HSTRING>> purchase;
    if (app->RequestAppPurchaseAsync(false, nullptr) != E_POINTER ||
        FAILED(app->RequestAppPurchaseAsync(false, &purchase)) || !purchase)
        return 14;
    ComPtr<IAsyncInfo> info;
    if (FAILED(purchase.As(&info))) return 15;
    AsyncStatus status{};
    HRESULT error = S_OK;
    if (FAILED(info->get_Status(&status)) || status != AsyncStatus::Error || FAILED(info->get_ErrorCode(&error)) ||
        SUCCEEDED(error))
        return 16;
    HString receipt;
    if (SUCCEEDED(purchase->GetResults(receipt.GetAddressOf())) || receipt.Length()) return 17;
    bool notified = false;
    auto completed =
        Callback<IAsyncOperationCompletedHandler<HSTRING>>([&](IAsyncOperation<HSTRING>*, AsyncStatus s) -> HRESULT {
            notified = s == AsyncStatus::Error;
            return S_OK;
        });
    if (FAILED(purchase->put_Completed(completed.Get())) || !notified) return 18;
    if (app->RequestProductPurchaseAsync(HStringReference(L"anything").Get(), false, nullptr) != E_POINTER ||
        FAILED(app->RequestProductPurchaseAsync(HStringReference(L"anything").Get(), false, &purchase)) ||
        (SUCCEEDED(purchase->GetResults(receipt.GetAddressOf())) != stub))
        return 19;
    if (stub) {
        if (FAILED(purchase.As(&info)) || FAILED(info->get_Status(&status)) || status != AsyncStatus::Completed ||
            FAILED(info->get_ErrorCode(&error)) || error != S_OK || purchase->GetResults(nullptr) != E_POINTER)
            return 33;
        notified = false;
        auto success = Callback<IAsyncOperationCompletedHandler<HSTRING>>(
            [&](IAsyncOperation<HSTRING>* operation, AsyncStatus s) -> HRESULT {
                notified = s == AsyncStatus::Completed;
                HString empty;
                return operation->GetResults(empty.GetAddressOf());
            });
        if (FAILED(purchase->put_Completed(success.Get())) || !notified ||
            purchase->put_Completed(success.Get()) != E_ILLEGAL_DELEGATE_ASSIGNMENT)
            return 34;
        if (FAILED(info->Close()) || purchase->GetResults(receipt.GetAddressOf()) != RO_E_CLOSED) return 35;
        // Exercise delegate lifetime and reentrancy on a successful operation too.
        ComPtr<IAsyncOperation<HSTRING>> closing;
        if (FAILED(app->RequestProductPurchaseAsync(HStringReference(L"anything").Get(), false, &closing))) return 36;
        ComPtr<SelfClosingCompletion> owner;
        std::atomic<int> released{0};
        bool during = false;
        owner = Make<SelfClosingCompletion>(&owner, &released, &during);
        if (FAILED(closing->put_Completed(owner.Get())) || during || released != 1) return 37;
    }
    ComPtr<IAsyncOperation<PurchaseResults*>> results;
    if (FAILED(consumables->RequestProductPurchaseWithResultsAsync(HStringReference(L"anything").Get(), &results)) ||
        !results)
        return 20;
    ComPtr<IPurchaseResults> result;
    if (SUCCEEDED(results->GetResults(&result)) != stub || bool(result) != stub) return 21;
    if (stub) {
        ProductPurchaseStatus purchased{};
        GUID id{};
        if (FAILED(result->get_Status(&purchased)) || purchased != ProductPurchaseStatus_Succeeded ||
            FAILED(result->get_TransactionId(&id)) || IsEqualGUID(id, GUID_NULL) ||
            result->get_Status(nullptr) != E_POINTER || results->GetResults(nullptr) != E_POINTER)
            return 38;
        // Games read the purchased product from the receipt (Human Anatomy
        // Atlas unlocks by its ProductReceipt). It stays marked and unsigned.
        if (FAILED(result->get_ReceiptXml(receipt.GetAddressOf())) ||
            !wcsstr(receipt.GetRawBuffer(nullptr), L"<ProductReceipt ") ||
            !wcsstr(receipt.GetRawBuffer(nullptr), L"ProductId=\"anything\"") ||
            !wcsstr(receipt.GetRawBuffer(nullptr), L"appxportable:Stub=\"true\"") ||
            wcsstr(receipt.GetRawBuffer(nullptr), L"<Signature"))
            return 64;
        if (FAILED(consumables->RequestProductPurchaseWithDisplayPropertiesAsync(
                HStringReference(L"anything").Get(), HStringReference(L"offer").Get(), nullptr, &results)) ||
            FAILED(results->GetResults(&result)) || FAILED(result->get_OfferId(receipt.GetAddressOf())) ||
            wcscmp(receipt.GetRawBuffer(nullptr), L"offer"))
            return 39;
        ComPtr<IAsyncOperation<HSTRING>> offline;
        if (FAILED(app->GetProductReceiptAsync(HStringReference(L"anything").Get(), &offline)) ||
            SUCCEEDED(offline->GetResults(receipt.GetAddressOf())))
            return 40;
    }
    ComPtr<IAsyncOperation<ListingInformation*>> listingOperation;
    ComPtr<IListingInformation> listing;
    if (app->LoadListingInformationAsync(nullptr) != E_POINTER ||
        FAILED(app->LoadListingInformationAsync(&listingOperation)) ||
        (SUCCEEDED(listingOperation->GetResults(&listing)) != stub) || bool(listing) != stub)
        return 43;
    if (stub) {
        using Listings = __FIMapView_2_HSTRING_Windows__CApplicationModel__CStore__CProductListing;
        using ListingItems =
            __FIIterable_1___FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductListing;
        using ListingIterator =
            __FIIterator_1___FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductListing;
        using ListingPair = __FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductListing;
        ComPtr<Listings> listings;
        ComPtr<ListingItems> listingItems;
        ComPtr<ListingIterator> listingIterator;
        UINT32 count = 99;
        if (FAILED(listing->get_ProductListings(&listings)) || FAILED(listings->get_Size(&count)) ||
            count != (catalog ? 2u : 0u) || FAILED(listings.As(&listingItems)) ||
            FAILED(listingItems->First(&listingIterator)))
            return 44;
        boolean hasListing = false;
        if (FAILED(listingIterator->get_HasCurrent(&hasListing)) || bool(hasListing) != catalog) return 45;
        ComPtr<IProductListing> listedProduct;
        if (listings->Lookup(HStringReference(L"missing").Get(), &listedProduct) != E_BOUNDS || listedProduct)
            return 46;
        if (catalog) {
            if (FAILED(listings->Lookup(HStringReference(L"test.pack").Get(), &listedProduct)) ||
                FAILED(listedProduct->get_FormattedPrice(receipt.GetAddressOf())) ||
                wcscmp(receipt.GetRawBuffer(nullptr), L"0.00"))
                return 47;
            ComPtr<ListingPair> pair;
            if (FAILED(listingIterator->get_Current(&pair)) || FAILED(pair->get_Key(receipt.GetAddressOf())) ||
                wcscmp(receipt.GetRawBuffer(nullptr), L"test.pack"))
                return 48;
            ListingPair* batch[3]{};
            UINT32 listingCount = 99;
            if (FAILED(listingIterator->GetMany(3, batch, &listingCount)) || listingCount != 2) return 49;
            for (UINT32 i = 0; i < listingCount; ++i) batch[i]->Release();
            if (FAILED(listingIterator->get_HasCurrent(&hasListing)) || hasListing ||
                listingIterator->get_Current(&pair) != E_BOUNDS)
                return 50;
            ComPtr<Listings> first, second;
            if (FAILED(listings->Split(&first, &second)) || !first || !second || FAILED(first->get_Size(&count)) ||
                count != 1 || FAILED(second->get_Size(&count)) || count != 1)
                return 51;
        }
    }
    GUID transaction{};
    ComPtr<IAsyncOperation<FulfillmentResult>> fulfillment;
    if (FAILED(consumables->ReportConsumableFulfillmentAsync(HStringReference(L"anything").Get(), transaction,
                                                             &fulfillment)) ||
        !fulfillment)
        return 22;
    FulfillmentResult fulfilled{};
    if (SUCCEEDED(fulfillment->GetResults(&fulfilled)) != stub || (stub && fulfilled != FulfillmentResult_Succeeded))
        return 23;
    if (FAILED(license->get_IsActive(&active)) || active != unlocked || FAILED(products->get_Size(&size)) ||
        size != (catalog ? 2u : 0u))
        return 24;
    if (stub) {
        ComPtr<Map> updated;
        ComPtr<IProductLicense> purchasedLicense;
        if (FAILED(license->get_ProductLicenses(&updated)) || FAILED(updated->get_Size(&size)) ||
            size != (catalog ? 3u : 1u) ||
            FAILED(updated->Lookup(HStringReference(L"anything").Get(), &purchasedLicense)) ||
            FAILED(purchasedLicense->get_IsActive(&active)) || !active)
            return 52;
        if (catalog) {
            if (FAILED(updated->Lookup(HStringReference(L"test.pack").Get(), &purchasedLicense)) ||
                FAILED(purchasedLicense->get_IsActive(&active)) || active)
                return 53;
        }
    }
    if (licenseChanges != (stub ? 1 : 0) || FAILED(license->remove_LicenseChanged(event))) return 55;
    ComPtr<IAsyncOperation<HSTRING>> selfClosing;
    if (FAILED(app->RequestAppPurchaseAsync(false, &selfClosing))) return 30;
    ComPtr<SelfClosingCompletion> owner;
    std::atomic<int> selfReleased{0};
    bool duringInvoke = false;
    owner = Make<SelfClosingCompletion>(&owner, &selfReleased, &duringInvoke);
    if (FAILED(selfClosing->put_Completed(owner.Get())) || duringInvoke || selfReleased != 1) {
        puts("FAIL completion delegate destroyed during Invoke");
        return 31;
    }
    // Close must release delegates outside its mutex: delegate destruction
    // can query and close the same operation again.
    ComPtr<IAsyncOperation<HSTRING>> reentrant;
    if (FAILED(app->RequestAppPurchaseAsync(false, &reentrant))) return 25;
    ComPtr<IAsyncInfo> reentrantInfo;
    if (FAILED(reentrant.As(&reentrantInfo))) return 26;
    std::atomic<int> released{0};
    auto reentrantHandler = Make<ReentrantCompletion>(reentrantInfo.Get(), released);
    if (FAILED(reentrant->put_Completed(reentrantHandler.Get()))) return 27;
    reentrantHandler.Reset();
    HANDLE closed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread closeThread([reentrantInfo, closed] {
        reentrantInfo->Close();
        SetEvent(closed);
    });
    if (WaitForSingleObject(closed, 2000) != WAIT_OBJECT_0) {
        puts("FAIL Close deadlocked during reentrant handler destruction");
        fflush(stdout);
        ExitProcess(28);
    }
    closeThread.join();
    CloseHandle(closed);
    if (released != 1) return 29;
    // Receipt-aware legacy clients distinguish success from cancellation by a
    // nonempty receipt. Stub XML is explicitly unsigned and escapes product IDs.
    ComPtr<IAsyncOperation<HSTRING>> withReceipt;
    if (FAILED(app->RequestProductPurchaseAsync(HStringReference(L"test&<\"'pack").Get(), true, &withReceipt)))
        return 60;
    HString stubReceipt;
    if ((SUCCEEDED(withReceipt->GetResults(stubReceipt.GetAddressOf())) != stub)) return 61;
    if (stub && (!wcsstr(stubReceipt.GetRawBuffer(nullptr), L"appxportable:Stub=\"true\"") ||
                 !wcsstr(stubReceipt.GetRawBuffer(nullptr), L"test&amp;&lt;&quot;&apos;pack") ||
                 wcsstr(stubReceipt.GetRawBuffer(nullptr), L"<Signature")))
        return 62;
    if (!stub && stubReceipt.Length()) return 63;
    // Unity IAP asks for unfulfilled consumables before it reads the listing,
    // and stops when that fails. Stub purchases leave none unfulfilled.
    ComPtr<IAsyncOperation<__FIVectorView_1_Windows__CApplicationModel__CStore__CUnfulfilledConsumable*>> unfulfilled;
    ComPtr<__FIVectorView_1_Windows__CApplicationModel__CStore__CUnfulfilledConsumable> pendingConsumables;
    UINT32 unfulfilledCount = 99;
    if (FAILED(consumables->GetUnfulfilledConsumablesAsync(&unfulfilled)) ||
        SUCCEEDED(unfulfilled->GetResults(&pendingConsumables)) != stub ||
        (stub && (FAILED(pendingConsumables->get_Size(&unfulfilledCount)) || unfulfilledCount != 0)))
        return 65;
    if (argc > 2 && strcmp(argv[2], "--expect-host") == 0) {
        using namespace ABI::Windows::UI::Xaml;
        ComPtr<IActivationFactory> applicationFactory;
        ComPtr<IApplicationStatics> applicationStatics;
        ComPtr<IApplicationFactory> compose;
        ComPtr<IApplication> application;
        bool callbackRan = false, overlayCreated = false;
        int hostFailure = 0;
        DWORD uiThread = 0;
        if (FAILED(get(HStringReference(L"Windows.UI.Xaml.Application").Get(), &applicationFactory)) ||
            FAILED(applicationFactory.As(&applicationStatics)) || FAILED(applicationFactory.As(&compose)))
            return 56;
        auto initialize =
            Callback<IApplicationInitializationCallback>([&](IApplicationInitializationCallbackParams*) -> HRESULT {
                uiThread = GetCurrentThreadId();
                auto hr = compose->CreateInstance(nullptr, nullptr, &application);
                if (FAILED(hr)) return hr;
                ComPtr<IAsyncOperation<HSTRING>> pending;
                hr = app->RequestProductPurchaseAsync(HStringReference(L"host.purchase").Get(), false, &pending);
                if (FAILED(hr)) return hr;
                auto completed = Callback<IAsyncOperationCompletedHandler<HSTRING>>(
                    [&](IAsyncOperation<HSTRING>* operation, AsyncStatus state) -> HRESULT {
                        HString receipt;
                        if (!overlayCreated || uiThread != GetCurrentThreadId() || state != AsyncStatus::Completed ||
                            FAILED(operation->GetResults(receipt.GetAddressOf())))
                            hostFailure = 57;
                        callbackRan = true;
                        PostQuitMessage(0);
                        return S_OK;
                    });
                hr = pending->put_Completed(completed.Get());
                if (callbackRan) hostFailure = 58;
                overlayCreated = true;
                // The queue must retain both the operation and its handler after these locals die.
                return hr;
            });
        if (FAILED(applicationStatics->Start(initialize.Get())) || !callbackRan || hostFailure)
            return hostFailure ? hostFailure : 59;
    }
    puts("storetest: 0 failure(s)");
    return 0;
}
