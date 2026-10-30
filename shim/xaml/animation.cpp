#include "animation.h"
#include "element.h"
#include "com/factory.h"
#include "registry.h"
#include <wrl/event.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <vector>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Media::Animation;
namespace appx {
namespace {
using Item = ABI::Windows::UI::Xaml::Media::Animation::Timeline*;
using Vector = IVector<Item>;
using View = IVectorView<Item>;
using Iterable = IIterable<Item>;
using Iterator = IIterator<Item>;
using Handler = IEventHandler<IInspectable*>;
using TimeReference = IReference<TimeSpan>;
using Items = std::vector<ComPtr<ITimeline>>;
template <class F> HRESULT Guard(F&& action) {
    try {
        return action();
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
}
HRESULT Unsupported(const wchar_t* member) {
    Log("UNIMPL", "%ls hr=%08lx", member, E_NOTIMPL);
    return E_NOTIMPL;
}
class TimeValue final : public RuntimeClass<RuntimeClassFlags<WinRt>, TimeReference> {
    InspectableClass(L"AppxPortable.Xaml.TimeSpanReference", BaseTrust);
    TimeSpan value_;

public:
    explicit TimeValue(TimeSpan value) : value_(value) {}
    HRESULT STDMETHODCALLTYPE get_Value(TimeSpan* value) override {
        if (!value) return E_POINTER;
        *value = value_;
        return S_OK;
    }
};
struct State {
    const DWORD thread = GetCurrentThreadId();
    bool storyboard = false, reverse = false;
    Duration duration{{0}, DurationType_Automatic};
    double speed = 1;
    FillBehavior fill = FillBehavior_HoldEnd;
    RepeatBehavior repeat{1, {0}, RepeatBehaviorType_Count};
    ComPtr<TimeReference> begin;
    std::wstring targetName, themeTargetName, targetProperty;
    std::weak_ptr<SourceNameScope> scope;
    Items children;
    HRESULT CheckThread() const { return GetCurrentThreadId() == thread ? S_OK : RPC_E_WRONG_THREAD; }
};
MIDL_INTERFACE("9CA679D7-6320-4CA9-B080-C9991102C5DA") IAnimationData : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetData(std::shared_ptr<State> & value) = 0;
};
HRESULT Data(IInspectable* value, std::shared_ptr<State>& result) {
    if (!value) return E_INVALIDARG;
    ComPtr<IAnimationData> node;
    auto hr = value->QueryInterface(IID_PPV_ARGS(&node));
    return FAILED(hr) ? hr : node->GetData(result);
}
bool Contains(const std::shared_ptr<State>& node, State* owner, std::set<State*>& seen) {
    if (node.get() == owner) return true;
    if (!seen.insert(node.get()).second) return false;
    for (const auto& child : node->children) {
        std::shared_ptr<State> next;
        if (SUCCEEDED(Data(child.Get(), next)) && Contains(next, owner, seen)) return true;
    }
    return false;
}
HRESULT Child(const std::shared_ptr<State>& owner, ITimeline* child) {
    std::shared_ptr<State> value;
    auto hr = Data(child, value);
    if (hr == E_NOINTERFACE) return Unsupported(L"Storyboard.Children timeline type");
    if (FAILED(hr)) return hr;
    std::set<State*> seen;
    return Contains(value, owner.get(), seen) ? E_INVALIDARG : S_OK;
}
HRESULT CopyItems(const Items& items, UINT32 start, UINT32 capacity, ITimeline** values, UINT32* actual) {
    if (!actual) return E_POINTER;
    *actual = 0;
    if (capacity && !values) return E_POINTER;
    if (start > items.size()) return E_BOUNDS;
    auto count = static_cast<UINT32>(std::min<size_t>(capacity, items.size() - start));
    for (UINT32 i = 0; i < count; ++i) items[start + i].CopyTo(&values[i]);
    *actual = count;
    return S_OK;
}
HRESULT Index(const Items& items, ITimeline* value, UINT32* index, boolean* found) {
    if (!index || !found) return E_POINTER;
    *index = 0;
    *found = false;
    if (!value) return S_OK;
    ComPtr<IUnknown> identity;
    auto hr = value->QueryInterface(IID_PPV_ARGS(&identity));
    if (FAILED(hr)) return hr;
    for (size_t i = 0; i < items.size(); ++i) {
        ComPtr<IUnknown> other;
        hr = items[i].As(&other);
        if (FAILED(hr)) return hr;
        if (other == identity) {
            *index = static_cast<UINT32>(i);
            *found = true;
            break;
        }
    }
    return S_OK;
}
class TimelineIterator final : public RuntimeClass<RuntimeClassFlags<WinRt>, Iterator> {
    InspectableClass(L"AppxPortable.Xaml.TimelineIterator", BaseTrust);
    Items items_;
    UINT32 position_ = 0;
    const DWORD thread_ = GetCurrentThreadId();
    HRESULT CheckThread() const { return GetCurrentThreadId() == thread_ ? S_OK : RPC_E_WRONG_THREAD; }

public:
    explicit TimelineIterator(Items value) : items_(std::move(value)) {}
    HRESULT STDMETHODCALLTYPE get_Current(ITimeline** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return position_ >= items_.size() ? E_BOUNDS : items_[position_].CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = position_ < items_.size();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* value) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (!value) return E_POINTER;
        if (position_ < items_.size()) ++position_;
        return get_HasCurrent(value);
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 capacity, ITimeline** values, UINT32* actual) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        hr = CopyItems(items_, position_, capacity, values, actual);
        if (SUCCEEDED(hr)) position_ += *actual;
        return hr;
    }
};
class TimelineView final : public RuntimeClass<RuntimeClassFlags<WinRt>, View, Iterable> {
    InspectableClass(L"AppxPortable.Xaml.TimelineView", BaseTrust);
    Items items_;
    const DWORD thread_ = GetCurrentThreadId();
    HRESULT CheckThread() const { return GetCurrentThreadId() == thread_ ? S_OK : RPC_E_WRONG_THREAD; }

public:
    explicit TimelineView(Items value) : items_(std::move(value)) {}
    HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, ITimeline** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return index >= items_.size() ? E_BOUNDS : items_[index].CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = static_cast<UINT32>(items_.size());
        return hr;
    }
    HRESULT STDMETHODCALLTYPE IndexOf(ITimeline* value, UINT32* index, boolean* found) override {
        auto hr = CheckThread();
        return FAILED(hr) ? hr : Index(items_, value, index, found);
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 start, UINT32 capacity, ITimeline** values, UINT32* actual) override {
        auto hr = CheckThread();
        return FAILED(hr) ? hr : CopyItems(items_, start, capacity, values, actual);
    }
    HRESULT STDMETHODCALLTYPE First(Iterator** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return Guard([&]() -> HRESULT {
            auto result = Make<TimelineIterator>(items_);
            return result ? result.CopyTo(value) : E_OUTOFMEMORY;
        });
    }
};
class TimelineVector final : public RuntimeClass<RuntimeClassFlags<WinRt>, Vector, Iterable> {
    InspectableClass(L"Windows.UI.Xaml.Media.Animation.TimelineCollection", BaseTrust);
    std::shared_ptr<State> state_;

public:
    explicit TimelineVector(std::shared_ptr<State> value) : state_(std::move(value)) {}
    HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, ITimeline** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = state_->CheckThread();
        if (FAILED(hr)) return hr;
        return index >= state_->children.size() ? E_BOUNDS : state_->children[index].CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        auto hr = state_->CheckThread();
        if (SUCCEEDED(hr)) *value = static_cast<UINT32>(state_->children.size());
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetView(View** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = state_->CheckThread();
        if (FAILED(hr)) return hr;
        return Guard([&]() -> HRESULT {
            auto view = Make<TimelineView>(state_->children);
            return view ? view.CopyTo(value) : E_OUTOFMEMORY;
        });
    }
    HRESULT STDMETHODCALLTYPE IndexOf(ITimeline* value, UINT32* index, boolean* found) override {
        auto hr = state_->CheckThread();
        return FAILED(hr) ? hr : Index(state_->children, value, index, found);
    }
    HRESULT STDMETHODCALLTYPE SetAt(UINT32 index, ITimeline* value) override {
        auto hr = state_->CheckThread();
        if (FAILED(hr)) return hr;
        if (index >= state_->children.size()) return E_BOUNDS;
        return Guard([&]() -> HRESULT {
            auto result = Child(state_, value);
            if (SUCCEEDED(result)) state_->children[index] = value;
            return result;
        });
    }
    HRESULT STDMETHODCALLTYPE InsertAt(UINT32 index, ITimeline* value) override {
        auto hr = state_->CheckThread();
        if (FAILED(hr)) return hr;
        if (index > state_->children.size()) return E_BOUNDS;
        return Guard([&]() -> HRESULT {
            auto result = Child(state_, value);
            if (SUCCEEDED(result)) state_->children.insert(state_->children.begin() + index, ComPtr<ITimeline>(value));
            return result;
        });
    }
    HRESULT STDMETHODCALLTYPE RemoveAt(UINT32 index) override {
        auto hr = state_->CheckThread();
        if (FAILED(hr)) return hr;
        if (index >= state_->children.size()) return E_BOUNDS;
        state_->children.erase(state_->children.begin() + index);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Append(ITimeline* value) override {
        auto hr = state_->CheckThread();
        if (FAILED(hr)) return hr;
        return InsertAt(static_cast<UINT32>(state_->children.size()), value);
    }
    HRESULT STDMETHODCALLTYPE RemoveAtEnd() override {
        auto hr = state_->CheckThread();
        if (FAILED(hr)) return hr;
        return state_->children.empty() ? E_BOUNDS : RemoveAt(static_cast<UINT32>(state_->children.size() - 1));
    }
    HRESULT STDMETHODCALLTYPE Clear() override {
        auto hr = state_->CheckThread();
        if (SUCCEEDED(hr)) state_->children.clear();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 start, UINT32 capacity, ITimeline** values, UINT32* actual) override {
        auto hr = state_->CheckThread();
        return FAILED(hr) ? hr : CopyItems(state_->children, start, capacity, values, actual);
    }
    HRESULT STDMETHODCALLTYPE ReplaceAll(UINT32 count, ITimeline** values) override {
        auto hr = state_->CheckThread();
        if (FAILED(hr)) return hr;
        if (count && !values) return E_POINTER;
        return Guard([&]() -> HRESULT {
            Items next;
            if (count > next.max_size()) return E_OUTOFMEMORY;
            next.reserve(count);
            for (UINT32 i = 0; i < count; ++i) {
                auto result = Child(state_, values[i]);
                if (FAILED(result)) return result;
                next.emplace_back(values[i]);
            }
            state_->children.swap(next);
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE First(Iterator** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = state_->CheckThread();
        if (FAILED(hr)) return hr;
        return Guard([&]() -> HRESULT {
            auto result = Make<TimelineIterator>(state_->children);
            return result ? result.CopyTo(value) : E_OUTOFMEMORY;
        });
    }
};
template <class Derived, class Primary>
class TimelineBase : public Composable<Derived, Primary, ITimeline, DefaultIDependencyObject, IAnimationData> {
    PropertyValues properties_;
    EventSource<Handler> completed_;

protected:
    std::shared_ptr<State> state_ = std::make_shared<State>();
    explicit TimelineBase(IInspectable* outer)
        : Composable<Derived, Primary, ITimeline, DefaultIDependencyObject, IAnimationData>(outer) {
        state_->begin = Make<TimeValue>(TimeSpan{0});
        if (!state_->begin) throw std::bad_alloc();
    }
    HRESULT CheckThread() const { return state_->CheckThread(); }

public:
    HRESULT STDMETHODCALLTYPE GetData(std::shared_ptr<State>& value) override {
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) value = state_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_AutoReverse(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = state_->reverse;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_AutoReverse(boolean value) override {
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) state_->reverse = value != 0;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_BeginTime(TimeReference** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        return FAILED(hr) ? hr : state_->begin.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_BeginTime(TimeReference* value) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        TimeSpan time{};
        if (value) {
            hr = value->get_Value(&time);
            if (FAILED(hr)) return hr;
            if (time.Duration < 0) return E_INVALIDARG;
        }
        state_->begin = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Duration(Duration* value) override {
        if (!value) return E_POINTER;
        *value = {};
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = state_->duration;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_Duration(Duration value) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (value.Type < DurationType_Automatic || value.Type > DurationType_Forever ||
            (value.Type == DurationType_TimeSpan && value.TimeSpan.Duration < 0))
            return E_INVALIDARG;
        state_->duration = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SpeedRatio(double* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = state_->speed;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_SpeedRatio(double value) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (!std::isfinite(value) || value <= 0) return E_INVALIDARG;
        state_->speed = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FillBehavior(FillBehavior* value) override {
        if (!value) return E_POINTER;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = state_->fill;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_FillBehavior(FillBehavior value) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (value != FillBehavior_HoldEnd && value != FillBehavior_Stop) return E_INVALIDARG;
        state_->fill = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_RepeatBehavior(RepeatBehavior* value) override {
        if (!value) return E_POINTER;
        *value = {};
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = state_->repeat;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_RepeatBehavior(RepeatBehavior value) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (value.Type < RepeatBehaviorType_Count || value.Type > RepeatBehaviorType_Forever ||
            (value.Type == RepeatBehaviorType_Count && (!std::isfinite(value.Count) || value.Count < 0)) ||
            (value.Type == RepeatBehaviorType_Duration && value.Duration.Duration < 0))
            return E_INVALIDARG;
        state_->repeat = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_Completed(Handler* value, EventRegistrationToken* token) override {
        if (!token) return E_POINTER;
        *token = {};
        if (!value) return E_INVALIDARG;
        auto hr = CheckThread();
        return FAILED(hr) ? hr : completed_.Add(value, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Completed(EventRegistrationToken token) override {
        auto hr = CheckThread();
        return FAILED(hr) ? hr : completed_.Remove(token);
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
};
class Storyboard final : public TimelineBase<Storyboard, IStoryboard> {
    ComPtr<Vector> children_;
    HRESULT Clock(const wchar_t* member) {
        auto hr = CheckThread();
        return FAILED(hr) ? hr : Unsupported(member);
    }

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Media.Animation.Storyboard";
    explicit Storyboard(IInspectable* outer) : TimelineBase(outer) {
        state_->storyboard = true;
        children_ = Make<TimelineVector>(state_);
        if (!children_) throw std::bad_alloc();
    }
    HRESULT STDMETHODCALLTYPE get_Children(Vector** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        return FAILED(hr) ? hr : children_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE Seek(TimeSpan) override { return Clock(L"Storyboard.Seek"); }
    HRESULT STDMETHODCALLTYPE Stop() override { return Clock(L"Storyboard.Stop"); }
    HRESULT STDMETHODCALLTYPE Begin() override { return Clock(L"Storyboard.Begin"); }
    HRESULT STDMETHODCALLTYPE Pause() override { return Clock(L"Storyboard.Pause"); }
    HRESULT STDMETHODCALLTYPE Resume() override { return Clock(L"Storyboard.Resume"); }
    HRESULT STDMETHODCALLTYPE GetCurrentState(ClockState* value) override {
        if (!value) return E_POINTER;
        *value = ClockState_Stopped;
        return Clock(L"Storyboard.GetCurrentState");
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTime(TimeSpan* value) override {
        if (!value) return E_POINTER;
        *value = {};
        return Clock(L"Storyboard.GetCurrentTime");
    }
    HRESULT STDMETHODCALLTYPE SeekAlignedToLastTick(TimeSpan) override {
        return Clock(L"Storyboard.SeekAlignedToLastTick");
    }
    HRESULT STDMETHODCALLTYPE SkipToFill() override { return Clock(L"Storyboard.SkipToFill"); }
};
class FadeIn final : public TimelineBase<FadeIn, IFadeInThemeAnimation> {
public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Media.Animation.FadeInThemeAnimation";
    explicit FadeIn(IInspectable* outer) : TimelineBase(outer) {}
    HRESULT STDMETHODCALLTYPE get_TargetName(HSTRING* value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return WindowsCreateString(state_->themeTargetName.data(), static_cast<UINT32>(state_->themeTargetName.size()),
                                   value);
    }
    HRESULT STDMETHODCALLTYPE put_TargetName(HSTRING value) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return Guard([&]() -> HRESULT {
            UINT32 length = 0;
            auto text = WindowsGetStringRawBuffer(value, &length);
            state_->themeTargetName.assign(text, length);
            return S_OK;
        });
    }
};
class FadeOut final : public TimelineBase<FadeOut, IFadeOutThemeAnimation> {
public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Media.Animation.FadeOutThemeAnimation";
    explicit FadeOut(IInspectable* outer) : TimelineBase(outer) {}
    HRESULT STDMETHODCALLTYPE get_TargetName(HSTRING* value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return WindowsCreateString(state_->themeTargetName.data(), static_cast<UINT32>(state_->themeTargetName.size()),
                                   value);
    }
    HRESULT STDMETHODCALLTYPE put_TargetName(HSTRING value) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return Guard([&]() -> HRESULT {
            UINT32 length = 0;
            auto text = WindowsGetStringRawBuffer(value, &length);
            state_->themeTargetName.assign(text, length);
            return S_OK;
        });
    }
};
HRESULT GetString(ITimeline* value, bool property, HSTRING* result) {
    if (!result) return E_POINTER;
    *result = nullptr;
    std::shared_ptr<State> state;
    auto hr = Data(value, state);
    if (FAILED(hr)) return hr;
    const auto& text = property ? state->targetProperty : state->targetName;
    return WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), result);
}
HRESULT SetString(ITimeline* value, bool property, HSTRING text) {
    std::shared_ptr<State> state;
    auto hr = Data(value, state);
    if (FAILED(hr)) return hr;
    return Guard([&]() -> HRESULT {
        UINT32 length = 0;
        auto data = WindowsGetStringRawBuffer(text, &length);
        (property ? state->targetProperty : state->targetName).assign(data, length);
        return S_OK;
    });
}
class StoryboardFactory final : public Factory<IStoryboardStatics> {
public:
    StoryboardFactory() : Factory(Storyboard::Name) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return Guard([&]() -> HRESULT {
            ComPtr<IStoryboard> object;
            auto hr = Compose<Storyboard>(nullptr, nullptr, object.GetAddressOf());
            return FAILED(hr) ? hr : object.CopyTo(value);
        });
    }
    HRESULT STDMETHODCALLTYPE get_TargetPropertyProperty(IDependencyProperty** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return Unsupported(L"Storyboard.TargetPropertyProperty");
    }
    HRESULT STDMETHODCALLTYPE GetTargetProperty(ITimeline* value, HSTRING* result) override {
        return GetString(value, true, result);
    }
    HRESULT STDMETHODCALLTYPE SetTargetProperty(ITimeline* value, HSTRING text) override {
        return SetString(value, true, text);
    }
    HRESULT STDMETHODCALLTYPE get_TargetNameProperty(IDependencyProperty** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return Unsupported(L"Storyboard.TargetNameProperty");
    }
    HRESULT STDMETHODCALLTYPE GetTargetName(ITimeline* value, HSTRING* result) override {
        return GetString(value, false, result);
    }
    HRESULT STDMETHODCALLTYPE SetTargetName(ITimeline* value, HSTRING text) override {
        return SetString(value, false, text);
    }
    HRESULT STDMETHODCALLTYPE SetTarget(ITimeline*, IDependencyObject*) override {
        return Unsupported(L"Storyboard.SetTarget");
    }
};
template <class T, class Primary, class Statics> class FadeFactory final : public Factory<Statics> {
public:
    FadeFactory() : Factory<Statics>(T::Name) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return Guard([&]() -> HRESULT {
            ComPtr<Primary> object;
            auto hr = Compose<T>(nullptr, nullptr, object.GetAddressOf());
            return FAILED(hr) ? hr : object.CopyTo(value);
        });
    }
    HRESULT STDMETHODCALLTYPE get_TargetNameProperty(IDependencyProperty** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return Unsupported(L"ThemeAnimation.TargetNameProperty");
    }
};
HRESULT MakeStoryboardFactory(IActivationFactory** value) {
    auto factory = Make<StoryboardFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
HRESULT MakeFadeInFactory(IActivationFactory** value) {
    auto factory = Make<FadeFactory<FadeIn, IFadeInThemeAnimation, IFadeInThemeAnimationStatics>>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
HRESULT MakeFadeOutFactory(IActivationFactory** value) {
    auto factory = Make<FadeFactory<FadeOut, IFadeOutThemeAnimation, IFadeOutThemeAnimationStatics>>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
bool Number(const std::wstring& text, UINT64 limit, UINT64& value) {
    if (text.empty()) return false;
    value = 0;
    for (auto c : text) {
        if (c < L'0' || c > L'9' || value > (limit - (c - L'0')) / 10) return false;
        value = value * 10 + c - L'0';
    }
    return true;
}
}
bool AnimationType(const std::wstring& type) {
    return type == L"Storyboard" || type == L"FadeInThemeAnimation" || type == L"FadeOutThemeAnimation";
}
HRESULT ParseAnimationDuration(const std::wstring& text, Duration* value) {
    if (!value) return E_POINTER;
    *value = {};
    if (text == L"Automatic") {
        value->Type = DurationType_Automatic;
        return S_OK;
    }
    if (text == L"Forever") {
        value->Type = DurationType_Forever;
        return S_OK;
    }
    auto first = text.find(L':'), second = first == std::wstring::npos ? first : text.find(L':', first + 1);
    if (first == std::wstring::npos || second == std::wstring::npos) return E_INVALIDARG;
    auto point = text.find(L'.', second + 1);
    UINT64 hours = 0, minutes = 0, seconds = 0, fraction = 0;
    const auto limit = static_cast<UINT64>(std::numeric_limits<INT64>::max());
    if (!Number(text.substr(0, first), limit / 36000000000ULL, hours) ||
        !Number(text.substr(first + 1, second - first - 1), 59, minutes) ||
        !Number(text.substr(second + 1, point == std::wstring::npos ? point : point - second - 1), 59, seconds))
        return E_INVALIDARG;
    if (point != std::wstring::npos) {
        auto digits = text.substr(point + 1);
        if (digits.size() > 7 || !Number(digits, 9999999, fraction)) return E_INVALIDARG;
        for (size_t i = digits.size(); i < 7; ++i) fraction *= 10;
    }
    const UINT64 rest = minutes * 600000000 + seconds * 10000000 + fraction;
    if (hours * 36000000000ULL > limit - rest) return E_INVALIDARG;
    value->TimeSpan.Duration = static_cast<INT64>(hours * 36000000000ULL + rest);
    value->Type = DurationType_TimeSpan;
    return S_OK;
}
HRESULT ConfigureAnimation(IInspectable* object, const Duration* duration, const std::wstring* targetName,
                           const std::wstring* themeName, const std::wstring* targetProperty) {
    return Guard([&]() -> HRESULT {
        std::shared_ptr<State> state;
        auto hr = Data(object, state);
        if (FAILED(hr)) return hr;
        ComPtr<ITimeline> timeline;
        hr = object->QueryInterface(IID_PPV_ARGS(&timeline));
        if (SUCCEEDED(hr) && duration) hr = timeline->put_Duration(*duration);
        if (FAILED(hr)) return hr;
        if (targetName) state->targetName = *targetName;
        if (themeName) state->themeTargetName = *themeName;
        if (targetProperty) state->targetProperty = *targetProperty;
        return S_OK;
    });
}
HRESULT BindAnimationScope(IInspectable* object, const std::shared_ptr<SourceNameScope>& scope) {
    std::shared_ptr<State> state;
    auto hr = Data(object, state);
    if (SUCCEEDED(hr)) state->scope = scope;
    return hr;
}
HRESULT AppendAnimation(IInspectable* storyboard, IInspectable* child) {
    ComPtr<IStoryboard> parent;
    ComPtr<ITimeline> timeline;
    auto hr = storyboard->QueryInterface(IID_PPV_ARGS(&parent));
    if (SUCCEEDED(hr)) hr = child->QueryInterface(IID_PPV_ARGS(&timeline));
    ComPtr<Vector> children;
    if (SUCCEEDED(hr)) hr = parent->get_Children(&children);
    return FAILED(hr) ? hr : children->Append(timeline.Get());
}
const ShimClass storyboardClass{Storyboard::Name, MakeStoryboardFactory, ShimPolicy::FillMissing};
const ShimClass fadeInClass{FadeIn::Name, MakeFadeInFactory, ShimPolicy::FillMissing};
const ShimClass fadeOutClass{FadeOut::Name, MakeFadeOutFactory, ShimPolicy::FillMissing};
}
