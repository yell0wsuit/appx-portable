#include "resources.h"
#include "generated/xaml.h"
#include "properties.h"
#include "com/composable.h"
#include "com/factory.h"
#include "registry.h"
#include <roapi.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <algorithm>
#include <map>
#include <memory>
#include <vector>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::UI::Xaml;
namespace appx {
namespace {
using Map = IMap<IInspectable*, IInspectable*>;
using View = IMapView<IInspectable*, IInspectable*>;
using Pair = IKeyValuePair<IInspectable*, IInspectable*>;
using Iterable = IIterable<Pair*>;
using Iterator = IIterator<Pair*>;
struct Entry {
    std::wstring name;
    ComPtr<IInspectable> key, value;
};
template <class F> HRESULT Guard(F&& action) {
    try {
        return action();
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
}
HRESULT ReadKey(IInspectable* key, std::wstring& name) {
    if (!key) return E_INVALIDARG;
    ComPtr<IPropertyValue> property;
    auto hr = key->QueryInterface(IID_PPV_ARGS(&property));
    if (hr == E_NOINTERFACE) {
        Log("FAIL", "ResourceDictionary key interface hr=%08lx", E_NOTIMPL);
        return E_NOTIMPL;
    }
    if (FAILED(hr)) return hr;
    PropertyType type;
    hr = property->get_Type(&type);
    if (FAILED(hr)) return hr;
    if (type != PropertyType_String) {
        Log("FAIL", "ResourceDictionary key type %d hr=%08lx", type, E_NOTIMPL);
        return E_NOTIMPL;
    }
    Wrappers::HString text;
    hr = property->GetString(text.GetAddressOf());
    if (SUCCEEDED(hr)) name.assign(text.GetRawBuffer(nullptr), text.Length());
    return hr;
}
struct Snapshot {
    std::vector<Entry> entries;
    const DWORD thread = GetCurrentThreadId();
    HRESULT CheckThread() const { return GetCurrentThreadId() == thread ? S_OK : RPC_E_WRONG_THREAD; }
};
class ResourcePair final : public RuntimeClass<RuntimeClassFlags<WinRt>, Pair> {
    InspectableClass(L"AppxPortable.Xaml.ResourcePair", BaseTrust);
    Entry entry_;

public:
    explicit ResourcePair(const Entry& entry) : entry_(entry) {}
    HRESULT STDMETHODCALLTYPE get_Key(IInspectable** value) override {
        if (!value) return E_POINTER;
        return entry_.key.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_Value(IInspectable** value) override {
        if (!value) return E_POINTER;
        return entry_.value.CopyTo(value);
    }
};
class ResourceIterator final : public RuntimeClass<RuntimeClassFlags<WinRt>, Iterator> {
    InspectableClass(L"AppxPortable.Xaml.ResourceIterator", BaseTrust);
    std::shared_ptr<Snapshot> snapshot_;
    size_t position_ = 0;

public:
    explicit ResourceIterator(std::shared_ptr<Snapshot> value) : snapshot_(std::move(value)) {}
    HRESULT STDMETHODCALLTYPE get_Current(Pair** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = snapshot_->CheckThread();
        if (FAILED(hr)) return hr;
        if (position_ >= snapshot_->entries.size()) return E_BOUNDS;
        return Guard([&]() -> HRESULT {
            auto pair = Make<ResourcePair>(snapshot_->entries[position_]);
            return pair ? pair.CopyTo(value) : E_OUTOFMEMORY;
        });
    }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        auto hr = snapshot_->CheckThread();
        if (SUCCEEDED(hr)) *value = position_ < snapshot_->entries.size();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        auto hr = snapshot_->CheckThread();
        if (FAILED(hr)) return hr;
        if (position_ < snapshot_->entries.size()) ++position_;
        return get_HasCurrent(value);
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 capacity, Pair** values, UINT32* actual) override {
        if (!actual) return E_POINTER;
        *actual = 0;
        if (capacity && !values) return E_POINTER;
        auto hr = snapshot_->CheckThread();
        if (FAILED(hr)) return hr;
        return Guard([&]() -> HRESULT {
            const size_t count = std::min<size_t>(capacity, snapshot_->entries.size() - position_);
            std::vector<ComPtr<ResourcePair>> pairs;
            pairs.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                auto pair = Make<ResourcePair>(snapshot_->entries[position_ + i]);
                if (!pair) return E_OUTOFMEMORY;
                pairs.push_back(std::move(pair));
            }
            for (size_t i = 0; i < count; ++i) pairs[i].CopyTo(&values[i]);
            position_ += count;
            *actual = static_cast<UINT32>(count);
            return S_OK;
        });
    }
};
class ResourceView final : public RuntimeClass<RuntimeClassFlags<WinRt>, View, Iterable> {
    InspectableClass(L"AppxPortable.Xaml.ResourceView", BaseTrust);
    std::shared_ptr<Snapshot> snapshot_;

public:
    explicit ResourceView(std::shared_ptr<Snapshot> value) : snapshot_(std::move(value)) {}
    HRESULT STDMETHODCALLTYPE Lookup(IInspectable* key, IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = snapshot_->CheckThread();
        if (FAILED(hr)) return hr;
        return Guard([&]() -> HRESULT {
            std::wstring name;
            auto result = ReadKey(key, name);
            if (FAILED(result)) return result;
            auto found = std::find_if(snapshot_->entries.begin(), snapshot_->entries.end(),
                                      [&](const Entry& entry) { return entry.name == name; });
            return found == snapshot_->entries.end() ? E_BOUNDS : found->value.CopyTo(value);
        });
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        auto hr = snapshot_->CheckThread();
        if (SUCCEEDED(hr)) *value = static_cast<UINT32>(snapshot_->entries.size());
        return hr;
    }
    HRESULT STDMETHODCALLTYPE HasKey(IInspectable* key, boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        ComPtr<IInspectable> ignored;
        auto hr = Lookup(key, &ignored);
        if (hr == E_BOUNDS) return S_OK;
        if (SUCCEEDED(hr)) *value = true;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Split(View** first, View** second) override {
        if (!first || !second) return E_POINTER;
        *first = *second = nullptr;
        auto hr = snapshot_->CheckThread();
        if (FAILED(hr) || snapshot_->entries.size() < 2) return hr;
        return Guard([&]() -> HRESULT {
            auto left = std::make_shared<Snapshot>(), right = std::make_shared<Snapshot>();
            auto middle = snapshot_->entries.begin() + snapshot_->entries.size() / 2;
            left->entries.assign(snapshot_->entries.begin(), middle);
            right->entries.assign(middle, snapshot_->entries.end());
            auto a = Make<ResourceView>(left), b = Make<ResourceView>(right);
            if (!a || !b) return E_OUTOFMEMORY;
            a.CopyTo(first);
            b.CopyTo(second);
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE First(Iterator** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = snapshot_->CheckThread();
        if (FAILED(hr)) return hr;
        auto iterator = Make<ResourceIterator>(snapshot_);
        return iterator ? iterator.CopyTo(value) : E_OUTOFMEMORY;
    }
};
class ResourceDictionary final
    : public Composable<ResourceDictionary, IResourceDictionary, Map, Iterable, DefaultIDependencyObject> {
    std::map<std::wstring, Entry> entries_;
    PropertyValues properties_;
    const DWORD thread_ = GetCurrentThreadId();
    HRESULT CheckThread() const { return GetCurrentThreadId() == thread_ ? S_OK : RPC_E_WRONG_THREAD; }
    std::shared_ptr<Snapshot> Copy() {
        auto snapshot = std::make_shared<Snapshot>();
        for (const auto& entry : entries_) snapshot->entries.push_back(entry.second);
        return snapshot;
    }

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.ResourceDictionary";
    explicit ResourceDictionary(IInspectable* outer) : Composable(outer) {}
    HRESULT STDMETHODCALLTYPE get_Source(IUriRuntimeClass** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return CheckThread();
    }
    HRESULT STDMETHODCALLTYPE put_Source(IUriRuntimeClass* value) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (!value) return S_OK;
        Log("FAIL", "ResourceDictionary.Source hr=%08lx", E_NOTIMPL);
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE
    get_MergedDictionaries(IVector<ABI::Windows::UI::Xaml::ResourceDictionary*>** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        Log("FAIL", "ResourceDictionary.MergedDictionaries hr=%08lx", E_NOTIMPL);
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE get_ThemeDictionaries(Map** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        Log("FAIL", "ResourceDictionary.ThemeDictionaries hr=%08lx", E_NOTIMPL);
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetValue(IDependencyProperty* property, IInspectable** value) override {
        return properties_.Get(property, value);
    }
    HRESULT STDMETHODCALLTYPE SetValue(IDependencyProperty* property, IInspectable* value) override {
        return properties_.Set(this, property, value);
    }
    HRESULT STDMETHODCALLTYPE ClearValue(IDependencyProperty* property) override {
        return properties_.Set(this, property, nullptr, true);
    }
    HRESULT STDMETHODCALLTYPE ReadLocalValue(IDependencyProperty* property, IInspectable** value) override {
        return properties_.Get(property, value, true);
    }
    HRESULT STDMETHODCALLTYPE GetAnimationBaseValue(IDependencyProperty* property, IInspectable** value) override {
        return properties_.Get(property, value);
    }
    HRESULT STDMETHODCALLTYPE Lookup(IInspectable* key, IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return Guard([&]() -> HRESULT {
            std::wstring name;
            auto result = ReadKey(key, name);
            if (FAILED(result)) return result;
            auto found = entries_.find(name);
            return found == entries_.end() ? E_BOUNDS : found->second.value.CopyTo(value);
        });
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = static_cast<UINT32>(entries_.size());
        return hr;
    }
    HRESULT STDMETHODCALLTYPE HasKey(IInspectable* key, boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        ComPtr<IInspectable> ignored;
        auto hr = Lookup(key, &ignored);
        if (hr == E_BOUNDS) return S_OK;
        if (SUCCEEDED(hr)) *value = true;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Insert(IInspectable* key, IInspectable* value, boolean* replaced) override {
        if (!replaced) return E_POINTER;
        *replaced = false;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return Guard([&]() -> HRESULT {
            std::wstring name;
            auto result = ReadKey(key, name);
            if (FAILED(result)) return result;
            auto found = entries_.find(name);
            if (found != entries_.end()) {
                found->second.value = value;
                *replaced = true;
            } else
                entries_.emplace(name, Entry{name, key, value});
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Remove(IInspectable* key) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return Guard([&]() -> HRESULT {
            std::wstring name;
            auto result = ReadKey(key, name);
            if (FAILED(result)) return result;
            return entries_.erase(name) ? S_OK : E_BOUNDS;
        });
    }
    HRESULT STDMETHODCALLTYPE Clear() override {
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) entries_.clear();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetView(View** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return Guard([&]() -> HRESULT {
            auto view = Make<ResourceView>(Copy());
            return view ? view.CopyTo(value) : E_OUTOFMEMORY;
        });
    }
    HRESULT STDMETHODCALLTYPE First(Iterator** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return Guard([&]() -> HRESULT {
            auto iterator = Make<ResourceIterator>(Copy());
            return iterator ? iterator.CopyTo(value) : E_OUTOFMEMORY;
        });
    }
};
class ResourceDictionaryFactory final : public Factory<IResourceDictionaryFactory> {
public:
    ResourceDictionaryFactory() : Factory(ResourceDictionary::Name) {}
    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* outer, IInspectable** inner,
                                             IResourceDictionary** value) override {
        return Guard([&]() -> HRESULT { return Compose<ResourceDictionary>(outer, inner, value); });
    }
};
HRESULT MakeResourceDictionaryFactory(IActivationFactory** value) {
    auto factory = Make<ResourceDictionaryFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
HRESULT MakeResourceDictionary(IResourceDictionary** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    return Guard([&]() -> HRESULT { return Compose<ResourceDictionary>(nullptr, nullptr, value); });
}
HRESULT AddSourceResource(IResourceDictionary* dictionary, HSTRING key, IInspectable* value) {
    ComPtr<IPropertyValueStatics> factory;
    auto hr = RoGetActivationFactory(Wrappers::HStringReference(L"Windows.Foundation.PropertyValue").Get(),
                                     IID_PPV_ARGS(&factory));
    ComPtr<IInspectable> boxed;
    if (SUCCEEDED(hr)) hr = factory->CreateString(key, &boxed);
    ComPtr<Map> map;
    if (SUCCEEDED(hr)) hr = dictionary->QueryInterface(IID_PPV_ARGS(&map));
    boolean replaced = false;
    if (SUCCEEDED(hr)) hr = map->Insert(boxed.Get(), value, &replaced);
    return SUCCEEDED(hr) && replaced ? E_INVALIDARG : hr;
}
const ShimClass resourceDictionaryClass{ResourceDictionary::Name, MakeResourceDictionaryFactory,
                                        ShimPolicy::FillMissing};
}
