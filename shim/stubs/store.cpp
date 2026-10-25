#include "generated/store.h"
#include "store_map.h"
#include "registry.h"
#include "com/factory.h"
#include "com/error_operation.h"
#include "com/completed_operation.h"
#include <wrl/wrappers/corewrappers.h>
#include "options.h"
#include "log.h"
#include <wrl/event.h>
#include "package/package.h"
#include <shlwapi.h>
#include <xmllite.h>
#include <vector>
#include <algorithm>
#include <set>
#include <mutex>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::ApplicationModel::Store;
namespace appx {
namespace {
using ProductMap = __FIMapView_2_HSTRING_Windows__CApplicationModel__CStore__CProductLicense;
using ProductPair = __FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductLicense;
using ProductIterable = __FIIterable_1___FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductLicense;
using ProductIterator = __FIIterator_1___FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductLicense;
using UnfulfilledView = __FIVectorView_1_Windows__CApplicationModel__CStore__CUnfulfilledConsumable;
class EmptyProductIterator final : public RuntimeClass<RuntimeClassFlags<WinRt>, ProductIterator, FtmBase> {
    InspectableClass(L"Windows.Foundation.Collections.IIterator`1", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE get_Current(ProductPair** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* value) override { return get_HasCurrent(value); }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 capacity, ProductPair** values, UINT32* actual) override {
        if (!actual) return E_POINTER;
        *actual = 0;
        if (capacity && !values) return E_POINTER;
        return S_OK;
    }
};
class EmptyProductMap final : public RuntimeClass<RuntimeClassFlags<WinRt>, ProductMap, ProductIterable, FtmBase> {
    InspectableClass(L"Windows.Foundation.Collections.IMapView`2", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE Lookup(HSTRING, IProductLicense** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HasKey(HSTRING, boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Split(ProductMap** first, ProductMap** second) override {
        if (first) *first = nullptr;
        if (second) *second = nullptr;
        return first && second ? S_OK : E_POINTER;
    }
    HRESULT STDMETHODCALLTYPE First(ProductIterator** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto iterator = Make<EmptyProductIterator>();
        return iterator ? iterator.CopyTo(value) : E_OUTOFMEMORY;
    }
};
HRESULT MakeStubProductMap(ProductMap** value);
class OfflineLicense final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultILicenseInformation, FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.Store.LicenseInformation", BaseTrust);
    EventSource<ILicenseChangedEventHandler> changed_;

public:
    void NotifyChanged() { changed_.InvokeAll(); }
    HRESULT STDMETHODCALLTYPE get_ProductLicenses(ProductMap** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (GetOptions().stubIap) return MakeStubProductMap(value);
        auto products = Make<EmptyProductMap>();
        return products ? products.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_IsActive(boolean* value) override {
        if (!value) return E_POINTER;
        *value = GetOptions().unlockTrial;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsTrial(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ExpirationDate(DateTime* value) override {
        if (!value) return E_POINTER;
        *value = {};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_LicenseChanged(ILicenseChangedEventHandler* handler,
                                                 EventRegistrationToken* token) override {
        return changed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_LicenseChanged(EventRegistrationToken token) override {
        return changed_.Remove(token);
    }
};
const ComPtr<OfflineLicense>& StoreLicense() {
    static const auto license = [] {
        if (GetOptions().unlockTrial) Log("info", "Store: trial override enabled (base app only)");
        return Make<OfflineLicense>();
    }();
    return license;
}
template <class RuntimeType, class AbiType = RuntimeType> HRESULT OfflineError(IAsyncOperation<RuntimeType>** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    auto operation = Make<ErrorOperation<RuntimeType, AbiType>>(E_ACCESSDENIED);
    return operation ? operation.CopyTo(value) : E_OUTOFMEMORY;
}
using ListingMap = __FIMapView_2_HSTRING_Windows__CApplicationModel__CStore__CProductListing;
using ListingPair = __FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductListing;
using ListingIterable = __FIIterable_1___FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductListing;
using ListingIterator = __FIIterator_1___FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductListing;
class StubProductListing final : public RuntimeClass<RuntimeClassFlags<WinRt>, IProductListing, FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.Store.ProductListing", BaseTrust);
    std::wstring id_;

public:
    explicit StubProductListing(std::wstring id) : id_(std::move(id)) {}
    HRESULT STDMETHODCALLTYPE get_ProductId(HSTRING* value) override { return store::CopyString(id_, value); }
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* value) override { return store::CopyString(id_, value); }
    HRESULT STDMETHODCALLTYPE get_FormattedPrice(HSTRING* value) override { return store::CopyString(L"0.00", value); }
};
using StubListingMap =
    store::StoreMap<ListingMap, ListingIterable, ListingIterator, ListingPair, IProductListing, StubProductListing>;
// Some packages ship a CurrentAppSimulator catalog. Use its product IDs for the
// offline listing; prices are explicitly synthetic and no license entries are imported.
std::vector<std::wstring> ReadStubCatalogFile(const std::filesystem::path& path) {
    ComPtr<IStream> stream;
    ComPtr<IXmlReader> reader;
    std::vector<std::wstring> ids;
    if (FAILED(SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &stream)) ||
        FAILED(CreateXmlReader(IID_PPV_ARGS(&reader), nullptr)) ||
        FAILED(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit)) ||
        FAILED(reader->SetInput(stream.Get())))
        return ids;
    XmlNodeType type;
    HRESULT hr;
    bool listing = false;
    while ((hr = reader->Read(&type)) == S_OK) {
        if (type != XmlNodeType_Element && type != XmlNodeType_EndElement) continue;
        UINT depth{};
        if (FAILED(reader->GetDepth(&depth))) return {};
        const wchar_t* name = nullptr;
        reader->GetLocalName(&name, nullptr);
        // Recognize the catalog schema, rather than interpreting arbitrary
        // asset XML containing Product or ListingInformation elements.
        if (!name || (depth == 0 && wcscmp(name, L"CurrentApp"))) return {};
        if (depth == 1)
            listing = type == XmlNodeType_Element && !reader->IsEmptyElement() && !wcscmp(name, L"ListingInformation");
        if (!listing || depth != 2 || type != XmlNodeType_Element || wcscmp(name, L"Product")) continue;
        if (SUCCEEDED(reader->MoveToAttributeByName(L"ProductId", nullptr))) {
            const wchar_t* id = nullptr;
            UINT32 length{};
            if (SUCCEEDED(reader->GetValue(&id, &length)) && length) {
                std::wstring product(id, length);
                if (std::find(ids.begin(), ids.end(), product) == ids.end()) ids.push_back(std::move(product));
            }
            reader->MoveToElement();
        }
    }
    if (FAILED(hr)) ids.clear();
    return ids;
}
std::vector<std::wstring> DiscoverStubCatalog() {
    const auto& package = CurrentPackage();
    if (!package.loaded) return {};
    std::error_code error;
    std::vector<std::filesystem::path> candidates;
    std::filesystem::recursive_directory_iterator files(
        package.directory, std::filesystem::directory_options::skip_permission_denied, error),
        end;
    for (; !error && files != end; files.increment(error)) {
        const auto& file = *files;
        const auto attributes = GetFileAttributesW(file.path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            files.disable_recursion_pending();
            continue;
        }
        if (file.is_regular_file(error) && _wcsicmp(file.path().extension().c_str(), L".xml") == 0)
            candidates.push_back(file.path());
    }
    std::sort(candidates.begin(), candidates.end());
    std::vector<std::wstring> ids;
    for (const auto& path : candidates) {
        auto products = ReadStubCatalogFile(path);
        if (!products.empty()) Log("info", "Store: stub IAP catalog %ls (%zu products)", path.c_str(), products.size());
        for (auto& product : products)
            if (std::find(ids.begin(), ids.end(), product) == ids.end()) ids.push_back(std::move(product));
    }
    return ids;
}
const std::vector<std::wstring>& ReadStubCatalog() {
    // Package assets do not change during a run. Scan once, including when
    // there is no catalog, rather than on every license or listing query.
    static const auto ids = DiscoverStubCatalog();
    return ids;
}
std::mutex stubPurchasesMutex;
std::set<std::wstring> stubPurchases;
class StubProductLicense final : public RuntimeClass<RuntimeClassFlags<WinRt>, IProductLicense, FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.Store.ProductLicense", BaseTrust);
    std::wstring id_;

public:
    explicit StubProductLicense(std::wstring id) : id_(std::move(id)) {}
    HRESULT STDMETHODCALLTYPE get_ProductId(HSTRING* value) override { return store::CopyString(id_, value); }
    HRESULT STDMETHODCALLTYPE get_IsActive(boolean* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(stubPurchasesMutex);
        *value = stubPurchases.count(id_) != 0;
        Log("TRACE", "Store: product license id=%ls active=%u", id_.c_str(), static_cast<unsigned>(*value));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ExpirationDate(DateTime* value) override {
        if (!value) return E_POINTER;
        value->UniversalTime = 0x7fffffffffffffffLL;
        return S_OK;
    }
};
using StubLicenseMap =
    store::StoreMap<ProductMap, ProductIterable, ProductIterator, ProductPair, IProductLicense, StubProductLicense>;
HRESULT MakeStubProductMap(ProductMap** value) {
    auto ids = ReadStubCatalog();
    {
        std::lock_guard<std::mutex> lock(stubPurchasesMutex);
        for (const auto& id : stubPurchases)
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    }
    auto products = Make<StubLicenseMap>(std::move(ids));
    return products ? products.CopyTo(value) : E_OUTOFMEMORY;
}
void RecordStubPurchase(HSTRING product) {
    UINT32 length{};
    const auto* raw = WindowsGetStringRawBuffer(product, &length);
    bool inserted;
    {
        std::lock_guard<std::mutex> lock(stubPurchasesMutex);
        inserted = stubPurchases.emplace(raw, length).second;
    }
    // License callbacks may query product state again.
    if (inserted && StoreLicense()) StoreLicense()->NotifyChanged();
}
class StubListing final : public RuntimeClass<RuntimeClassFlags<WinRt>, IListingInformation, FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.Store.ListingInformation", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE get_CurrentMarket(HSTRING* value) override { return store::CopyString(L"US", value); }
    HRESULT STDMETHODCALLTYPE get_Description(HSTRING* value) override {
        return store::CopyString(L"Offline stub IAP", value);
    }
    HRESULT STDMETHODCALLTYPE get_FormattedPrice(HSTRING* value) override { return store::CopyString(L"0.00", value); }
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* value) override {
        return store::CopyString(CurrentPackage().displayName, value);
    }
    HRESULT STDMETHODCALLTYPE get_AgeRating(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ProductListings(ListingMap** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto products = Make<StubListingMap>(ReadStubCatalog());
        return products ? products.CopyTo(value) : E_OUTOFMEMORY;
    }
};
// Purchase results never claim to carry a Store signature.
// A local receipt for legacy callers that treat an empty result as cancellation.
// The marker and absence of a signature distinguish this from a Store receipt.
HRESULT StubReceipt(HSTRING product, std::wstring& receipt) {
    GUID transaction{};
    auto hr = CoCreateGuid(&transaction);
    if (FAILED(hr)) return hr;
    wchar_t id[40]{};
    StringFromGUID2(transaction, id, ARRAYSIZE(id));
    std::wstring escaped;
    UINT32 length{};
    const auto* raw = WindowsGetStringRawBuffer(product, &length);
    for (UINT32 i = 0; i < length; ++i) {
        switch (raw[i]) {
        case L'&':
            escaped += L"&amp;";
            break;
        case L'<':
            escaped += L"&lt;";
            break;
        case L'>':
            escaped += L"&gt;";
            break;
        case L'"':
            escaped += L"&quot;";
            break;
        case L'\'':
            escaped += L"&apos;";
            break;
        default:
            if (raw[i] < 0x20) return E_INVALIDARG;
            escaped += raw[i];
        }
    }
    receipt = L"<Receipt Version=\"1.0\" xmlns=\"http://schemas.microsoft.com/windows/2012/store/receipt\" "
              L"xmlns:appxportable=\"urn:appx-portable:stub-iap\" appxportable:Stub=\"true\">"
              L"<ProductReceipt Id=\"" +
              std::wstring(id + 1, 36) + L"\" ProductId=\"" + escaped + L"\" ProductType=\"Consumable\"/></Receipt>";
    return S_OK;
}
class StubPurchaseResults final : public RuntimeClass<RuntimeClassFlags<WinRt>, IPurchaseResults, FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.Store.PurchaseResults", BaseTrust);
    GUID transaction_{};
    Microsoft::WRL::Wrappers::HString offer_;

public:
    explicit StubPurchaseResults(GUID transaction) : transaction_(transaction) {}
    HRESULT SetOffer(HSTRING offer) { return offer_.Set(offer); }
    HRESULT STDMETHODCALLTYPE get_Status(ProductPurchaseStatus* value) override {
        if (!value) return E_POINTER;
        *value = ProductPurchaseStatus_Succeeded;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_TransactionId(GUID* value) override {
        if (!value) return E_POINTER;
        *value = transaction_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ReceiptXml(HSTRING* value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_OfferId(HSTRING* value) override {
        if (!value) return E_POINTER;
        return offer_.CopyTo(value);
    }
};
template <class T, class Abi = T, class Result> HRESULT StubSuccess(IAsyncOperation<T>** value, Result result) {
    if (!value) return E_POINTER;
    *value = nullptr;
    auto operation = Make<CompletedOperation<T, Abi>>(std::move(result));
    return operation ? operation.CopyTo(value) : E_OUTOFMEMORY;
}
HRESULT StubPurchase(HSTRING product, HSTRING offer, IAsyncOperation<PurchaseResults*>** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    GUID transaction{};
    auto hr = CoCreateGuid(&transaction);
    if (FAILED(hr)) return hr;
    auto result = Make<StubPurchaseResults>(transaction);
    if (!result) return E_OUTOFMEMORY;
    hr = result->SetOffer(offer);
    if (FAILED(hr)) return hr;
    Log("info", "Store: stub IAP purchase succeeded product=%ls", WindowsGetStringRawBuffer(product, nullptr));
    auto completed = StubSuccess<PurchaseResults*, IPurchaseResults*>(
        value, [result](IPurchaseResults** out) { return result.CopyTo(out); });
    if (SUCCEEDED(completed)) RecordStubPurchase(product);
    return completed;
}
class StoreFactory final : public Factory<DefaultICurrentApp, DefaultICurrentAppWithConsumables> {
public:
    StoreFactory() : Factory(L"Windows.ApplicationModel.Store.CurrentApp") {}
    HRESULT STDMETHODCALLTYPE get_LicenseInformation(ILicenseInformation** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        // The trial override only changes the base-app license.
        // Stub IAP is a separate opt-in and never creates Store receipts.
        const auto& license = StoreLicense();
        return license ? license.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE RequestAppPurchaseAsync(boolean, IAsyncOperation<HSTRING>** value) override {
        return OfflineError(value);
    }
    HRESULT STDMETHODCALLTYPE RequestProductPurchaseAsync(HSTRING product, boolean includeReceipt,
                                                          IAsyncOperation<HSTRING>** value) override {
        if (!GetOptions().stubIap) return OfflineError(value);
        if (!value) return E_POINTER;
        Log("info", "Store: stub IAP purchase succeeded product=%ls includeReceipt=%u",
            WindowsGetStringRawBuffer(product, nullptr), static_cast<unsigned>(includeReceipt));
        *value = nullptr;
        std::wstring receipt;
        if (includeReceipt) {
            const auto hr = StubReceipt(product, receipt);
            if (FAILED(hr)) return hr;
        }
        auto completed = StubSuccess<HSTRING>(
            value, [receipt = std::move(receipt)](HSTRING* out) { return store::CopyString(receipt, out); });
        if (SUCCEEDED(completed)) RecordStubPurchase(product);
        return completed;
    }
    HRESULT STDMETHODCALLTYPE GetAppReceiptAsync(IAsyncOperation<HSTRING>** value) override {
        return OfflineError(value);
    }
    HRESULT STDMETHODCALLTYPE GetProductReceiptAsync(HSTRING, IAsyncOperation<HSTRING>** value) override {
        return OfflineError(value);
    }
    HRESULT STDMETHODCALLTYPE LoadListingInformationAsync(IAsyncOperation<ListingInformation*>** value) override {
        if (!GetOptions().stubIap) return OfflineError<ListingInformation*, IListingInformation*>(value);
        if (!value) return E_POINTER;
        *value = nullptr;
        auto listing = Make<StubListing>();
        if (!listing) return E_OUTOFMEMORY;
        Log("info", "Store: stub IAP listing enabled");
        return StubSuccess<ListingInformation*, IListingInformation*>(
            value, [listing](IListingInformation** out) { return listing.CopyTo(out); });
    }
    HRESULT STDMETHODCALLTYPE ReportConsumableFulfillmentAsync(HSTRING product, GUID,
                                                               IAsyncOperation<FulfillmentResult>** value) override {
        if (!GetOptions().stubIap) return OfflineError(value);
        Log("info", "Store: stub IAP fulfillment succeeded product=%ls", WindowsGetStringRawBuffer(product, nullptr));
        return StubSuccess<FulfillmentResult>(value, [](FulfillmentResult* out) {
            *out = FulfillmentResult_Succeeded;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE
    RequestProductPurchaseWithResultsAsync(HSTRING product, IAsyncOperation<PurchaseResults*>** value) override {
        if (GetOptions().stubIap) return StubPurchase(product, nullptr, value);
        return OfflineError<PurchaseResults*, IPurchaseResults*>(value);
    }
    HRESULT STDMETHODCALLTYPE
    RequestProductPurchaseWithDisplayPropertiesAsync(HSTRING product, HSTRING offer, IProductPurchaseDisplayProperties*,
                                                     IAsyncOperation<PurchaseResults*>** value) override {
        if (GetOptions().stubIap) return StubPurchase(product, offer, value);
        return OfflineError<PurchaseResults*, IPurchaseResults*>(value);
    }
    HRESULT STDMETHODCALLTYPE GetUnfulfilledConsumablesAsync(IAsyncOperation<UnfulfilledView*>** value) override {
        return OfflineError(value);
    }
};
}
HRESULT MakeStoreFactory(IActivationFactory** value) {
    auto factory = Make<StoreFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
