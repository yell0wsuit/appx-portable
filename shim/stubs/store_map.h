#pragma once
#include <wrl.h>
#include "log.h"
#include <winstring.h>
#include <algorithm>
#include <string>
#include <vector>
namespace appx {
namespace store {
using namespace Microsoft::WRL;
inline HRESULT CopyString(const std::wstring& text, HSTRING* value) {
    if (!value) return E_POINTER;
    return WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), value);
}
// Immutable key snapshots shared by offline Store listings and product licenses.
template <class Pair, class Value, class Item>
class StorePair final : public RuntimeClass<RuntimeClassFlags<WinRt>, Pair, FtmBase> {
    using RuntimeClassT = RuntimeClass<RuntimeClassFlags<WinRt>, Pair, FtmBase>;
    InspectableClass(L"Windows.Foundation.Collections.IKeyValuePair`2", BaseTrust);
    std::wstring id_;

public:
    explicit StorePair(std::wstring id) : id_(std::move(id)) {}
    HRESULT STDMETHODCALLTYPE get_Key(HSTRING* value) override { return CopyString(id_, value); }
    HRESULT STDMETHODCALLTYPE get_Value(Value** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto product = Make<Item>(id_);
        return product ? product.CopyTo(value) : E_OUTOFMEMORY;
    }
};
template <class Iterator, class Pair, class Value, class Item>
class StoreIterator final : public RuntimeClass<RuntimeClassFlags<WinRt>, Iterator, FtmBase> {
    using RuntimeClassT = RuntimeClass<RuntimeClassFlags<WinRt>, Iterator, FtmBase>;
    InspectableClass(L"Windows.Foundation.Collections.IIterator`1", BaseTrust);
    std::vector<std::wstring> ids_;
    size_t index_ = 0;

public:
    explicit StoreIterator(std::vector<std::wstring> ids) : ids_(std::move(ids)) {}
    HRESULT STDMETHODCALLTYPE get_Current(Pair** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (index_ >= ids_.size()) return E_BOUNDS;
        auto pair = Make<StorePair<Pair, Value, Item>>(ids_[index_]);
        return pair ? pair.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* value) override {
        if (!value) return E_POINTER;
        *value = index_ < ids_.size();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* value) override {
        if (!value) return E_POINTER;
        if (index_ < ids_.size()) ++index_;
        return get_HasCurrent(value);
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 capacity, Pair** values, UINT32* actual) override {
        if (!actual) return E_POINTER;
        *actual = 0;
        if (capacity && !values) return E_POINTER;
        while (*actual < capacity && index_ < ids_.size()) {
            auto hr = get_Current(&values[*actual]);
            if (FAILED(hr)) return hr;
            ++*actual;
            ++index_;
        }
        return S_OK;
    }
};
template <class Map, class Iterable, class Iterator, class Pair, class Value, class Item>
class StoreMap final : public RuntimeClass<RuntimeClassFlags<WinRt>, Map, Iterable, FtmBase> {
    using RuntimeClassT = RuntimeClass<RuntimeClassFlags<WinRt>, Map, Iterable, FtmBase>;
    InspectableClass(L"Windows.Foundation.Collections.IMapView`2", BaseTrust);
    std::vector<std::wstring> ids_;

public:
    explicit StoreMap(std::vector<std::wstring> ids) : ids_(std::move(ids)) {}
    HRESULT STDMETHODCALLTYPE Lookup(HSTRING key, Value** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        boolean found = false;
        HasKey(key, &found);
        if (!found) return E_BOUNDS;
        UINT32 length{};
        const auto* raw = WindowsGetStringRawBuffer(key, &length);
        auto product = Make<Item>(std::wstring(raw, length));
        return product ? product.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<UINT32>(ids_.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HasKey(HSTRING key, boolean* value) override {
        if (!value) return E_POINTER;
        UINT32 length{};
        const auto* raw = WindowsGetStringRawBuffer(key, &length);
        const std::wstring id(raw, length);
        *value = std::find(ids_.begin(), ids_.end(), id) != ids_.end();
        Log("TRACE", "StoreMap HasKey id=%ls found=%u", id.c_str(), static_cast<unsigned>(*value));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Split(Map** first, Map** second) override {
        if (first) *first = nullptr;
        if (second) *second = nullptr;
        if (!first || !second) return E_POINTER;
        if (ids_.size() < 2) return S_OK;
        const auto middle = ids_.begin() + ids_.size() / 2;
        auto left = Make<StoreMap>(std::vector<std::wstring>(ids_.begin(), middle));
        auto right = Make<StoreMap>(std::vector<std::wstring>(middle, ids_.end()));
        if (!left || !right) return E_OUTOFMEMORY;
        left.CopyTo(first);
        return right.CopyTo(second);
    }
    HRESULT STDMETHODCALLTYPE First(Iterator** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto iterator = Make<StoreIterator<Iterator, Pair, Value, Item>>(ids_);
        return iterator ? iterator.CopyTo(value) : E_OUTOFMEMORY;
    }
};
}
}
