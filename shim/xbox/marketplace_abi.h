#pragma once
// Xbox Marketplace ABI transcribed from the game's Microsoft.Xbox.winmd.
// Generic IIDs use WinRT runtime-class signatures, not interface signatures.
#include "xbox_abi.h"
namespace appx::xbox {
MIDL_INTERFACE("b68b1319-9524-40e2-9ae2-459908b2de28") IOffer : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_ContentCategory(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DownloadSizeInBytes(UINT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_OfferId(GUID*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_OfferName(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_OfferType(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_OfferImages(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DisplayPrice(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ReleaseDate(ABI::Windows::Foundation::DateTime*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SellText(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TitleId(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TitleName(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AverageUserRating(DOUBLE*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_UserRatingCount(UINT32*) = 0;
};
MIDL_INTERFACE("64316033-9488-48f3-8c23-6cd8352ab932") IReceipt : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_TransactionId(GUID*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_OfferId(GUID*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Xuid(UINT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PurchasingPartnerId(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TitleId(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PurchaseDate(ABI::Windows::Foundation::DateTime*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MediaInstanceUrls(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Json(HSTRING*) = 0;
};
MIDL_INTERFACE("ae09ce48-3ab9-4a70-ab51-d7ff94edabde") IAssetBalance : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Id(UINT32) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Quantity(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Quantity(UINT32) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Xuid(UINT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TitleId(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Timestamp(ABI::Windows::Foundation::DateTime*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Json(HSTRING*) = 0;
};
MIDL_INTERFACE("66de1986-539e-4a1f-9df6-acf028a170f7") IOfferImage : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_FileUrl(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Height(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Width(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Purpose(HSTRING*) = 0;
};
MIDL_INTERFACE("d8da7362-27b9-4860-855a-635232d29a4b") IOfferCollection : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Items(IInspectable**) = 0;
};
MIDL_INTERFACE("2225bf0e-3ef6-44c7-a057-71d5b750288e") IReceiptCollection : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Items(IInspectable**) = 0;
};
MIDL_INTERFACE("f8b77f84-3983-4b52-8b71-e9c3dabc8586") IAssetBalanceCollection : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Items(IInspectable**) = 0;
};
MIDL_INTERFACE("4622fc0c-219c-4e3a-bd95-04d5afd71c6b") IAssetBalanceFactory : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateAssetBalance(UINT32, UINT32, IInspectable**) = 0;
};
template <class T> struct VectorAbi;
template <class T> struct IterableAbi;
template <class T> struct IteratorAbi;
template <class T> struct OperationAbi;
template <> struct __declspec(uuid("4aad2c36-17d0-58d5-9b35-bce5fa6c15c2")) VectorAbi<IOffer> : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32, IOffer**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE IndexOf(IOffer*, UINT32*, boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32, UINT32, IOffer**, UINT32*) = 0;
};
template <> struct __declspec(uuid("d95f7c9e-951c-5012-97f9-698daaf244db")) IterableAbi<IOffer> : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE First(IteratorAbi<IOffer>**) = 0;
};
template <> struct __declspec(uuid("65a6886a-1546-5b73-addf-9ce9f81f1ce8")) IteratorAbi<IOffer> : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Current(IOffer**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveNext(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32, IOffer**, UINT32*) = 0;
};
template <> struct __declspec(uuid("55d16461-f230-55e7-9c99-dedf3bf9692d")) VectorAbi<IReceipt> : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32, IReceipt**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE IndexOf(IReceipt*, UINT32*, boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32, UINT32, IReceipt**, UINT32*) = 0;
};
template <> struct __declspec(uuid("d749d356-804f-5ce3-91f1-5ecc15c02a7e")) IterableAbi<IReceipt> : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE First(IteratorAbi<IReceipt>**) = 0;
};
template <> struct __declspec(uuid("30031755-2e95-5cf8-9e6f-1272d7fd9308")) IteratorAbi<IReceipt> : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Current(IReceipt**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveNext(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32, IReceipt**, UINT32*) = 0;
};
template <> struct __declspec(uuid("47f16fe9-252a-5aa6-b2e2-343fa27fdecb")) VectorAbi<IAssetBalance> : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32, IAssetBalance**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE IndexOf(IAssetBalance*, UINT32*, boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32, UINT32, IAssetBalance**, UINT32*) = 0;
};
template <> struct __declspec(uuid("687a5551-b50b-5100-aecf-b74c9d7c5a07")) IterableAbi<IAssetBalance> : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE First(IteratorAbi<IAssetBalance>**) = 0;
};
template <> struct __declspec(uuid("a3b619e9-1122-5256-bcd8-c04def1ff5e4")) IteratorAbi<IAssetBalance> : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Current(IAssetBalance**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveNext(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32, IAssetBalance**, UINT32*) = 0;
};
template <> struct __declspec(uuid("7ee5e7ed-4573-52b8-b5ef-82b102102bea")) VectorAbi<IOfferImage> : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32, IOfferImage**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE IndexOf(IOfferImage*, UINT32*, boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32, UINT32, IOfferImage**, UINT32*) = 0;
};
template <> struct __declspec(uuid("267e205c-5dd8-5119-a565-b1f07751ade5")) IterableAbi<IOfferImage> : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE First(IteratorAbi<IOfferImage>**) = 0;
};
template <> struct __declspec(uuid("190e12fd-58f1-583b-b412-364c55531544")) IteratorAbi<IOfferImage> : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Current(IOfferImage**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveNext(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32, IOfferImage**, UINT32*) = 0;
};
template <>
struct __declspec(uuid("87ba6d86-8b73-5513-9f18-0eaa1789cf47")) OperationAbi<IOfferCollection> : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(IOfferCollection**) = 0;
};
template <>
struct __declspec(uuid("cfa894ea-c465-5453-beae-c8cd526d46d9")) OperationAbi<IReceiptCollection> : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(IReceiptCollection**) = 0;
};
template <>
struct __declspec(uuid("51c837a3-2f02-534a-9f53-b299e0559cf6")) OperationAbi<IAssetBalanceCollection> : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(IAssetBalanceCollection**) = 0;
};
template <> struct __declspec(uuid("67d8362a-719e-5aa5-ad82-ae8dd8307908")) OperationAbi<IReceipt> : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(IReceipt**) = 0;
};
HRESULT MakeMarketplaceFactory(IActivationFactory** value);
HRESULT MakeAssetBalanceFactory(IActivationFactory** value);
}
