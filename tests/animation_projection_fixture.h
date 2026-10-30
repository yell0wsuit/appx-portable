// SDK-typed checks for timeline data; clock execution is a separate contract.
#undef GetCurrentTime
#include <windows.ui.xaml.media.animation.h>
#include <limits>
#include <thread>
namespace animation_fixture {
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Media::Animation;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using Item = ABI::Windows::UI::Xaml::Media::Animation::Timeline*;
using Vector = IVector<Item>;
using View = IVectorView<Item>;
using Iterator = IIterator<Item>;
HRESULT Activate(const wchar_t* name, IInspectable** value, IActivationFactory** result = nullptr) {
    using GetFactory = HRESULT(WINAPI*)(HSTRING, IActivationFactory**);
    auto get = reinterpret_cast<GetFactory>(GetProcAddress(GetModuleHandleW(L"appxportable.dll"), "ShimGetFactory"));
    if (!get) return E_UNEXPECTED;
    ComPtr<IActivationFactory> factory;
    auto hr = get(Wrappers::HStringReference(name).Get(), &factory);
    if (SUCCEEDED(hr)) hr = factory->ActivateInstance(value);
    if (SUCCEEDED(hr) && result) hr = factory.CopyTo(result);
    return hr;
}
class TimeValue final : public RuntimeClass<RuntimeClassFlags<WinRt>, IReference<TimeSpan>> {
    InspectableClass(L"Test.TimeValue", BaseTrust);
    TimeSpan value_;

public:
    explicit TimeValue(TimeSpan value) : value_(value) {}
    HRESULT STDMETHODCALLTYPE get_Value(TimeSpan* value) override {
        if (!value) return E_POINTER;
        *value = value_;
        return S_OK;
    }
};
class Completed final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IEventHandler<IInspectable*>> {
    bool* destroyed_;

public:
    explicit Completed(bool* value) : destroyed_(value) {}
    ~Completed() { *destroyed_ = true; }
    HRESULT STDMETHODCALLTYPE Invoke(IInspectable*, IInspectable*) override { return E_UNEXPECTED; }
};
HRESULT Exercise() {
    ComPtr<IUIElement> root;
    auto hr = WindowContent(&root);
    ComPtr<IFrameworkElement> element;
    if (FAILED(hr) || FAILED(root.As(&element))) return E_FAIL;
    ComPtr<IInspectable> named;
    if (FAILED(element->FindName(Wrappers::HStringReference(L"animation").Get(), &named))) return E_FAIL;
    ComPtr<IStoryboard> source;
    ComPtr<Vector> sourceChildren;
    if (!named || FAILED(named.As(&source)) || FAILED(source->get_Children(&sourceChildren))) return E_FAIL;
    UINT32 size = 0;
    if (FAILED(sourceChildren->get_Size(&size)) || size != 2) return E_FAIL;
    ComPtr<ITimeline> sourceFade;
    Duration duration{};
    if (FAILED(sourceChildren->GetAt(0, &sourceFade)) || FAILED(sourceFade->get_Duration(&duration)) ||
        duration.Type != DurationType_TimeSpan || duration.TimeSpan.Duration != 45000001)
        return E_FAIL;
    ComPtr<IInspectable> object;
    ComPtr<IActivationFactory> factory;
    if (FAILED(Activate(L"Windows.UI.Xaml.Media.Animation.Storyboard", &object, &factory))) return E_FAIL;
    ComPtr<IStoryboard> parent;
    ComPtr<IStoryboardStatics> statics;
    ComPtr<ITimeline> parentTimeline;
    ComPtr<Vector> children;
    if (FAILED(object.As(&parent)) || FAILED(object.As(&parentTimeline)) || FAILED(factory.As(&statics)) ||
        FAILED(parent->get_Children(&children)))
        return E_FAIL;
    Wrappers::HString text;
    if (FAILED(statics->GetTargetName(sourceFade.Get(), text.GetAddressOf())) ||
        wcscmp(text.GetRawBuffer(nullptr), L"leaf"))
        return E_FAIL;
    ComPtr<IFadeInThemeAnimation> own;
    if (FAILED(sourceFade.As(&own))) return E_FAIL;
    text.Release();
    if (FAILED(own->get_TargetName(text.GetAddressOf())) || text.Length()) return E_FAIL;
    ComPtr<IInspectable> a, b, nestedObject;
    if (FAILED(Activate(L"Windows.UI.Xaml.Media.Animation.FadeInThemeAnimation", &a)) ||
        FAILED(Activate(L"Windows.UI.Xaml.Media.Animation.FadeOutThemeAnimation", &b)) ||
        FAILED(Activate(L"Windows.UI.Xaml.Media.Animation.Storyboard", &nestedObject)))
        return E_FAIL;
    ComPtr<ITimeline> first, second, nestedTimeline;
    ComPtr<IStoryboard> nested;
    if (FAILED(a.As(&first)) || FAILED(b.As(&second)) || FAILED(nestedObject.As(&nestedTimeline)) ||
        FAILED(nestedObject.As(&nested)))
        return E_FAIL;
    if (FAILED(first->get_Duration(&duration)) || duration.Type != DurationType_Automatic) return E_FAIL;
    if (FAILED(first->put_Duration({{1234567}, DurationType_TimeSpan})) ||
        first->put_Duration({{-1}, DurationType_TimeSpan}) != E_INVALIDARG)
        return E_FAIL;
    if (FAILED(first->get_Duration(&duration)) || duration.TimeSpan.Duration != 1234567) return E_FAIL;
    if (FAILED(first->put_Duration({{0}, DurationType_Forever})) || FAILED(first->get_Duration(&duration)) ||
        duration.Type != DurationType_Forever)
        return E_FAIL;
    ComPtr<IReference<TimeSpan>> begin;
    TimeSpan beginValue{};
    if (FAILED(first->get_BeginTime(&begin)) || !begin || FAILED(begin->get_Value(&beginValue)) || beginValue.Duration)
        return E_FAIL;
    auto negative = Make<TimeValue>(TimeSpan{-1});
    if (first->put_BeginTime(negative.Get()) != E_INVALIDARG || FAILED(first->put_BeginTime(nullptr))) return E_FAIL;
    begin.Reset();
    if (FAILED(first->get_BeginTime(&begin)) || begin) return E_FAIL;
    if (FAILED(first->put_AutoReverse(true))) return E_FAIL;
    boolean reverse = false;
    if (FAILED(first->get_AutoReverse(&reverse)) || !reverse || first->put_SpeedRatio(0) != E_INVALIDARG ||
        first->put_SpeedRatio(std::numeric_limits<double>::quiet_NaN()) != E_INVALIDARG ||
        FAILED(first->put_SpeedRatio(2)))
        return E_FAIL;
    double speed = 0;
    if (FAILED(first->get_SpeedRatio(&speed)) || speed != 2) return E_FAIL;
    if (FAILED(first->put_FillBehavior(FillBehavior_Stop)) ||
        FAILED(first->put_RepeatBehavior({2.5, {0}, RepeatBehaviorType_Count})))
        return E_FAIL;
    RepeatBehavior repeat{};
    if (FAILED(first->get_RepeatBehavior(&repeat)) || repeat.Count != 2.5) return E_FAIL;
    if (FAILED(statics->SetTargetName(first.Get(), Wrappers::HStringReference(L"té target").Get())) ||
        FAILED(statics->SetTargetProperty(first.Get(), Wrappers::HStringReference(L"Opacity").Get())))
        return E_FAIL;
    text.Release();
    if (FAILED(statics->GetTargetProperty(first.Get(), text.GetAddressOf())) ||
        wcscmp(text.GetRawBuffer(nullptr), L"Opacity"))
        return E_FAIL;
    bool destroyed = false;
    auto handler = Make<Completed>(&destroyed);
    EventRegistrationToken token{};
    if (FAILED(first->add_Completed(handler.Get(), &token))) return E_FAIL;
    handler.Reset();
    if (destroyed || FAILED(first->remove_Completed(token)) || !destroyed) return E_FAIL;
    if (FAILED(children->get_Size(&size)) || size || children->RemoveAtEnd() != E_BOUNDS ||
        children->Append(nullptr) != E_INVALIDARG || children->Append(parentTimeline.Get()) != E_INVALIDARG)
        return E_FAIL;
    if (FAILED(children->Append(first.Get())) || FAILED(children->InsertAt(0, second.Get())) ||
        FAILED(children->SetAt(0, first.Get())))
        return E_FAIL;
    UINT32 index = 99;
    boolean found = false;
    if (FAILED(children->IndexOf(first.Get(), &index, &found)) || !found || index != 0) return E_FAIL;
    ComPtr<View> view;
    if (FAILED(children->GetView(&view))) return E_FAIL;
    if (FAILED(children->Clear()) || FAILED(children->Append(nestedTimeline.Get()))) return E_FAIL;
    ComPtr<Vector> nestedChildren;
    if (FAILED(nested->get_Children(&nestedChildren)) || nestedChildren->Append(parentTimeline.Get()) != E_INVALIDARG)
        return E_FAIL;
    ITimeline* invalid[] = {first.Get(), nullptr};
    if (sizeof(void*) == 4) {
        HRESULT oversized = S_OK;
        try {
            oversized = children->ReplaceAll(std::numeric_limits<UINT32>::max(), invalid);
        } catch (...) {
            return E_UNEXPECTED;
        }
        if (oversized != E_OUTOFMEMORY || FAILED(children->get_Size(&size)) || size != 1) return E_FAIL;
    }
    if (children->ReplaceAll(2, invalid) != E_INVALIDARG || FAILED(children->get_Size(&size)) || size != 1)
        return E_FAIL;
    ITimeline* replacement[] = {first.Get(), second.Get()};
    if (FAILED(children->ReplaceAll(2, replacement))) return E_FAIL;
    HRESULT wrong = S_OK;
    std::thread worker([&] { wrong = children->Append(first.Get()); });
    worker.join();
    if (wrong != RPC_E_WRONG_THREAD) return E_FAIL;
    if (parent->Begin() != E_NOTIMPL || parent->Pause() != E_NOTIMPL || parent->Stop() != E_NOTIMPL) return E_FAIL;
    parent.Reset();
    parentTimeline.Reset();
    object.Reset();
    factory.Reset();
    a.Reset();
    b.Reset();
    first.Reset();
    second.Reset();
    replacement[0] = replacement[1] = nullptr;
    if (FAILED(children->Clear()) || FAILED(view->get_Size(&size)) || size != 2) return E_FAIL;
    ComPtr<IIterable<Item>> iterable;
    ComPtr<Iterator> iterator;
    if (FAILED(view.As(&iterable)) || FAILED(iterable->First(&iterator))) return E_FAIL;
    ITimeline* batch[3]{};
    UINT32 actual = 0;
    if (FAILED(iterator->GetMany(3, batch, &actual)) || actual != 2) return E_FAIL;
    for (UINT32 i = 0; i < actual; ++i) batch[i]->Release();
    ComPtr<ITimeline> held;
    if (iterator->get_Current(&held) != E_BOUNDS || FAILED(view->GetAt(0, &held)) ||
        FAILED(held->get_Duration(&duration)) || duration.Type != DurationType_Forever)
        return E_FAIL;
    return S_OK;
}
}
extern "C" HRESULT __cdecl ExerciseAnimationData() { return animation_fixture::Exercise(); }

extern "C" HRESULT __cdecl ValidateSourceRootFields() {
    ComPtr<ABI::Windows::UI::Xaml::IUIElement> content;
    auto hr = WindowContent(&content);
    ComPtr<ABI::Windows::UI::Xaml::IFrameworkElement> root;
    if (FAILED(hr) || FAILED(content.As(&root))) return E_FAIL;
    double width = 0, height = 0;
    Wrappers::HString name;
    if (FAILED(root->get_Width(&width)) || FAILED(root->get_Height(&height)) || width != 321 || height != 123 ||
        FAILED(root->get_Name(name.GetAddressOf())) || wcscmp(name.GetRawBuffer(nullptr), L"root"))
        return E_FAIL;
    return S_OK;
}
