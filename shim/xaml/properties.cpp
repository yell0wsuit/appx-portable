#include "properties.h"
#include "com/composable.h"
#include "com/factory.h"
#include "host/dispatcher.h"
#include "registry.h"
#include <wrl/wrappers/corewrappers.h>
#include <mutex>
#include <string>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Interop;
using namespace ABI::Windows::Foundation;
namespace appx {
namespace {
MIDL_INTERFACE("D341E4E1-45E7-4659-A528-FB6AA1269243") IPropertyDefinition : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE DefaultValue(IInspectable * *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Validate(IInspectable * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Changed(IDependencyObject * owner, IInspectable * old, IInspectable * value) = 0;
};
// Internal contract with host/winrt/property_abi.cs; unlike an IInspectable
// class-name comparison, the CLR can validate base classes and generic types.
MIDL_INTERFACE("0B8297A1-48A9-4FF6-B911-BF34BC07059C") IManagedPropertyValue : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE IsInstance(HSTRING type, boolean * matches) = 0;
};
MIDL_INTERFACE("166751C2-70A9-4672-BE86-582638C7291F") IMetadataCallback : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Callback(IPropertyChangedCallback * *value) = 0;
};
class Unset final : public RuntimeClass<RuntimeClassFlags<WinRt>, IInspectable, FtmBase> {
    InspectableClass(L"Windows.UI.Xaml.DependencyProperty.UnsetValue", BaseTrust);
};
ComPtr<IInspectable> UnsetValue() {
    static auto* value = new ComPtr<IInspectable>(Make<Unset>());
    return *value;
}
bool Same(IUnknown* a, IUnknown* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    ComPtr<IUnknown> left, right;
    return SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&left))) && SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&right))) &&
           left.Get() == right.Get();
}
// Property values compare by value; arbitrary runtime objects compare by identity.
bool Equal(IInspectable* a, IInspectable* b) {
    if (Same(a, b)) return true;
    if (!a || !b) return false;
    ComPtr<IPropertyValue> left, right;
    PropertyType l{}, r{};
    if (FAILED(a->QueryInterface(IID_PPV_ARGS(&left))) || FAILED(b->QueryInterface(IID_PPV_ARGS(&right))) ||
        FAILED(left->get_Type(&l)) || FAILED(right->get_Type(&r)) || l != r)
        return false;
    switch (l) {
    case PropertyType_String: {
        HString x, y;
        INT32 order{};
        return SUCCEEDED(left->GetString(x.GetAddressOf())) && SUCCEEDED(right->GetString(y.GetAddressOf())) &&
               SUCCEEDED(WindowsCompareStringOrdinal(x.Get(), y.Get(), &order)) && order == 0;
    }
#define SCALAR(kind, type)                                                                                             \
    case PropertyType_##kind: {                                                                                        \
        type x{}, y{};                                                                                                 \
        return SUCCEEDED(left->Get##kind(&x)) && SUCCEEDED(right->Get##kind(&y)) && x == y;                            \
    }
        SCALAR(Boolean, boolean)
        SCALAR(Int32, INT32)
        SCALAR(UInt32, UINT32)
        SCALAR(Int64, INT64)
        SCALAR(UInt64, UINT64)
        SCALAR(Double, DOUBLE)
        SCALAR(Single, FLOAT)
#undef SCALAR
    default:
        return false;
    }
}
class Metadata final : public Composable<Metadata, IPropertyMetadata, IMetadataCallback> {
    ComPtr<IInspectable> value_;
    ComPtr<IPropertyChangedCallback> callback_;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.PropertyMetadata";
    explicit Metadata(IInspectable* outer) : Composable(outer) {}
    void Initialize(IInspectable* value, IPropertyChangedCallback* callback) {
        value_ = value;
        callback_ = callback;
    }
    HRESULT STDMETHODCALLTYPE get_DefaultValue(IInspectable** value) override {
        return value ? value_.CopyTo(value) : E_POINTER;
    }
    HRESULT STDMETHODCALLTYPE get_CreateDefaultValueCallback(ICreateDefaultValueCallback** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Callback(IPropertyChangedCallback** value) override {
        return value ? callback_.CopyTo(value) : E_POINTER;
    }
};
class MetadataFactory final : public Factory<IPropertyMetadataFactory> {
public:
    MetadataFactory() : Factory(Metadata::Name) {}
    HRESULT STDMETHODCALLTYPE CreateInstanceWithDefaultValue(IInspectable* value, IInspectable* outer,
                                                             IInspectable** inner,
                                                             IPropertyMetadata** result) override {
        return CreateInstanceWithDefaultValueAndCallback(value, nullptr, outer, inner, result);
    }
    HRESULT STDMETHODCALLTYPE CreateInstanceWithDefaultValueAndCallback(IInspectable* value,
                                                                        IPropertyChangedCallback* callback,
                                                                        IInspectable* outer, IInspectable** inner,
                                                                        IPropertyMetadata** result) override {
        auto hr = Compose<Metadata>(outer, inner, result);
        if (SUCCEEDED(hr)) static_cast<Metadata*>(*result)->Initialize(value, callback);
        return hr;
    }
};
class ChangedArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>, IDependencyPropertyChangedEventArgs> {
    InspectableClass(L"Windows.UI.Xaml.DependencyPropertyChangedEventArgs", BaseTrust);
    ComPtr<IDependencyProperty> property_;
    ComPtr<IInspectable> old_, value_;

public:
    ChangedArgs(IDependencyProperty* property, IInspectable* old, IInspectable* value)
        : property_(property), old_(old), value_(value) {}
    HRESULT STDMETHODCALLTYPE get_Property(IDependencyProperty** value) override {
        return value ? property_.CopyTo(value) : E_POINTER;
    }
    HRESULT STDMETHODCALLTYPE get_OldValue(IInspectable** value) override {
        return value ? old_.CopyTo(value) : E_POINTER;
    }
    HRESULT STDMETHODCALLTYPE get_NewValue(IInspectable** value) override {
        return value ? value_.CopyTo(value) : E_POINTER;
    }
};
class Property final
    : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IDependencyProperty, IPropertyDefinition, FtmBase> {
    InspectableClass(L"Windows.UI.Xaml.DependencyProperty", BaseTrust);
    ComPtr<IPropertyMetadata> metadata_;
    ComPtr<IPropertyChangedCallback> callback_;
    PropertyType type_{};
    HString customType_;
    bool custom_ = false;

public:
    HRESULT Initialize(TypeName type, IPropertyMetadata* metadata) {
        std::wstring name = WindowsGetStringRawBuffer(type.Name, nullptr);
        // Non-scalar/redirected types must acquire their own validation contract.
        if (name == L"Object" || name == L"System.Object")
            type_ = PropertyType_Inspectable;
        else if (type.Kind == TypeKind_Custom) {
            type_ = PropertyType_Inspectable;
            custom_ = true;
            auto hr = customType_.Set(type.Name);
            if (FAILED(hr)) return hr;
        } else if (type.Kind != TypeKind_Primitive) {
            Log("UNIMPL", "DependencyProperty type %ls", name.c_str());
            return E_NOTIMPL;
        }
#define TYPE(name) else if (nameString == L#name) type_ = PropertyType_##name;
        else {
            const auto& nameString = name;
            if (name == L"String") type_ = PropertyType_String;
            TYPE(Boolean)
            TYPE(Int32) TYPE(UInt32) TYPE(Int64) TYPE(UInt64) TYPE(Double) TYPE(Single) else return E_NOTIMPL;
        }
#undef TYPE
        if (metadata)
            metadata_ = metadata;
        else {
            ComPtr<IInspectable> value;
            ComPtr<IPropertyValueStatics> box;
            HRESULT hr = S_OK;
            if (type_ != PropertyType_String && type_ != PropertyType_Inspectable) {
                hr = RoGetActivationFactory(HStringReference(L"Windows.Foundation.PropertyValue").Get(),
                                            IID_PPV_ARGS(&box));
                if (FAILED(hr)) return hr;
                switch (type_) {
#define ZERO(kind)                                                                                                     \
    case PropertyType_##kind:                                                                                          \
        hr = box->Create##kind(0, &value);                                                                             \
        break;
                    ZERO(Boolean)
                    ZERO(Int32) ZERO(UInt32) ZERO(Int64) ZERO(UInt64) ZERO(Double) ZERO(Single)
#undef ZERO
                        default : return E_NOTIMPL;
                }
            }
            if (FAILED(hr)) return hr;
            ComPtr<IPropertyMetadata> info;
            hr = Compose<Metadata>(nullptr, nullptr, info.GetAddressOf());
            if (FAILED(hr)) return hr;
            static_cast<Metadata*>(info.Get())->Initialize(value.Get(), nullptr);
            metadata_ = info;
        }
        ComPtr<ICreateDefaultValueCallback> factory;
        auto hr = metadata_->get_CreateDefaultValueCallback(&factory);
        if (FAILED(hr)) return hr;
        if (factory) return E_NOTIMPL;
        ComPtr<IMetadataCallback> own;
        if (FAILED(metadata_.As(&own))) return E_NOTIMPL;
        hr = own->Callback(&callback_);
        if (FAILED(hr)) return hr;
        ComPtr<IInspectable> value;
        hr = metadata_->get_DefaultValue(&value);
        return FAILED(hr) ? hr : Validate(value.Get());
    }
    HRESULT STDMETHODCALLTYPE GetMetadata(TypeName, IPropertyMetadata** value) override {
        return value ? metadata_.CopyTo(value) : E_POINTER;
    }
    HRESULT STDMETHODCALLTYPE DefaultValue(IInspectable** value) override { return metadata_->get_DefaultValue(value); }
    HRESULT STDMETHODCALLTYPE Validate(IInspectable* value) override {
        if (Same(value, UnsetValue().Get())) return E_INVALIDARG;
        if (custom_ && value) {
            ComPtr<IManagedPropertyValue> managed;
            if (FAILED(value->QueryInterface(IID_PPV_ARGS(&managed)))) return E_NOTIMPL;
            boolean matches = false;
            auto hr = managed->IsInstance(customType_.Get(), &matches);
            return FAILED(hr) ? hr : matches ? S_OK : E_INVALIDARG;
        }
        if (type_ == PropertyType_Inspectable) return S_OK;
        if (!value) return type_ == PropertyType_String ? S_OK : E_INVALIDARG;
        ComPtr<IPropertyValue> boxed;
        PropertyType actual{};
        return SUCCEEDED(value->QueryInterface(IID_PPV_ARGS(&boxed))) && SUCCEEDED(boxed->get_Type(&actual)) &&
                       actual == type_
                   ? S_OK
                   : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Changed(IDependencyObject* owner, IInspectable* old, IInspectable* value) override {
        if (!callback_ || Equal(old, value)) return S_OK;
        auto args = Make<ChangedArgs>(this, old, value);
        return args ? callback_->Invoke(owner, args.Get()) : E_OUTOFMEMORY;
    }
};
struct Registration {
    std::mutex mutex;
    std::map<std::pair<std::wstring, std::wstring>, ComPtr<IDependencyProperty>> properties;
};
class PropertyFactory final : public Factory<IDependencyPropertyStatics> {
public:
    PropertyFactory() : Factory(L"Windows.UI.Xaml.DependencyProperty") {}
    HRESULT STDMETHODCALLTYPE get_UnsetValue(IInspectable** value) override {
        return value ? UnsetValue().CopyTo(value) : E_POINTER;
    }
    HRESULT STDMETHODCALLTYPE Register(HSTRING name, TypeName type, TypeName owner, IPropertyMetadata* metadata,
                                       IDependencyProperty** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (WindowsIsStringEmpty(name) || WindowsIsStringEmpty(type.Name) || WindowsIsStringEmpty(owner.Name))
            return E_INVALIDARG;
        try {
            // Registration owns properties for the process, as static DP fields do.
            static auto* registry = new Registration;
            const auto key = std::make_pair(std::wstring(WindowsGetStringRawBuffer(owner.Name, nullptr)),
                                            std::wstring(WindowsGetStringRawBuffer(name, nullptr)));
            // Do not call metadata/COM code while holding the registry lock.
            auto property = Make<Property>();
            if (!property) return E_OUTOFMEMORY;
            auto hr = property->Initialize(type, metadata);
            if (FAILED(hr)) return hr;
            std::lock_guard lock(registry->mutex);
            if (registry->properties.contains(key)) return E_INVALIDARG;
            registry->properties.emplace(key, property);
            return property.CopyTo(value);
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        }
    }
    HRESULT STDMETHODCALLTYPE RegisterAttached(HSTRING name, TypeName type, TypeName owner, IPropertyMetadata* metadata,
                                               IDependencyProperty** value) override {
        return Register(name, type, owner, metadata, value);
    }
};
HRESULT Definition(IDependencyProperty* property, ComPtr<IUnknown>& identity, ComPtr<IPropertyDefinition>& definition) {
    if (!property) return E_INVALIDARG;
    auto hr = property->QueryInterface(IID_PPV_ARGS(&identity));
    if (SUCCEEDED(hr)) hr = property->QueryInterface(IID_PPV_ARGS(&definition));
    return hr;
}
}
HRESULT PropertyValues::Get(IDependencyProperty* property, IInspectable** value, bool local) {
    if (!value) return E_POINTER;
    *value = nullptr;
    if (thread_ != GetCurrentThreadId()) return RPC_E_WRONG_THREAD;
    ComPtr<IUnknown> identity;
    ComPtr<IPropertyDefinition> definition;
    auto hr = Definition(property, identity, definition);
    if (FAILED(hr)) return hr;
    auto found = local_.find(identity.Get());
    if (found != local_.end()) return found->second.value.CopyTo(value);
    return local ? UnsetValue().CopyTo(value) : definition->DefaultValue(value);
}
HRESULT PropertyValues::Set(IDependencyObject* owner, IDependencyProperty* property, IInspectable* value, bool clear) {
    if (thread_ != GetCurrentThreadId()) return RPC_E_WRONG_THREAD;
    ComPtr<IUnknown> identity;
    ComPtr<IPropertyDefinition> definition;
    auto hr = Definition(property, identity, definition);
    if (FAILED(hr)) return hr;
    clear = clear || Same(value, UnsetValue().Get());
    if (!clear && FAILED(hr = definition->Validate(value))) return hr;
    ComPtr<IInspectable> old;
    hr = Get(property, &old);
    if (FAILED(hr)) return hr;
    try {
        if (clear)
            local_.erase(identity.Get());
        else
            local_[identity.Get()] = Entry{property, value};
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    ComPtr<IInspectable> next;
    hr = Get(property, &next);
    return FAILED(hr) ? hr : definition->Changed(owner, old.Get(), next.Get());
}
namespace {
HRESULT MakePropertyFactory(IActivationFactory** value) {
    auto factory = Make<PropertyFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
HRESULT MakeMetadataFactory(IActivationFactory** value) {
    auto factory = Make<MetadataFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
const ShimClass dependencyPropertyClass{L"Windows.UI.Xaml.DependencyProperty", MakePropertyFactory};
const ShimClass propertyMetadataClass{L"Windows.UI.Xaml.PropertyMetadata", MakeMetadataFactory};
}
