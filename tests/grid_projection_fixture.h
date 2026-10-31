// SDK callers inspect source-loaded visual children without projection helpers.
#include <thread>
namespace grid_fixture {
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
using namespace ABI::Windows::Foundation::Collections;
using Item = ABI::Windows::UI::Xaml::UIElement*;
using Vector = IVector<Item>;
using View = IVectorView<Item>;
using Iterable = IIterable<Item>;
using Iterator = IIterator<Item>;
ComPtr<Vector> retained;
ComPtr<View> retainedView;
ComPtr<Iterator> retainedIterator;
bool Name(IUIElement* child, const wchar_t* expected) {
    ComPtr<IFrameworkElement> element;
    Wrappers::HString name;
    return child && SUCCEEDED(child->QueryInterface(IID_PPV_ARGS(&element))) &&
           SUCCEEDED(element->get_Name(name.GetAddressOf())) && !wcscmp(name.GetRawBuffer(nullptr), expected);
}
HRESULT Check(int stage) {
    ComPtr<IUIElement> root, content;
    ComPtr<IUserControl> page;
    ComPtr<IPanel> panel;
    ComPtr<Vector> children;
    auto hr = WindowContent(&root);
    if (FAILED(hr) || FAILED(root.As(&page)) || FAILED(page->get_Content(&content)) || !content ||
        FAILED(content.As(&panel)) || FAILED(panel->get_Children(&children)) || !children)
        return E_FAIL;
    UINT32 size = 99;
    if (FAILED(children->get_Size(&size)) || size != (stage ? 0 : 2)) return E_FAIL;
    if (stage) {
        ComPtr<IUIElement> first;
        if (!retained || FAILED(retained->get_Size(&size)) || size != 2 || FAILED(retained->GetAt(0, &first)) ||
            !Name(first.Get(), L"first"))
            return E_FAIL;
        ComPtr<IUIElement> snapshotChild;
        if (!retainedView || FAILED(retainedView->get_Size(&size)) || size != 2 || !retainedIterator ||
            FAILED(retainedIterator->get_Current(&snapshotChild)) || !Name(snapshotChild.Get(), L"first"))
            return E_FAIL;
        retainedView.Reset();
        retainedIterator.Reset();
        retained.Reset();
        return S_OK;
    }
    retained = children;
    ComPtr<Vector> same;
    if (FAILED(panel->get_Children(&same)) || same.Get() != children.Get()) return E_FAIL;
    ComPtr<IUIElement> first, second;
    if (FAILED(children->GetAt(0, &first)) || FAILED(children->GetAt(1, &second)) || !Name(first.Get(), L"first") ||
        !Name(second.Get(), L"second"))
        return E_FAIL;
    ComPtr<IPanel> nested;
    ComPtr<Vector> inner;
    ComPtr<IUIElement> leaf;
    if (FAILED(first.As(&nested)) || FAILED(nested->get_Children(&inner)) || FAILED(inner->get_Size(&size)) ||
        size != 1 || FAILED(inner->GetAt(0, &leaf)) || !Name(leaf.Get(), L"leaf"))
        return E_FAIL;
    ComPtr<View> view;
    ComPtr<Iterable> iterable;
    ComPtr<Iterator> iterator;
    if (FAILED(children->GetView(&view)) || FAILED(children.As(&iterable)) || FAILED(iterable->First(&iterator)))
        return E_FAIL;
    retainedView = view;
    if (FAILED(iterable->First(&retainedIterator))) return E_FAIL;
    UINT32 index = 99;
    boolean found = false;
    if (FAILED(view->IndexOf(second.Get(), &index, &found)) || !found || index != 1) return E_FAIL;
    IUIElement* items[3]{};
    UINT32 actual = 99;
    if (FAILED(iterator->GetMany(3, items, &actual)) || actual != 2) return E_FAIL;
    const bool names = Name(items[0], L"first") && Name(items[1], L"second");
    for (auto* item : items)
        if (item) item->Release();
    if (!names || FAILED(iterator->get_HasCurrent(&found)) || found || iterator->get_Current(&leaf) != E_BOUNDS ||
        FAILED(view->GetMany(2, 0, nullptr, &actual)) || actual || view->GetMany(3, 0, nullptr, &actual) != E_BOUNDS)
        return E_FAIL;
    if (children->GetAt(2, &leaf) != E_BOUNDS || children->Clear() != E_NOTIMPL ||
        children->Append(first.Get()) != E_NOTIMPL)
        return E_FAIL;
    ComPtr<IUIElementCollection> collection;
    if (FAILED(children.As(&collection)) || collection->Move(0, 1) != E_NOTIMPL) return E_FAIL;
    HRESULT wrong = S_OK, wrongView = S_OK, wrongIterator = S_OK;
    std::thread worker([&] {
        UINT32 count;
        boolean has;
        wrong = children->get_Size(&count);
        wrongView = view->get_Size(&count);
        wrongIterator = iterator->get_HasCurrent(&has);
    });
    worker.join();
    return wrong == RPC_E_WRONG_THREAD && wrongView == RPC_E_WRONG_THREAD && wrongIterator == RPC_E_WRONG_THREAD
               ? S_OK
               : E_FAIL;
}
}
extern "C" HRESULT __cdecl ValidateGridChildren(INT32 stage) { return grid_fixture::Check(stage); }
