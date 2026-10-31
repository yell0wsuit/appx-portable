#include "xbox/marketplace_abi.h"
#include "com/pinterface.h"
#include <roapi.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace appx::xbox;
using namespace ABI::Windows::Foundation;
static int failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)
template <class T> ComPtr<T> Result(ComPtr<IInspectable> operation) {
    ComPtr<OperationAbi<T>> typed;
    CHECK(operation && SUCCEEDED(operation.As(&typed)));
    ComPtr<T> result;
    CHECK(typed && SUCCEEDED(typed->GetResults(&result)) && result);
    return result;
}
template <class T, class C> ComPtr<VectorAbi<T>> Items(ComPtr<C> collection) {
    ComPtr<IInspectable> raw;
    CHECK(collection && SUCCEEDED(collection->get_Items(&raw)));
    ComPtr<VectorAbi<T>> view;
    CHECK(raw && SUCCEEDED(raw.As(&view)));
    return view;
}
static UINT32 Balance(IMarketplaceStatics* market) {
    ComPtr<IInspectable> op;
    CHECK(SUCCEEDED(market->GetAssetsAsync(0, 0, 0, nullptr, &op)));
    auto view = Items<IAssetBalance>(Result<IAssetBalanceCollection>(op));
    UINT32 size{};
    CHECK(view && SUCCEEDED(view->get_Size(&size)));
    if (!size) return 0;
    ComPtr<IAssetBalance> asset;
    CHECK(SUCCEEDED(view->GetAt(0, &asset)));
    UINT32 id{}, quantity{};
    CHECK(asset && SUCCEEDED(asset->get_Id(&id)) && id == 1);
    CHECK(asset && SUCCEEDED(asset->get_Quantity(&quantity)));
    return quantity;
}
static HRESULT Purchase(IMarketplaceStatics* market, GUID offer) {
    ComPtr<IAsyncAction> action;
    auto hr = market->ShowPurchaseAsync(offer, &action);
    CHECK(SUCCEEDED(hr) && action);
    if (!action) return E_FAIL;
    ComPtr<IAsyncInfo> info;
    CHECK(SUCCEEDED(action.As(&info)));
    AsyncStatus status{};
    CHECK(info && SUCCEEDED(info->get_Status(&status)));
    CHECK(status == AsyncStatus::Completed);
    HRESULT error = E_FAIL;
    CHECK(info && SUCCEEDED(info->get_ErrorCode(&error)) && error == S_OK);
    bool called = false;
    auto handler = Callback<IAsyncActionCompletedHandler>([&](IAsyncAction*, AsyncStatus state) {
        CHECK(state == AsyncStatus::Completed);
        called = true;
        return S_OK;
    });
    CHECK(SUCCEEDED(action->put_Completed(handler.Get())) && called);
    return action->GetResults();
}
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    RoInitialize(RO_INIT_MULTITHREADED);
    auto dll = LoadLibraryW(L"appxportable.dll");
    using FactoryCall = HRESULT(WINAPI*)(HSTRING, IActivationFactory**);
    auto get = dll ? reinterpret_cast<FactoryCall>(GetProcAddress(dll, "ShimGetFactory")) : nullptr;
    if (!get) return 1;
    ComPtr<IActivationFactory> factory;
    CHECK(SUCCEEDED(get(HStringReference(L"Microsoft.Xbox.Marketplace.MarketplaceService").Get(), &factory)));
    ComPtr<IMarketplaceStatics> market;
    CHECK(factory && SUCCEEDED(factory.As(&market)));
    if (!market) return 1;
    GUID one{}, two{}, durable{};
    CLSIDFromString(L"{546f0200-0000-4000-8000-0000394e07d3}", &one);
    CLSIDFromString(L"{a9c41e00-0000-4000-8000-0000394e07d3}", &two);
    CLSIDFromString(L"{12345678-1234-5678-8000-000000000001}", &durable);
    CHECK(Balance(market.Get()) == 0);
    CHECK(market->ShowPurchaseAsync(one, nullptr) == E_POINTER);
    CHECK(Balance(market.Get()) == 0);
    ComPtr<IInspectable> op;
    CHECK(SUCCEEDED(market->EnumerateOffersAsync(0, 0, 0, 0, 0, 0, true, &op)));
    auto offers = Items<IOffer>(Result<IOfferCollection>(op));
    UINT32 size{};
    CHECK(offers && SUCCEEDED(offers->get_Size(&size)) && size == 3);
    ComPtr<IOffer> offer;
    CHECK(SUCCEEDED(offers->GetAt(0, &offer)));
    GUID offerId{};
    CHECK(offer && SUCCEEDED(offer->get_OfferId(&offerId)) && offerId == one);
    ComPtr<IterableAbi<IOffer>> iterable;
    CHECK(SUCCEEDED(offers.As(&iterable)));
    ComPtr<IteratorAbi<IOffer>> iterator;
    CHECK(iterable && SUCCEEDED(iterable->First(&iterator)));
    boolean has{};
    CHECK(iterator && SUCCEEDED(iterator->get_HasCurrent(&has)) && has);
    IOffer* many[4]{};
    UINT32 actual{};
    CHECK(iterator && SUCCEEDED(iterator->GetMany(4, many, &actual)) && actual == 3);
    for (UINT32 i = 0; i < actual; ++i) many[i]->Release();
    CHECK(iterator && SUCCEEDED(iterator->get_HasCurrent(&has)) && !has);
    CHECK(iterator && iterator->get_Current(offer.ReleaseAndGetAddressOf()) == E_BOUNDS && !offer);
    CHECK(SUCCEEDED(offers->GetMany(3, 4, many, &actual)) && actual == 0);
    CHECK(offers->GetMany(4, 4, many, &actual) == E_BOUNDS);
    CHECK(SUCCEEDED(Purchase(market.Get(), one)));
    CHECK(Balance(market.Get()) == 1500);
    CHECK(SUCCEEDED(Purchase(market.Get(), two)));
    CHECK(Balance(market.Get()) == 5000);
    CHECK(SUCCEEDED(market->GetAssetsAsync(0, 0, 0, nullptr, op.ReleaseAndGetAddressOf())));
    auto assets = Result<IAssetBalanceCollection>(op);
    auto view = Items<IAssetBalance>(assets);
    ComPtr<IAssetBalance> asset;
    CHECK(view && SUCCEEDED(view->GetAt(0, &asset)));
    CHECK(asset && SUCCEEDED(asset->put_Quantity(2000)));
    ComPtr<IInspectable> raw;
    CHECK(SUCCEEDED(assets->get_Items(&raw)));
    ComPtr<IAsyncAction> consume;
    CHECK(SUCCEEDED(market->ConsumeAssetsAsync(0, raw.Get(), &consume)) && consume && SUCCEEDED(consume->GetResults()));
    CHECK(Balance(market.Get()) == 3000);
    // Insufficient funds must not partially consume anything.
    CHECK(SUCCEEDED(asset->put_Quantity(3001)));
    CHECK(SUCCEEDED(market->ConsumeAssetsAsync(0, raw.Get(), consume.ReleaseAndGetAddressOf())) && consume &&
          FAILED(consume->GetResults()));
    CHECK(Balance(market.Get()) == 3000);
    CHECK(SUCCEEDED(asset->put_Quantity(3000)));
    CHECK(SUCCEEDED(market->ConsumeAssetsAsync(0, raw.Get(), consume.ReleaseAndGetAddressOf())) && consume &&
          SUCCEEDED(consume->GetResults()));
    CHECK(Balance(market.Get()) == 0);
    CHECK(SUCCEEDED(Purchase(market.Get(), durable)) && Balance(market.Get()) == 0);
    CHECK(SUCCEEDED(market->GetReceiptsAsync(0, 0, 0, nullptr, op.ReleaseAndGetAddressOf())));
    auto receipts = Items<IReceipt>(Result<IReceiptCollection>(op));
    CHECK(receipts && SUCCEEDED(receipts->get_Size(&size)) && size == 3);
    ComPtr<IReceipt> first, second;
    CHECK(SUCCEEDED(receipts->GetAt(0, &first)) && SUCCEEDED(receipts->GetAt(1, &second)));
    GUID tx1{}, tx2{};
    CHECK(first && SUCCEEDED(first->get_TransactionId(&tx1)));
    CHECK(second && SUCCEEDED(second->get_TransactionId(&tx2)) && tx1 != tx2);
    CHECK(SUCCEEDED(market->GetReceiptAsync(0, durable, op.ReleaseAndGetAddressOf())));
    auto receipt = Result<IReceipt>(op);
    CHECK(receipt && SUCCEEDED(receipt->get_OfferId(&offerId)) && offerId == durable);
    HString json;
    CHECK(receipt && SUCCEEDED(receipt->get_Json(json.GetAddressOf())) &&
          wcsstr(json.GetRawBuffer(nullptr), L"appxportableStub"));
    CHECK(SUCCEEDED(market->GetReceiptsAsync(0, 1, 1, nullptr, op.ReleaseAndGetAddressOf())));
    receipts = Items<IReceipt>(Result<IReceiptCollection>(op));
    CHECK(receipts && SUCCEEDED(receipts->get_Size(&size)) && size == 1);
    DateTime future{INT64_MAX};
    CHECK(SUCCEEDED(market->GetReceiptsWithDateFilterAsync(0, 0, 0, future, nullptr, op.ReleaseAndGetAddressOf())));
    receipts = Items<IReceipt>(Result<IReceiptCollection>(op));
    CHECK(receipts && SUCCEEDED(receipts->get_Size(&size)) && size == 0);
    // Successful operations enforce delegate assignment and closed-state rules.
    ComPtr<OperationAbi<IReceiptCollection>> typed;
    CHECK(SUCCEEDED(op.As(&typed)));
    CHECK(SUCCEEDED(typed->put_Completed(nullptr)));
    CHECK(typed->put_Completed(nullptr) == E_ILLEGAL_DELEGATE_ASSIGNMENT);
    ComPtr<IAsyncInfo> info;
    CHECK(SUCCEEDED(op.As(&info)) && SUCCEEDED(info->Close()));
    ComPtr<IReceiptCollection> closed;
    CHECK(typed->GetResults(&closed) == RO_E_CLOSED && !closed);
    CHECK(info->get_Status(nullptr) == E_POINTER);
    // The constructor used by the game's QueueAssetConsume must activate too.
    CHECK(SUCCEEDED(
        get(HStringReference(L"Microsoft.Xbox.Marketplace.AssetBalance").Get(), factory.ReleaseAndGetAddressOf())));
    ComPtr<IAssetBalanceFactory> assetFactory;
    CHECK(SUCCEEDED(factory.As(&assetFactory)));
    CHECK(SUCCEEDED(assetFactory->CreateAssetBalance(1, 100, op.ReleaseAndGetAddressOf())));
    CHECK(SUCCEEDED(op.As(&asset)));
    UINT32 quantity{};
    CHECK(SUCCEEDED(asset->get_Quantity(&quantity)) && quantity == 100);
    printf("Xbox Marketplace: %d failures\n", failures);
    return failures ? 1 : 0;
}
