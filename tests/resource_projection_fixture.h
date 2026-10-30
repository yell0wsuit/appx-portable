// Included only by the SDK-typed Page/component test DLL.
#include <thread>
using ResourceMap = ABI::Windows::Foundation::Collections::IMap<IInspectable*, IInspectable*>;
using ResourceView = ABI::Windows::Foundation::Collections::IMapView<IInspectable*, IInspectable*>;
using ResourcePair = ABI::Windows::Foundation::Collections::IKeyValuePair<IInspectable*, IInspectable*>;
static HRESULT BoxResourceKey(const wchar_t* text, IInspectable** value) {
    ComPtr<ABI::Windows::Foundation::IPropertyValueStatics> factory;
    auto hr = RoGetActivationFactory(Wrappers::HStringReference(L"Windows.Foundation.PropertyValue").Get(),
                                     IID_PPV_ARGS(&factory));
    return FAILED(hr) ? hr : factory->CreateString(Wrappers::HStringReference(text).Get(), value);
}
static HRESULT CurrentResources(ABI::Windows::UI::Xaml::IResourceDictionary** value) {
    ComPtr<ABI::Windows::UI::Xaml::IUIElement> content;
    auto hr = WindowContent(&content);
    ComPtr<ABI::Windows::UI::Xaml::IFrameworkElement> root;
    if (SUCCEEDED(hr)) hr = content.As(&root);
    return FAILED(hr) ? hr : root->get_Resources(value);
}
static HRESULT ResourceWidth(ResourceMap* map, const wchar_t* name, double expected, IInspectable** result = nullptr) {
    ComPtr<IInspectable> key, value;
    auto hr = BoxResourceKey(name, &key);
    if (SUCCEEDED(hr)) hr = map->Lookup(key.Get(), &value);
    ComPtr<ABI::Windows::UI::Xaml::IFrameworkElement> element;
    if (SUCCEEDED(hr)) hr = value.As(&element);
    double width = 0;
    if (SUCCEEDED(hr)) hr = element->get_Width(&width);
    if (FAILED(hr) || width != expected) return E_FAIL;
    return result ? value.CopyTo(result) : S_OK;
}
extern "C" HRESULT __cdecl ExerciseResourceMap() {
    using namespace ABI::Windows::Foundation::Collections;
    ComPtr<ABI::Windows::UI::Xaml::IResourceDictionary> dictionary;
    auto hr = CurrentResources(&dictionary);
    ComPtr<ResourceMap> map;
    if (SUCCEEDED(hr)) hr = dictionary.As(&map);
    if (FAILED(hr)) return hr;
    UINT32 size = 1;
    boolean replaced = true, present = true;
    if (FAILED(map->get_Size(&size)) || size != 0) return E_FAIL;
    ComPtr<IInspectable> first, second, same;
    if (FAILED(BoxResourceKey(L"é key", &first)) || FAILED(BoxResourceKey(L"second", &second)) ||
        FAILED(BoxResourceKey(L"é key", &same)))
        return E_FAIL;
    auto marker = Make<NamelessTarget>(E_NOTIMPL);
    if (!marker || FAILED(map->Insert(first.Get(), marker.Get(), &replaced)) || replaced) return E_FAIL;
    if (FAILED(map->HasKey(same.Get(), &present)) || !present) return E_FAIL;
    ComPtr<IInspectable> result;
    if (FAILED(map->Lookup(same.Get(), &result)) || !result) return E_FAIL;
    ComPtr<IUnknown> original, found;
    if (FAILED(marker.As(&original)) || FAILED(result.As(&found)) || original != found) return E_FAIL;
    ComPtr<ResourceView> view;
    if (FAILED(map->GetView(&view))) return E_FAIL;
    marker.Reset();
    original.Reset();
    found.Reset();
    HRESULT wrongMap = S_OK, wrongView = S_OK;
    std::thread worker([&] {
        ComPtr<IInspectable> value;
        UINT32 count = 0;
        wrongMap = map->Lookup(first.Get(), &value);
        wrongView = view->get_Size(&count);
    });
    worker.join();
    if (wrongMap != RPC_E_WRONG_THREAD || wrongView != RPC_E_WRONG_THREAD) return E_FAIL;
    if (FAILED(map->Insert(same.Get(), nullptr, &replaced)) || !replaced) return E_FAIL;
    result.Reset();
    if (FAILED(map->Lookup(first.Get(), &result)) || result) return E_FAIL;
    if (FAILED(view->Lookup(first.Get(), &result)) || !result || FAILED(CheckNamelessTarget(result.Get())))
        return E_FAIL;
    result.Reset();
    if (FAILED(map->Insert(second.Get(), nullptr, &replaced)) || replaced) return E_FAIL;
    ComPtr<ResourceView> pairView, left, right;
    if (FAILED(map->GetView(&pairView)) || FAILED(pairView->Split(&left, &right)) || !left || !right) return E_FAIL;
    UINT32 a = 0, b = 0;
    if (FAILED(left->get_Size(&a)) || FAILED(right->get_Size(&b)) || a + b != 2) return E_FAIL;
    ComPtr<IIterable<ResourcePair*>> iterable;
    ComPtr<IIterator<ResourcePair*>> iterator;
    if (FAILED(map.As(&iterable)) || FAILED(iterable->First(&iterator))) return E_FAIL;
    boolean current = false;
    if (FAILED(iterator->get_HasCurrent(&current)) || !current) return E_FAIL;
    ComPtr<ResourcePair> item;
    if (FAILED(iterator->get_Current(&item))) return E_FAIL;
    ComPtr<IInspectable> key;
    if (FAILED(item->get_Key(&key)) || !key || FAILED(item->get_Value(&result)) || result) return E_FAIL;
    ResourcePair* batch[3]{};
    UINT32 actual = 0;
    if (FAILED(iterator->GetMany(3, batch, &actual)) || actual != 2) return E_FAIL;
    for (UINT32 i = 0; i < actual; ++i) batch[i]->Release();
    if (FAILED(iterator->get_HasCurrent(&current)) || current || iterator->get_Current(&item) != E_BOUNDS)
        return E_FAIL;
    if (FAILED(iterator->MoveNext(&current)) || current) return E_FAIL;
    if (map->Lookup(nullptr, &result) != E_INVALIDARG) return E_FAIL;
    if (FAILED(map->Remove(first.Get())) || map->Remove(first.Get()) != E_BOUNDS) return E_FAIL;
    if (FAILED(map->Clear()) || FAILED(map->get_Size(&size)) || size != 0) return E_FAIL;
    if (FAILED(view->Lookup(first.Get(), &result)) || !result || FAILED(CheckNamelessTarget(result.Get())))
        return E_FAIL;
    ComPtr<IInspectable> ignored;
    if (map->Lookup(result.Get(), &ignored) != E_NOTIMPL) return E_FAIL;
    if (FAILED(map->HasKey(first.Get(), &present)) || present) return E_FAIL;
    if (FAILED(view->HasKey(first.Get(), &present)) || !present) return E_FAIL;
    if (FAILED(view->Split(&left, &right)) || left || right) return E_FAIL;
    // The snapshot alone retained the marker after the original/map references left.
    ComPtr<ABI::Windows::Foundation::IPropertyValueStatics> values;
    if (FAILED(RoGetActivationFactory(Wrappers::HStringReference(L"Windows.Foundation.PropertyValue").Get(),
                                      IID_PPV_ARGS(&values))))
        return E_FAIL;
    ComPtr<IInspectable> number;
    if (FAILED(values->CreateInt32(3, &number)) || map->Insert(number.Get(), nullptr, &replaced) != E_NOTIMPL)
        return E_FAIL;
    ComPtr<ABI::Windows::Foundation::IUriRuntimeClass> uri;
    if (FAILED(dictionary->get_Source(&uri)) || uri) return E_FAIL;
    ABI::Windows::Foundation::Collections::IVector<ABI::Windows::UI::Xaml::ResourceDictionary*>* merged = nullptr;
    if (dictionary->get_MergedDictionaries(&merged) != E_NOTIMPL || merged) return E_FAIL;
    return S_OK;
}
static ComPtr<ABI::Windows::UI::Xaml::IResourceDictionary> heldResources;
extern "C" HRESULT __cdecl ValidateLoadedResources(INT32 stage) {
    ComPtr<ABI::Windows::UI::Xaml::IResourceDictionary> dictionary;
    auto hr = CurrentResources(&dictionary);
    ComPtr<ResourceMap> map;
    if (SUCCEEDED(hr)) hr = dictionary.As(&map);
    if (FAILED(hr)) return hr;
    UINT32 size = 0;
    if (FAILED(map->get_Size(&size)) || size != (stage == 1 ? 3U : stage == 2 ? 1U : 0U)) return E_FAIL;
    if (stage == 1) {
        if (FAILED(ResourceWidth(map.Get(), L"first", 11)) || FAILED(ResourceWidth(map.Get(), L"named", 22)) ||
            FAILED(ResourceWidth(map.Get(), L"distinct", 33)))
            return E_FAIL;
        ComPtr<IInspectable> key, value;
        if (FAILED(BoxResourceKey(L"visible", &key)) || map->Lookup(key.Get(), &value) != E_BOUNDS) return E_FAIL;
        ComPtr<IInspectable> named;
        if (FAILED(ResourceWidth(map.Get(), L"named", 22, &named))) return E_FAIL;
        ComPtr<ABI::Windows::UI::Xaml::IFrameworkElement> element;
        ComPtr<ABI::Windows::UI::Xaml::IResourceDictionary> nested;
        ComPtr<ResourceMap> local;
        if (FAILED(named.As(&element)) || FAILED(element->get_Resources(&nested)) || FAILED(nested.As(&local)) ||
            FAILED(ResourceWidth(local.Get(), L"first", 55)))
            return E_FAIL;
        heldResources = dictionary;
    } else if (stage == 2) {
        if (FAILED(ResourceWidth(map.Get(), L"explicit", 44))) return E_FAIL;
        ComPtr<ResourceMap> old;
        if (!heldResources || FAILED(heldResources.As(&old)) || FAILED(ResourceWidth(old.Get(), L"named", 22)))
            return E_FAIL;
        heldResources.Reset();
    }
    return S_OK;
}
