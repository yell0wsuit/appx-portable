// Local Xbox Marketplace purchases. No Xbox SDK DLL or network service is used.
#include "marketplace_abi.h"
#include "com/factory.h"
#include "com/async_action.h"
#include "com/async_id.h"
#include "com/failed_operation.h"
#include "com/pinterface.h"
#include "options.h"
#include "stubs/store_map.h"
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
using namespace Microsoft::WRL;
namespace appx::xbox {
namespace {
using namespace ABI::Windows::Foundation;
constexpr HRESULT Offline = HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE);
template <class T> HRESULT Copy(T value, T* out) {
    if (!out) return E_POINTER;
    *out = value;
    return S_OK;
}
DateTime Now() {
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    return {static_cast<INT64>((static_cast<UINT64>(time.dwHighDateTime) << 32) | time.dwLowDateTime)};
}
std::wstring GuidText(GUID id) {
    wchar_t text[40]{};
    StringFromGUID2(id, text, 40);
    return text;
}
HRESULT Action(HRESULT result, IAsyncAction** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    auto action = Make<AsyncAction>(true);
    if (!action) return E_OUTOFMEMORY;
    action->Finish(result);
    return action.CopyTo(out);
}
// Immutable collection snapshots: GetAt, enumeration and GetMany share the
// same objects. Their parameterized IIDs describe Xbox runtime classes.
template <class T> class Iterator final : public RuntimeClass<RuntimeClassFlags<WinRt>, IteratorAbi<T>, FtmBase> {
    using RuntimeClassT = RuntimeClass<RuntimeClassFlags<WinRt>, IteratorAbi<T>, FtmBase>;
    InspectableClass(L"Windows.Foundation.Collections.IIterator`1", BaseTrust);
    std::vector<ComPtr<T>> items_;
    size_t index_ = 0;

public:
    explicit Iterator(std::vector<ComPtr<T>> items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE get_Current(T** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return index_ < items_.size() ? items_[index_].CopyTo(out) : E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* out) override {
        return Copy<boolean>(index_ < items_.size(), out);
    }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* out) override {
        if (index_ < items_.size()) ++index_;
        return get_HasCurrent(out);
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 capacity, T** out, UINT32* actual) override {
        if (!actual || (capacity && !out)) return E_POINTER;
        *actual = 0;
        while (*actual < capacity && index_ < items_.size()) {
            items_[index_++].CopyTo(&out[(*actual)++]);
        }
        return S_OK;
    }
};
template <class T>
class View final : public RuntimeClass<RuntimeClassFlags<WinRt>, VectorAbi<T>, IterableAbi<T>, FtmBase> {
    using RuntimeClassT = RuntimeClass<RuntimeClassFlags<WinRt>, VectorAbi<T>, IterableAbi<T>, FtmBase>;
    InspectableClass(L"Windows.Foundation.Collections.IVectorView`1", BaseTrust);
    std::vector<ComPtr<T>> items_;

public:
    explicit View(std::vector<ComPtr<T>> items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, T** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return index < items_.size() ? items_[index].CopyTo(out) : E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* out) override {
        return Copy<UINT32>(static_cast<UINT32>(items_.size()), out);
    }
    HRESULT STDMETHODCALLTYPE IndexOf(T* item, UINT32* index, boolean* found) override {
        if (!index || !found) return E_POINTER;
        *index = 0;
        *found = false;
        ComPtr<IUnknown> identity;
        if (item) item->QueryInterface(IID_PPV_ARGS(&identity));
        for (UINT32 i = 0; i < items_.size(); ++i) {
            ComPtr<IUnknown> other;
            items_[i].As(&other);
            if (identity == other) {
                *index = i;
                *found = true;
                break;
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 start, UINT32 capacity, T** out, UINT32* actual) override {
        if (!actual || (capacity && !out)) return E_POINTER;
        *actual = 0;
        if (start > items_.size()) return E_BOUNDS;
        while (*actual < capacity && start < items_.size()) items_[start++].CopyTo(&out[(*actual)++]);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE First(IteratorAbi<T>** out) override {
        if (!out) return E_POINTER;
        return Make<Iterator<T>>(items_).CopyTo(out);
    }
};
template <class T, class Item> class Collection final : public RuntimeClass<RuntimeClassFlags<WinRt>, T, FtmBase> {
    using RuntimeClassT = RuntimeClass<RuntimeClassFlags<WinRt>, T, FtmBase>;
    const wchar_t* name_;
    ComPtr<View<Item>> view_;

public:
    Collection(const wchar_t* name, std::vector<ComPtr<Item>> items)
        : name_(name), view_(Make<View<Item>>(std::move(items))) {}
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* out) override { return store::CopyString(name_, out); }
    HRESULT STDMETHODCALLTYPE get_Items(IInspectable** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return view_ ? view_.CopyTo(out) : E_OUTOFMEMORY;
    }
};
// A completed IAsyncOperation<Xbox runtime class>, retaining a stable result
// and dispatching completion on the same host queue as CurrentApp purchases.
template <class T>
class ClassOperation final : public RuntimeClass<RuntimeClassFlags<WinRt>, OperationAbi<T>, IAsyncInfo, FtmBase> {
    using RuntimeClassT = RuntimeClass<RuntimeClassFlags<WinRt>, OperationAbi<T>, IAsyncInfo, FtmBase>;
    InspectableClass(L"Windows.Foundation.IAsyncOperation`1", BaseTrust);
    ComPtr<T> result_;
    ComPtr<IUnknown> handler_;
    std::mutex mutex_;
    bool assigned_ = false, closed_ = false;
    const UINT32 id_ = NextAsyncId();

public:
    explicit ClassOperation(ComPtr<T> result) : result_(std::move(result)) {}
    HRESULT STDMETHODCALLTYPE put_Completed(IUnknown* handler) override {
        ComPtr<IUnknown> callback(handler);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return RO_E_CLOSED;
            if (assigned_) return E_ILLEGAL_DELEGATE_ASSIGNMENT;
            assigned_ = true;
            handler_ = callback;
        }
        if (callback) {
            ComPtr<ClassOperation> self(this);
            auto invoke = [self, callback] {
                using Invoke = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, IInspectable*, AsyncStatus);
                reinterpret_cast<Invoke>((*reinterpret_cast<void***>(callback.Get()))[3])(
                    callback.Get(), static_cast<OperationAbi<T>*>(self.Get()), AsyncStatus::Completed);
            };
            if (FAILED(PostHostWork(0, invoke))) invoke();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Completed(IUnknown** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_ ? RO_E_CLOSED : handler_.CopyTo(out);
    }
    HRESULT STDMETHODCALLTYPE GetResults(T** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_ ? RO_E_CLOSED : result_.CopyTo(out);
    }
    HRESULT STDMETHODCALLTYPE get_Id(UINT32* out) override { return Info(id_, out); }
    HRESULT STDMETHODCALLTYPE get_Status(AsyncStatus* out) override { return Info(AsyncStatus::Completed, out); }
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* out) override { return Info(S_OK, out); }
    template <class V> HRESULT Info(V value, V* out) {
        if (!out) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_ ? RO_E_CLOSED : Copy(value, out);
    }
    HRESULT STDMETHODCALLTYPE Cancel() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_ ? RO_E_CLOSED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        ComPtr<IUnknown> releasing;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
            releasing.Swap(handler_);
        }
        return S_OK;
    }
};
template <class T> HRESULT Success(ComPtr<T> result, IInspectable** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!result) return E_OUTOFMEMORY;
    return Make<ClassOperation<T>>(std::move(result)).CopyTo(out);
}
template <class T> HRESULT Failed(IInspectable** out, HRESULT error = Offline) {
    return MakeFailedOperation(__uuidof(OperationAbi<T>), error, sizeof(void*), out);
}
HRESULT EmptyStrings(IInspectable** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    auto view = winrt::single_threaded_vector<winrt::hstring>().GetView();
    *out = reinterpret_cast<IInspectable*>(winrt::detach_abi(view));
    return S_OK;
}
class AssetBalance final : public RuntimeClass<RuntimeClassFlags<WinRt>, IAssetBalance, FtmBase> {
    InspectableClass(L"Microsoft.Xbox.Marketplace.AssetBalance", BaseTrust);
    std::atomic<UINT32> id_, quantity_;
    const DateTime time_ = Now();

public:
    AssetBalance(UINT32 id, UINT32 quantity) : id_(id), quantity_(quantity) {}
    HRESULT STDMETHODCALLTYPE get_Id(UINT32* out) override { return Copy(id_.load(), out); }
    HRESULT STDMETHODCALLTYPE put_Id(UINT32 value) override {
        id_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Quantity(UINT32* out) override { return Copy(quantity_.load(), out); }
    HRESULT STDMETHODCALLTYPE put_Quantity(UINT32 value) override {
        quantity_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Xuid(UINT64* out) override { return Copy<UINT64>(0, out); }
    HRESULT STDMETHODCALLTYPE get_TitleId(UINT32* out) override { return Copy<UINT32>(0, out); }
    HRESULT STDMETHODCALLTYPE get_Timestamp(DateTime* out) override { return Copy(time_, out); }
    HRESULT STDMETHODCALLTYPE get_Json(HSTRING* out) override {
        return store::CopyString(L"{\"appxportableStub\":true,\"id\":" + std::to_wstring(id_.load()) +
                                     L",\"quantity\":" + std::to_wstring(quantity_.load()) + L"}",
                                 out);
    }
};
class AssetFactory final : public Factory<IAssetBalanceFactory> {
public:
    AssetFactory() : Factory(L"Microsoft.Xbox.Marketplace.AssetBalance") {}
    HRESULT STDMETHODCALLTYPE CreateAssetBalance(UINT32 id, UINT32 quantity, IInspectable** out) override {
        if (!out) return E_POINTER;
        return Make<AssetBalance>(id, quantity).CopyTo(out);
    }
};
struct Product {
    GUID offer{};
    UINT32 asset = 0, quantity = 0;
};
class Offer final : public RuntimeClass<RuntimeClassFlags<WinRt>, IOffer, FtmBase> {
    InspectableClass(L"Microsoft.Xbox.Marketplace.Offer", BaseTrust);
    const Product product_;

public:
    explicit Offer(Product product) : product_(product) {}
    HRESULT STDMETHODCALLTYPE get_ContentCategory(UINT32* out) override { return Copy<UINT32>(0, out); }
    HRESULT STDMETHODCALLTYPE get_DownloadSizeInBytes(UINT64* out) override { return Copy<UINT64>(0, out); }
    HRESULT STDMETHODCALLTYPE get_OfferId(GUID* out) override { return Copy(product_.offer, out); }
    HRESULT STDMETHODCALLTYPE get_OfferName(HSTRING* out) override {
        return store::CopyString(GuidText(product_.offer), out);
    }
    HRESULT STDMETHODCALLTYPE get_OfferType(INT32* out) override { return Copy<INT32>(product_.asset ? 4 : 2, out); }
    HRESULT STDMETHODCALLTYPE get_OfferImages(IInspectable** out) override {
        if (!out) return E_POINTER;
        ComPtr<VectorAbi<IOfferImage>> view = Make<View<IOfferImage>>(std::vector<ComPtr<IOfferImage>>{});
        return view.CopyTo(out);
    }
    HRESULT STDMETHODCALLTYPE get_DisplayPrice(HSTRING* out) override { return store::CopyString(L"0.00", out); }
    HRESULT STDMETHODCALLTYPE get_ReleaseDate(DateTime* out) override { return Copy(DateTime{}, out); }
    HRESULT STDMETHODCALLTYPE get_SellText(HSTRING* out) override {
        return store::CopyString(L"Offline stub IAP", out);
    }
    HRESULT STDMETHODCALLTYPE get_TitleId(UINT32* out) override { return Copy<UINT32>(0, out); }
    HRESULT STDMETHODCALLTYPE get_TitleName(HSTRING* out) override { return store::CopyString(L"", out); }
    HRESULT STDMETHODCALLTYPE get_AverageUserRating(DOUBLE* out) override { return Copy<DOUBLE>(0, out); }
    HRESULT STDMETHODCALLTYPE get_UserRatingCount(UINT32* out) override { return Copy<UINT32>(0, out); }
};
class Receipt final : public RuntimeClass<RuntimeClassFlags<WinRt>, IReceipt, FtmBase> {
    InspectableClass(L"Microsoft.Xbox.Marketplace.Receipt", BaseTrust);
    const GUID offer_, transaction_;
    const DateTime date_;

public:
    Receipt(GUID offer, GUID transaction, DateTime date) : offer_(offer), transaction_(transaction), date_(date) {}
    HRESULT STDMETHODCALLTYPE get_TransactionId(GUID* out) override { return Copy(transaction_, out); }
    HRESULT STDMETHODCALLTYPE get_OfferId(GUID* out) override { return Copy(offer_, out); }
    HRESULT STDMETHODCALLTYPE get_Xuid(UINT64* out) override { return Copy<UINT64>(0, out); }
    HRESULT STDMETHODCALLTYPE get_PurchasingPartnerId(UINT32* out) override { return Copy<UINT32>(0, out); }
    HRESULT STDMETHODCALLTYPE get_TitleId(UINT32* out) override { return Copy<UINT32>(0, out); }
    HRESULT STDMETHODCALLTYPE get_Title(HSTRING* out) override { return store::CopyString(L"Offline stub IAP", out); }
    HRESULT STDMETHODCALLTYPE get_PurchaseDate(DateTime* out) override { return Copy(date_, out); }
    HRESULT STDMETHODCALLTYPE get_Status(HSTRING* out) override { return store::CopyString(L"Succeeded", out); }
    HRESULT STDMETHODCALLTYPE get_MediaInstanceUrls(IInspectable** out) override { return EmptyStrings(out); }
    HRESULT STDMETHODCALLTYPE get_Json(HSTRING* out) override {
        return store::CopyString(L"{\"appxportableStub\":true,\"offerId\":\"" + GuidText(offer_) +
                                     L"\",\"transactionId\":\"" + GuidText(transaction_) + L"\"}",
                                 out);
    }
};
std::vector<Product> Catalog() {
    std::vector<Product> result;
    std::wistringstream entries(GetOptions().stubXboxProducts);
    std::wstring entry;
    while (std::getline(entries, entry, L',')) {
        auto colon = entry.find(L':');
        Product product;
        auto text = entry.substr(0, colon);
        text.erase(0, text.find_first_not_of(L" \t"));
        auto end = text.find_last_not_of(L" \t");
        if (end != std::wstring::npos) text.erase(end + 1);
        if (text.empty()) continue;
        if (text.front() != L'{') text = L"{" + text + L"}";
        bool valid = SUCCEEDED(CLSIDFromString(text.c_str(), &product.offer));
        if (colon != std::wstring::npos) {
            auto second = entry.find(L':', colon + 1);
            auto number = [](const std::wstring& value, UINT32& out) {
                if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring::npos) return false;
                wchar_t* tail = nullptr;
                auto n = wcstoull(value.c_str(), &tail, 10);
                if (*tail || n > std::numeric_limits<UINT32>::max()) return false;
                out = static_cast<UINT32>(n);
                return true;
            };
            valid = valid && second != std::wstring::npos &&
                    number(entry.substr(colon + 1, second - colon - 1), product.asset) &&
                    number(entry.substr(second + 1), product.quantity) && product.quantity > 0;
        }
        if (!valid) {
            Log("FAIL", "Xbox Marketplace: invalid StubXboxProducts entry %ls", entry.c_str());
            continue;
        }
        if (std::none_of(result.begin(), result.end(), [&](const Product& p) { return p.offer == product.offer; }))
            result.push_back(product);
    }
    return result;
}
class MarketplaceFactory final : public Factory<IMarketplaceStatics> {
    std::atomic<boolean> retype_{false};
    std::mutex mutex_;
    const std::vector<Product> products_ = Catalog();
    std::map<UINT32, UINT32> balances_;
    std::vector<ComPtr<IReceipt>> receipts_;
    template <class C, class T>
    HRESULT List(const wchar_t* name, std::vector<ComPtr<T>> items, UINT32 skip, UINT32 max, IInspectable** out) {
        // max=0 requests all items (the Windows 8 Xbox DLC framework uses it).
        const auto begin = std::min<size_t>(skip, items.size());
        const auto count = max ? std::min<size_t>(max, items.size() - begin) : items.size() - begin;
        std::vector<ComPtr<T>> page(items.begin() + begin, items.begin() + begin + count);
        ComPtr<C> collection = Make<Collection<C, T>>(name, std::move(page));
        return Success<C>(collection, out);
    }

public:
    MarketplaceFactory() : Factory(L"Microsoft.Xbox.Marketplace.MarketplaceService") {}
    HRESULT STDMETHODCALLTYPE EnumerateOffersByOfferIdsAsync(IInspectable* ids, IInspectable** out) override {
        if (!GetOptions().stubIap) return Failed<IOfferCollection>(out);
        if (!ids) return E_INVALIDARG;
        ComPtr<__FIVectorView_1_HSTRING> view;
        auto hr = ids->QueryInterface(IID_PPV_ARGS(&view));
        if (FAILED(hr)) return hr;
        UINT32 count{};
        hr = view->get_Size(&count);
        if (FAILED(hr)) return hr;
        std::vector<ComPtr<IOffer>> offers;
        for (UINT32 i = 0; i < count; ++i) {
            Wrappers::HString text;
            hr = view->GetAt(i, text.GetAddressOf());
            if (FAILED(hr)) return hr;
            GUID id{};
            auto raw = std::wstring(WindowsGetStringRawBuffer(text.Get(), nullptr));
            if (!raw.empty() && raw.front() != L'{') raw = L"{" + raw + L"}";
            if (FAILED(CLSIDFromString(raw.c_str(), &id))) continue;
            auto found =
                std::find_if(products_.begin(), products_.end(), [&](const Product& p) { return p.offer == id; });
            offers.push_back(Make<Offer>(found == products_.end() ? Product{id} : *found));
        }
        return List<IOfferCollection>(L"Microsoft.Xbox.Marketplace.OfferCollection", std::move(offers), 0, 0, out);
    }
    HRESULT STDMETHODCALLTYPE EnumerateOffersAsync(UINT32, UINT32 skip, UINT32 max, INT32 types, UINT32, INT32 order,
                                                   boolean ascending, IInspectable** out) override {
        if (!GetOptions().stubIap) return Failed<IOfferCollection>(out);
        std::vector<ComPtr<IOffer>> offers;
        for (auto product : products_)
            if (!types || (types & (product.asset ? 4 : 2))) offers.push_back(Make<Offer>(product));
        // Placeholder metadata is equal for all offers; stable catalog order is
        // the tie-breaker, reversed when the caller requests descending order.
        (void)order;
        if (!ascending) std::reverse(offers.begin(), offers.end());
        return List<IOfferCollection>(L"Microsoft.Xbox.Marketplace.OfferCollection", std::move(offers), skip, max, out);
    }
    HRESULT STDMETHODCALLTYPE ShowPurchaseAsync(GUID id, IAsyncAction** out) override {
        if (!GetOptions().stubIap) return Action(Offline, out);
        if (!out) return E_POINTER;
        *out = nullptr;
        GUID transaction{};
        auto hr = CoCreateGuid(&transaction);
        if (FAILED(hr)) return hr;
        auto receipt = Make<Receipt>(id, transaction, Now());
        if (!receipt) return E_OUTOFMEMORY;
        // Allocate the action before granting anything. Invalid output pointers
        // and failed allocations must not change balances.
        hr = Action(S_OK, out);
        if (FAILED(hr)) return hr;
        std::lock_guard<std::mutex> lock(mutex_);
        auto product =
            std::find_if(products_.begin(), products_.end(), [&](const Product& p) { return p.offer == id; });
        if (product != products_.end() && product->asset) {
            auto& balance = balances_[product->asset];
            balance =
                static_cast<UINT32>(std::min<UINT64>(static_cast<UINT64>(balance) + product->quantity, UINT32_MAX));
        }
        receipts_.push_back(receipt);
        Log("INFO", "Xbox Marketplace: stub IAP purchase succeeded offer=%ls", GuidText(id).c_str());
        if (product == products_.end())
            Log("INFO", "Xbox Marketplace: no asset mapping for offer; receipt only, configure StubXboxProducts for "
                        "consumables");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetReceiptsAsync(UINT32, UINT32 skip, UINT32 max, IInspectable*,
                                               IInspectable** out) override {
        if (!GetOptions().stubIap) return Failed<IReceiptCollection>(out);
        std::lock_guard<std::mutex> lock(mutex_);
        return List<IReceiptCollection>(L"Microsoft.Xbox.Marketplace.ReceiptCollection", receipts_, skip, max, out);
    }
    HRESULT STDMETHODCALLTYPE GetReceiptsWithDateFilterAsync(UINT32, UINT32 skip, UINT32 max, DateTime date,
                                                             IInspectable*, IInspectable** out) override {
        if (!GetOptions().stubIap) return Failed<IReceiptCollection>(out);
        std::vector<ComPtr<IReceipt>> items;
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& receipt : receipts_) {
            DateTime time{};
            receipt->get_PurchaseDate(&time);
            if (time.UniversalTime >= date.UniversalTime) items.push_back(receipt);
        }
        return List<IReceiptCollection>(L"Microsoft.Xbox.Marketplace.ReceiptCollection", std::move(items), skip, max,
                                        out);
    }
    HRESULT STDMETHODCALLTYPE GetReceiptAsync(UINT32, GUID id, IInspectable** out) override {
        if (!GetOptions().stubIap) return Failed<IReceipt>(out);
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = receipts_.rbegin(); it != receipts_.rend(); ++it) {
            GUID offer{};
            (*it)->get_OfferId(&offer);
            if (offer == id) return Success<IReceipt>(*it, out);
        }
        return Failed<IReceipt>(out, HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
    }
    HRESULT STDMETHODCALLTYPE GetAssetsAsync(UINT32, UINT32 skip, UINT32 max, IInspectable*,
                                             IInspectable** out) override {
        if (!GetOptions().stubIap) return Failed<IAssetBalanceCollection>(out);
        std::vector<ComPtr<IAssetBalance>> items;
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, quantity] : balances_)
            if (quantity) items.push_back(Make<AssetBalance>(id, quantity));
        return List<IAssetBalanceCollection>(L"Microsoft.Xbox.Marketplace.AssetBalanceCollection", std::move(items),
                                             skip, max, out);
    }
    HRESULT STDMETHODCALLTYPE ConsumeAssetsAsync(UINT32, IInspectable* assets, IAsyncAction** out) override {
        if (!GetOptions().stubIap) return Action(Offline, out);
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!assets) return E_INVALIDARG;
        ComPtr<VectorAbi<IAssetBalance>> view;
        auto hr = assets->QueryInterface(IID_PPV_ARGS(&view));
        if (FAILED(hr)) return hr;
        UINT32 count{};
        hr = view->get_Size(&count);
        if (FAILED(hr)) return hr;
        std::map<UINT32, UINT64> requested;
        for (UINT32 i = 0; i < count; ++i) {
            ComPtr<IAssetBalance> asset;
            hr = view->GetAt(i, &asset);
            if (FAILED(hr)) return hr;
            if (!asset) return E_INVALIDARG;
            UINT32 id{}, quantity{};
            hr = asset->get_Id(&id);
            if (FAILED(hr)) return hr;
            hr = asset->get_Quantity(&quantity);
            if (FAILED(hr)) return hr;
            requested[id] += quantity;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, quantity] : requested) {
            auto it = balances_.find(id);
            if (quantity && (it == balances_.end() || quantity > it->second)) return Action(E_INVALIDARG, out);
        }
        hr = Action(S_OK, out);
        if (FAILED(hr)) return hr;
        for (const auto& [id, quantity] : requested) balances_[id] -= static_cast<UINT32>(quantity);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_RetypeCredentialsAtPurchase(boolean* out) override {
        return Copy(retype_.load(), out);
    }
    HRESULT STDMETHODCALLTYPE put_RetypeCredentialsAtPurchase(boolean value) override {
        retype_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetRetypeCredentialsAtPurchaseAsync(boolean value, IAsyncAction** out) override {
        if (!out) return E_POINTER;
        auto hr = Action(S_OK, out);
        if (SUCCEEDED(hr)) retype_ = value;
        return hr;
    }
};
}
HRESULT MakeMarketplaceFactory(IActivationFactory** out) {
    if (!out) return E_POINTER;
    static auto factory = Make<MarketplaceFactory>();
    return factory ? factory.CopyTo(out) : E_OUTOFMEMORY;
}
HRESULT MakeAssetBalanceFactory(IActivationFactory** out) {
    if (!out) return E_POINTER;
    static auto factory = Make<AssetFactory>();
    return factory ? factory.CopyTo(out) : E_OUTOFMEMORY;
}
}
