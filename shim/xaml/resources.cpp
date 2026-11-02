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
#include <set>
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
    ResourceFactory factory; // Set until a source entry is first built.
};
MIDL_INTERFACE("7A86EACA-AD32-491F-AC74-CAF408CD4616") ISourceDictionary : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE AddDeferred(IInspectable * key, const std::wstring& name,
                                                  ResourceFactory factory) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSourceParts(const std::wstring& source,
                                                     std::vector<ComPtr<IResourceDictionary>> merged,
                                                     std::set<std::wstring> themeKeys) = 0;
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
class ResourceDictionary final : public Composable<ResourceDictionary, IResourceDictionary, Map, Iterable,
                                                    DefaultIDependencyObject, ISourceDictionary> {
    std::map<std::wstring, Entry> entries_;
    // Source dictionaries: lookup checks own entries, then theme keys, then the
    // merged dictionaries from last to first, each with its own full lookup.
    std::vector<ComPtr<IResourceDictionary>> merged_;
    std::set<std::wstring> themeKeys_;
    std::wstring source_;
    PropertyValues properties_;
    const DWORD thread_ = GetCurrentThreadId();
    HRESULT CheckThread() const { return GetCurrentThreadId() == thread_ ? S_OK : RPC_E_WRONG_THREAD; }
    // The factory may reenter this dictionary, so the entry is found again afterwards.
    HRESULT Materialize(const std::wstring& name) {
        auto found = entries_.find(name);
        if (found == entries_.end() || !found->second.factory) return S_OK;
        auto factory = found->second.factory;
        ComPtr<IInspectable> value;
        auto hr = factory(value);
        if (FAILED(hr)) return hr;
        found = entries_.find(name);
        if (found != entries_.end() && found->second.factory) {
            found->second.value = std::move(value);
            found->second.factory = nullptr;
        }
        return S_OK;
    }
    HRESULT Copy(std::shared_ptr<Snapshot>& snapshot) {
        std::vector<std::wstring> names;
        for (const auto& entry : entries_) names.push_back(entry.first);
        for (const auto& name : names) {
            auto hr = Materialize(name);
            if (FAILED(hr)) return hr;
        }
        snapshot = std::make_shared<Snapshot>();
        for (const auto& entry : entries_) snapshot->entries.push_back(entry.second);
        return S_OK;
    }
    HRESULT Find(IInspectable* key, const std::wstring& name, IInspectable** value) {
        if (entries_.count(name)) {
            auto hr = Materialize(name);
            if (FAILED(hr)) return hr;
            auto found = entries_.find(name);
            return found == entries_.end() ? E_BOUNDS : found->second.value.CopyTo(value);
        }
        if (themeKeys_.count(name)) {
            Log("FAIL", "ResourceDictionary.ThemeDictionaries lookup %ls hr=%08lx", name.c_str(), E_NOTIMPL);
            return E_NOTIMPL;
        }
        for (auto dictionary = merged_.rbegin(); dictionary != merged_.rend(); ++dictionary) {
            ComPtr<Map> map;
            auto hr = dictionary->As(&map);
            if (SUCCEEDED(hr)) hr = map->Lookup(key, value);
            if (hr != E_BOUNDS) return hr;
        }
        return E_BOUNDS;
    }

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.ResourceDictionary";
    explicit ResourceDictionary(IInspectable* outer) : Composable(outer) {}
    HRESULT STDMETHODCALLTYPE get_Source(IUriRuntimeClass** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr) || source_.empty()) return hr;
        return Guard([&]() -> HRESULT {
            ComPtr<IUriRuntimeClassFactory> factory;
            auto result = RoGetActivationFactory(Wrappers::HStringReference(L"Windows.Foundation.Uri").Get(),
                                                 IID_PPV_ARGS(&factory));
            auto uri = L"ms-appx:///" + source_;
            return FAILED(result) ? result : factory->CreateUri(Wrappers::HStringReference(uri.c_str()).Get(), value);
        });
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
            return FAILED(result) ? result : Find(key, name, value);
        });
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = static_cast<UINT32>(entries_.size());
        return hr;
    }
    // Answers from the key index without building deferred entries.
    HRESULT STDMETHODCALLTYPE HasKey(IInspectable* key, boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return Guard([&]() -> HRESULT {
            std::wstring name;
            auto result = ReadKey(key, name);
            if (FAILED(result)) return result;
            if (entries_.count(name) || themeKeys_.count(name)) {
                *value = true;
                return S_OK;
            }
            for (auto dictionary = merged_.rbegin(); dictionary != merged_.rend() && !*value; ++dictionary) {
                ComPtr<Map> map;
                result = dictionary->As(&map);
                if (SUCCEEDED(result)) result = map->HasKey(key, value);
                if (FAILED(result)) return result;
            }
            return S_OK;
        });
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
                found->second.factory = nullptr;
                *replaced = true;
            } else
                entries_.emplace(name, Entry{name, key, value, nullptr});
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
            std::shared_ptr<Snapshot> snapshot;
            auto result = Copy(snapshot);
            if (FAILED(result)) return result;
            auto view = Make<ResourceView>(std::move(snapshot));
            return view ? view.CopyTo(value) : E_OUTOFMEMORY;
        });
    }
    HRESULT STDMETHODCALLTYPE First(Iterator** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return Guard([&]() -> HRESULT {
            std::shared_ptr<Snapshot> snapshot;
            auto result = Copy(snapshot);
            if (FAILED(result)) return result;
            auto iterator = Make<ResourceIterator>(std::move(snapshot));
            return iterator ? iterator.CopyTo(value) : E_OUTOFMEMORY;
        });
    }
    HRESULT STDMETHODCALLTYPE AddDeferred(IInspectable* key, const std::wstring& name,
                                          ResourceFactory factory) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return Guard([&]() -> HRESULT {
            if (!factory) return E_INVALIDARG;
            return entries_.emplace(name, Entry{name, key, nullptr, std::move(factory)}).second ? S_OK : E_INVALIDARG;
        });
    }
    HRESULT STDMETHODCALLTYPE SetSourceParts(const std::wstring& source, std::vector<ComPtr<IResourceDictionary>> merged,
                                             std::set<std::wstring> themeKeys) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return Guard([&]() -> HRESULT {
            source_ = source;
            merged_ = std::move(merged);
            themeKeys_ = std::move(themeKeys);
            return S_OK;
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
namespace {
HRESULT BoxKey(HSTRING key, ComPtr<IInspectable>& boxed) {
    ComPtr<IPropertyValueStatics> factory;
    auto hr = RoGetActivationFactory(Wrappers::HStringReference(L"Windows.Foundation.PropertyValue").Get(),
                                     IID_PPV_ARGS(&factory));
    return FAILED(hr) ? hr : factory->CreateString(key, &boxed);
}
}
HRESULT AddSourceResource(IResourceDictionary* dictionary, HSTRING key, IInspectable* value) {
    ComPtr<IInspectable> boxed;
    auto hr = BoxKey(key, boxed);
    ComPtr<Map> map;
    if (SUCCEEDED(hr)) hr = dictionary->QueryInterface(IID_PPV_ARGS(&map));
    boolean replaced = false;
    if (SUCCEEDED(hr)) hr = map->Insert(boxed.Get(), value, &replaced);
    return SUCCEEDED(hr) && replaced ? E_INVALIDARG : hr;
}
HRESULT AddDeferredSourceResource(IResourceDictionary* dictionary, const std::wstring& key, ResourceFactory factory) {
    ComPtr<IInspectable> boxed;
    auto hr = BoxKey(Wrappers::HStringReference(key.c_str(), static_cast<unsigned>(key.size())).Get(), boxed);
    ComPtr<ISourceDictionary> source;
    if (SUCCEEDED(hr)) hr = dictionary->QueryInterface(IID_PPV_ARGS(&source));
    return FAILED(hr) ? hr : source->AddDeferred(boxed.Get(), key, std::move(factory));
}
HRESULT SetSourceDictionaryParts(IResourceDictionary* dictionary, const std::wstring& source,
                                 std::vector<ComPtr<IResourceDictionary>> merged, std::set<std::wstring> themeKeys) {
    ComPtr<ISourceDictionary> parts;
    auto hr = dictionary->QueryInterface(IID_PPV_ARGS(&parts));
    return FAILED(hr) ? hr : parts->SetSourceParts(source, std::move(merged), std::move(themeKeys));
}
const ShimClass resourceDictionaryClass{ResourceDictionary::Name, MakeResourceDictionaryFactory,
                                        ShimPolicy::FillMissing};
}
