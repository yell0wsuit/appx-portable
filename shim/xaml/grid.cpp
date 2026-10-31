#include "grid.h"
#include "log.h"
#include <algorithm>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
using namespace ABI::Windows::Foundation::Collections;
namespace appx {
namespace {
using Item = ABI::Windows::UI::Xaml::UIElement*;
using View = IVectorView<Item>;
using Iterable = IIterable<Item>;
using Iterator = IIterator<Item>;
using Snapshot = std::shared_ptr<const VisualChildren>;
template <class F> HRESULT Guard(F&& action) {
    try {
        return action();
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
}
MIDL_INTERFACE("58388d94-cd23-4d95-94bb-4d52b10d6334") ISourceChildren : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Replace(const Snapshot& children) = 0;
};
HRESULT Copy(const VisualChildren& items, UINT32 start, UINT32 capacity, IUIElement** values, UINT32* actual) {
    if (!actual) return E_POINTER;
    *actual = 0;
    if (capacity && !values) return E_POINTER;
    if (start > items.size()) return E_BOUNDS;
    auto count = static_cast<UINT32>(std::min<size_t>(capacity, items.size() - start));
    for (UINT32 i = 0; i < count; ++i) items[start + i].CopyTo(&values[i]);
    *actual = count;
    return S_OK;
}
class ChildrenIterator final : public RuntimeClass<RuntimeClassFlags<WinRt>, Iterator> {
    InspectableClass(L"AppxPortable.Xaml.ChildrenIterator", BaseTrust);
    Snapshot items_;
    UINT32 index_ = 0;
    const DWORD thread_ = GetCurrentThreadId();
    HRESULT CheckThread() const { return thread_ == GetCurrentThreadId() ? S_OK : RPC_E_WRONG_THREAD; }

public:
    explicit ChildrenIterator(Snapshot items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE get_Current(IUIElement** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return index_ < items_->size() ? (*items_)[index_].CopyTo(value) : E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = index_ < items_->size();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (index_ < items_->size()) ++index_;
        *value = index_ < items_->size();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 capacity, IUIElement** values, UINT32* actual) override {
        if (!actual) return E_POINTER;
        *actual = 0;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) hr = Copy(*items_, index_, capacity, values, actual);
        if (SUCCEEDED(hr)) index_ += *actual;
        return hr;
    }
};
template <class Primary, class... Interfaces>
class ChildrenReader : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Primary, Iterable, Interfaces...> {
protected:
    Snapshot items_;
    const DWORD thread_ = GetCurrentThreadId();
    explicit ChildrenReader(Snapshot items) : items_(std::move(items)) {}
    HRESULT CheckThread() const { return thread_ == GetCurrentThreadId() ? S_OK : RPC_E_WRONG_THREAD; }

public:
    HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, IUIElement** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return index < items_->size() ? (*items_)[index].CopyTo(value) : E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = static_cast<UINT32>(items_->size());
        return hr;
    }
    HRESULT STDMETHODCALLTYPE IndexOf(IUIElement* value, UINT32* index, boolean* found) override {
        if (!index || !found) return E_POINTER;
        *index = 0;
        *found = false;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        ComPtr<IUnknown> identity;
        if (value) {
            hr = value->QueryInterface(IID_PPV_ARGS(&identity));
            if (FAILED(hr)) return hr;
        }
        for (UINT32 i = 0; i < items_->size(); ++i) {
            ComPtr<IUnknown> candidate;
            hr = (*items_)[i].As(&candidate);
            if (FAILED(hr)) return hr;
            if (identity == candidate) {
                *index = i;
                *found = true;
                break;
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 start, UINT32 capacity, IUIElement** values, UINT32* actual) override {
        if (!actual) return E_POINTER;
        *actual = 0;
        auto hr = CheckThread();
        return FAILED(hr) ? hr : Copy(*items_, start, capacity, values, actual);
    }
    HRESULT STDMETHODCALLTYPE First(Iterator** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        auto result = Make<ChildrenIterator>(items_);
        return result ? result.CopyTo(value) : E_OUTOFMEMORY;
    }
};
class ChildrenView final : public ChildrenReader<View> {
    InspectableClass(L"AppxPortable.Xaml.ChildrenView", BaseTrust);

public:
    explicit ChildrenView(Snapshot items) : ChildrenReader(std::move(items)) {}
};
class ChildrenCollection final : public ChildrenReader<GridChildren, IUIElementCollection, ISourceChildren> {
    InspectableClass(L"Windows.UI.Xaml.Controls.UIElementCollection", BaseTrust);
    HRESULT Unsupported(const wchar_t* member) {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        Log("UNIMPL", "Grid.Children.%ls hr=%08lx", member, E_NOTIMPL);
        return E_NOTIMPL;
    }

public:
    ChildrenCollection() : ChildrenReader(std::make_shared<VisualChildren>()) {}
    HRESULT STDMETHODCALLTYPE Replace(const Snapshot& items) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (!items) return E_INVALIDARG;
        items_ = items;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetView(View** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        auto result = Make<ChildrenView>(items_);
        return result ? result.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE SetAt(UINT32, IUIElement*) override { return Unsupported(L"SetAt"); }
    HRESULT STDMETHODCALLTYPE InsertAt(UINT32, IUIElement*) override { return Unsupported(L"InsertAt"); }
    HRESULT STDMETHODCALLTYPE RemoveAt(UINT32) override { return Unsupported(L"RemoveAt"); }
    HRESULT STDMETHODCALLTYPE Append(IUIElement*) override { return Unsupported(L"Append"); }
    HRESULT STDMETHODCALLTYPE RemoveAtEnd() override { return Unsupported(L"RemoveAtEnd"); }
    HRESULT STDMETHODCALLTYPE Clear() override { return Unsupported(L"Clear"); }
    HRESULT STDMETHODCALLTYPE ReplaceAll(UINT32, IUIElement**) override { return Unsupported(L"ReplaceAll"); }
    HRESULT STDMETHODCALLTYPE Move(UINT32, UINT32) override { return Unsupported(L"Move"); }
};
}
HRESULT MakeGridChildren(GridChildren** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    return Guard([&]() -> HRESULT {
        auto result = Make<ChildrenCollection>();
        return result ? result.CopyTo(value) : E_OUTOFMEMORY;
    });
}
HRESULT SetGridChildren(IInspectable* grid, const Snapshot& children) {
    if (!grid || !children) return E_INVALIDARG;
    ComPtr<IPanel> panel;
    auto hr = grid->QueryInterface(IID_PPV_ARGS(&panel));
    ComPtr<GridChildren> collection;
    if (SUCCEEDED(hr)) hr = panel->get_Children(&collection);
    ComPtr<ISourceChildren> target;
    if (SUCCEEDED(hr)) hr = collection.As(&target);
    return FAILED(hr) ? hr : target->Replace(children);
}
}
