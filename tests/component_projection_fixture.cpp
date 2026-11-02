// Independent native ABI contract for the generated synthetic WinMD.
#include <windows.h>
#include <roapi.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <vector>
using namespace Microsoft::WRL;
using Microsoft::WRL::Wrappers::HString;
MIDL_INTERFACE("723d9b38-bb73-463e-adc8-25b46f56121a") IChanged : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(INT32, BYTE, HSTRING, HSTRING*) = 0;
};
MIDL_INTERFACE("239635fc-88f0-4fd6-80b1-b21ac63c939a") INode : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Read(INT32*) = 0;
};
MIDL_INTERFACE("45684efa-c695-4027-8fc4-51bf12a079ac") IExternalChanged : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(INode*) = 0;
};
class Node final : public RuntimeClass<RuntimeClassFlags<WinRt>, INode> {
    InspectableClass(L"Example.External.Node", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE Read(INT32* result) override {
        *result = 73;
        return S_OK;
    }
};
class NodeFactory final : public ActivationFactory<Implements<IActivationFactory>> {
public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) noexcept override {
        return Make<Node>().CopyTo(value);
    }
};
MIDL_INTERFACE("2fc62e47-e1ce-471d-9098-014b39ca938e") IEngine : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Echo(INT32, INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetState(BYTE, DOUBLE, UINT32) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetState(DOUBLE*) = 0;
    virtual HRESULT STDMETHODCALLTYPE EchoText(HSTRING, HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SumBytes(UINT32, const BYTE*, UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Add(IChanged*, INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Remove(INT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE Raise(INT32, BYTE, HSTRING, HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Fail() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetNode(INode*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNode(INode**) = 0;
    virtual HRESULT STDMETHODCALLTYPE NextMode(INT32, INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddExternal(IExternalChanged*, INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveExternal(INT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE RaiseExternal() = 0;
};
class Engine final : public RuntimeClass<RuntimeClassFlags<WinRt>, IEngine> {
    InspectableClass(L"Example.Component.Engine", BaseTrust);
    ComPtr<INode> node_;
    ComPtr<IExternalChanged> external_;
    DOUBLE state_ = 0;
    INT64 token_ = 0;
    std::vector<std::pair<INT64, ComPtr<IChanged>>> handlers_;

public:
    HRESULT STDMETHODCALLTYPE Echo(INT32 value, INT32* result) override {
        *result = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetState(BYTE active, DOUBLE value, UINT32 count) override {
        if (active > 1) return E_INVALIDARG;
        state_ = active ? value + count : -value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetState(DOUBLE* result) override {
        *result = state_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EchoText(HSTRING value, HSTRING* result) override {
        return WindowsDuplicateString(value, result);
    }
    HRESULT STDMETHODCALLTYPE SumBytes(UINT32 count, const BYTE* value, UINT32* result) override {
        if (count && !value) return E_POINTER;
        *result = 0;
        for (UINT32 i = 0; i < count; ++i) *result += value[i];
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Add(IChanged* handler, INT64* token) override {
        if (!handler) return E_POINTER;
        *token = ++token_;
        handlers_.emplace_back(*token, handler);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Remove(INT64 token) override {
        for (auto i = handlers_.begin(); i != handlers_.end(); ++i)
            if (i->first == token) {
                handlers_.erase(i);
                return S_OK;
            }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Raise(INT32 count, BYTE active, HSTRING name, HSTRING* result) override {
        *result = nullptr;
        auto snapshot = handlers_;
        for (auto& handler : snapshot) {
            HString current;
            auto hr = handler.second->Invoke(count, active, name, current.GetAddressOf());
            if (FAILED(hr)) {
                WindowsDeleteString(*result);
                *result = nullptr;
                return hr;
            }
            WindowsDeleteString(*result);
            *result = current.Detach();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Fail() override { return E_INVALIDARG; }
    HRESULT STDMETHODCALLTYPE SetNode(INode* node) override {
        // Validate the independently declared IID rather than accepting a layout cast.
        node_.Reset();
        return node ? node->QueryInterface(IID_PPV_ARGS(&node_)) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNode(INode** result) override { return node_.CopyTo(result); }
    HRESULT STDMETHODCALLTYPE NextMode(INT32 mode, INT32* result) override {
        if (mode != 7) return E_INVALIDARG;
        *result = 19;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AddExternal(IExternalChanged* handler, INT64* token) override {
        external_ = handler;
        *token = 9001;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RemoveExternal(INT64 token) override {
        if (token != 9001) return E_INVALIDARG;
        external_.Reset();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RaiseExternal() override { return external_ ? external_->Invoke(node_.Get()) : S_OK; }
};
// Like a C++/CX engine creating a Platform::Exception in its constructor: the
MIDL_INTERFACE("8c1f2a90-6d3e-4b57-9e21-3a4c5d6e7f80") IGauge : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Name(HSTRING*) = 0;
};
MIDL_INTERFACE("5b2e7c41-9a0d-4f3b-8c62-1d4e5f6a7b8c") IGaugeFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Create(HSTRING, IGauge**) = 0;
};
class Gauge final : public RuntimeClass<RuntimeClassFlags<WinRt>, IGauge> {
    InspectableClass(L"Example.Component.Gauge", BaseTrust);
    HString name_;

public:
    explicit Gauge(HSTRING name) { name_.Set(name); }
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* value) override { return name_.CopyTo(value); }
};
class GaugeFactory final : public ActivationFactory<Implements<IActivationFactory>, IGaugeFactory> {
public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) noexcept override {
        *value = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Create(HSTRING name, IGauge** value) override { return Make<Gauge>(name).CopyTo(value); }
};
// call goes through a dependency the component, not the game, loaded.
extern "C" HRESULT __cdecl ReportComponentFailure();
class EngineFactory final : public ActivationFactory<Implements<IActivationFactory>> {
public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) noexcept override {
        auto hr = ReportComponentFailure();
        return FAILED(hr) ? hr : Make<Engine>().CopyTo(value);
    }
};
extern "C" HRESULT WINAPI DllGetActivationFactory(HSTRING name, IActivationFactory** factory) {
    *factory = nullptr;
    if (wcscmp(WindowsGetStringRawBuffer(name, nullptr), L"Example.External.Node") == 0)
        return Make<NodeFactory>().CopyTo(factory);
    if (wcscmp(WindowsGetStringRawBuffer(name, nullptr), L"Example.Component.Gauge") == 0)
        return Make<GaugeFactory>().CopyTo(factory);
    if (wcscmp(WindowsGetStringRawBuffer(name, nullptr), L"Example.Component.Engine") != 0)
        return CLASS_E_CLASSNOTAVAILABLE;
    return Make<EngineFactory>().CopyTo(factory);
}
