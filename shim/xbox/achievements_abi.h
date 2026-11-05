#pragma once
#include "marketplace_abi.h"
namespace appx::xbox {
// Microsoft.Xbox.winmd: object results use IInspectable* at the ABI boundary.
// Collection generic IIDs use the runtime-class signature
// rc(Microsoft.Xbox.Achievement;{843a65e0-8ed2-45a3-8524-6983a9ad7fdd}).
MIDL_INTERFACE("cd8ef6ea-e325-461d-869f-cce8bca68605") IAchievementCollection : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Items(IInspectable * *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TotalRecords(UINT32 * value) = 0;
};
MIDL_INTERFACE("8fcedd9d-0fda-541f-99d2-6512039ba5d3") IAchievementIterator : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Current(IInspectable * *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveNext(boolean * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32 capacity, IInspectable * *values, UINT32 * actual) = 0;
};
MIDL_INTERFACE("42416f6a-3391-581e-b5ef-e09956a0925c") IAchievementIterable : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE First(IAchievementIterator * *value) = 0;
};
MIDL_INTERFACE("0a130faa-6623-54df-8099-b2b78581bdab") IAchievementVectorView : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, IInspectable * *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32 * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE IndexOf(IInspectable * item, UINT32 * index, boolean * found) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32 start, UINT32 capacity, IInspectable * *values,
                                              UINT32 * actual) = 0;
};
template <>
struct __declspec(uuid("94bff9be-9797-56a4-b971-f6113304af0d")) OperationAbi<IAchievementCollection> : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(IAchievementCollection** value) = 0;
};
}
