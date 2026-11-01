// Storyboard clocks driven by the UI thread's real message loop. Failures
// return the failing line so the managed probe names the broken expectation.
#include <algorithm>
#include <functional>
#include <string>
#include <vector>
namespace clock_fixture {
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Media::Animation;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using Handler = IEventHandler<IInspectable*>;
// Windows.UI.Xaml originates this for unresolvable animation targets.
constexpr HRESULT AnimationError = static_cast<HRESULT>(0x800F1000L);
#define CLOCK_CHECK(value)                                                                                             \
    if (!(value)) return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, __LINE__)
struct Record {
    std::vector<std::wstring> order;
    bool bad = false;
};
class Observer final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, Handler> {
    Record* record_;
    std::wstring label_;
    IUnknown* expected_; // Borrowed: the observer must not keep its sender alive.
    const DWORD thread_ = GetCurrentThreadId();
    bool* destroyed_;

public:
    std::function<void()> action;
    Observer(Record* record, const wchar_t* label, IUnknown* expected, bool* destroyed = nullptr)
        : record_(record), label_(label), expected_(expected), destroyed_(destroyed) {}
    ~Observer() {
        if (destroyed_) *destroyed_ = true;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IInspectable* sender, IInspectable* args) override {
        record_->order.push_back(label_);
        ComPtr<IUnknown> identity;
        if (!sender || FAILED(sender->QueryInterface(IID_PPV_ARGS(&identity))) || identity.Get() != expected_ || args ||
            GetCurrentThreadId() != thread_)
            record_->bad = true;
        if (action) action();
        return S_OK;
    }
};
INT64 Now() {
    LARGE_INTEGER frequency, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    return now.QuadPart / frequency.QuadPart * 10000000 +
           now.QuadPart % frequency.QuadPart * 10000000 / frequency.QuadPart;
}
// Dispatch the UI thread's messages until done() or the deadline.
HRESULT Pump(DWORD milliseconds, const std::function<bool()>& done = [] { return false; }) {
    const auto end = GetTickCount64() + milliseconds;
    for (;;) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(message.wParam));
                return E_FAIL;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (done()) return S_OK;
        const auto now = GetTickCount64();
        if (now >= end) return S_FALSE;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(end - now), QS_ALLINPUT);
    }
}
HRESULT Find(const wchar_t* name, IStoryboard** value) {
    ComPtr<IUIElement> root;
    auto hr = WindowContent(&root);
    ComPtr<IFrameworkElement> element;
    if (SUCCEEDED(hr)) hr = root.As(&element);
    ComPtr<IInspectable> named;
    if (SUCCEEDED(hr)) hr = element->FindName(Wrappers::HStringReference(name).Get(), &named);
    if (SUCCEEDED(hr) && !named) hr = E_FAIL;
    return FAILED(hr) ? hr : named.CopyTo(value);
}
HRESULT Child(IStoryboard* storyboard, UINT32 index, ITimeline** value) {
    ComPtr<IVector<Timeline*>> children;
    auto hr = storyboard->get_Children(&children);
    return FAILED(hr) ? hr : children->GetAt(index, value);
}
ClockState State(IStoryboard* storyboard) {
    auto value = static_cast<ClockState>(-1);
    return SUCCEEDED(storyboard->GetCurrentState(&value)) ? value : static_cast<ClockState>(-1);
}
INT64 Time(IStoryboard* storyboard) {
    TimeSpan value{-1};
    return SUCCEEDED(storyboard->GetCurrentTime(&value)) ? value.Duration : -1;
}
HRESULT Observe(IInspectable* timeline, Record* record, const wchar_t* label, ComPtr<Observer>& observer,
                EventRegistrationToken* token) {
    ComPtr<ITimeline> value;
    ComPtr<IUnknown> identity;
    auto hr = timeline->QueryInterface(IID_PPV_ARGS(&value));
    if (SUCCEEDED(hr)) hr = timeline->QueryInterface(IID_PPV_ARGS(&identity));
    if (FAILED(hr)) return hr;
    observer = Make<Observer>(record, label, identity.Get());
    return observer ? value->add_Completed(observer.Get(), token) : E_OUTOFMEMORY;
}
HRESULT Unobserve(IInspectable* timeline, EventRegistrationToken token) {
    ComPtr<ITimeline> value;
    auto hr = timeline->QueryInterface(IID_PPV_ARGS(&value));
    return FAILED(hr) ? hr : value->remove_Completed(token);
}
size_t Count(const Record& record, const wchar_t* label) {
    return std::count(record.order.begin(), record.order.end(), std::wstring(label));
}
HRESULT Lifecycle() {
    ComPtr<IStoryboard> enter;
    ComPtr<ITimeline> fade;
    CLOCK_CHECK(SUCCEEDED(Find(L"enter", &enter)) && SUCCEEDED(Child(enter.Get(), 0, &fade)));
    CLOCK_CHECK(State(enter.Get()) == ClockState_Stopped && Time(enter.Get()) == 0);
    Record record;
    ComPtr<Observer> board, child;
    EventRegistrationToken boardToken{}, childToken{};
    CLOCK_CHECK(SUCCEEDED(Observe(enter.Get(), &record, L"board", board, &boardToken)));
    CLOCK_CHECK(SUCCEEDED(Observe(fade.Get(), &record, L"child", child, &childToken)));
    auto started = Now();
    CLOCK_CHECK(SUCCEEDED(enter->Begin()));
    // Completion is raised from a later tick, never inside Begin.
    CLOCK_CHECK(record.order.empty() && State(enter.Get()) == ClockState_Active);
    auto time = Time(enter.Get());
    CLOCK_CHECK(time >= 0 && time <= 2000000);
    CLOCK_CHECK(Pump(5000, [&] { return Count(record, L"board") == 1; }) == S_OK);
    CLOCK_CHECK(Now() - started >= 2000000);
    CLOCK_CHECK(!record.bad && Count(record, L"child") == 1);
    CLOCK_CHECK(State(enter.Get()) == ClockState_Filling && Time(enter.Get()) == 2000000);
    // Begin restarts a filling clock; a handler may restart its own storyboard.
    bool restarted = false;
    board->action = [&] {
        if (!restarted) restarted = SUCCEEDED(enter->Begin());
    };
    CLOCK_CHECK(SUCCEEDED(enter->Begin()) && State(enter.Get()) == ClockState_Active);
    CLOCK_CHECK(Pump(5000, [&] { return Count(record, L"board") == 3; }) == S_OK);
    CLOCK_CHECK(restarted && !record.bad && Count(record, L"child") == 3);
    CLOCK_CHECK(State(enter.Get()) == ClockState_Filling);
    board->action = nullptr;
    // Stop discards a running clock without completion; stopping twice is harmless.
    CLOCK_CHECK(SUCCEEDED(enter->Begin()) && SUCCEEDED(enter->Stop()) && SUCCEEDED(enter->Stop()));
    CLOCK_CHECK(State(enter.Get()) == ClockState_Stopped && Time(enter.Get()) == 0);
    CLOCK_CHECK(Pump(400) == S_FALSE && Count(record, L"board") == 3 && Count(record, L"child") == 3);
    CLOCK_CHECK(SUCCEEDED(Unobserve(enter.Get(), boardToken)) && SUCCEEDED(Unobserve(fade.Get(), childToken)));
    return S_OK;
}
HRESULT Targets() {
    // Own TargetName wins over Storyboard.TargetName; children inherit the storyboard's.
    ComPtr<IStoryboard> inherited;
    ComPtr<ITimeline> first, second;
    CLOCK_CHECK(SUCCEEDED(Find(L"inherited", &inherited)) && SUCCEEDED(Child(inherited.Get(), 0, &first)) &&
                SUCCEEDED(Child(inherited.Get(), 1, &second)));
    Record record;
    ComPtr<Observer> board, early, late;
    EventRegistrationToken tokens[3]{};
    CLOCK_CHECK(SUCCEEDED(Observe(inherited.Get(), &record, L"board", board, &tokens[0])) &&
                SUCCEEDED(Observe(first.Get(), &record, L"early", early, &tokens[1])) &&
                SUCCEEDED(Observe(second.Get(), &record, L"late", late, &tokens[2])));
    // A child's handler can stop its storyboard before the remaining clocks finish.
    early->action = [&] { inherited->Stop(); };
    CLOCK_CHECK(SUCCEEDED(inherited->Begin()));
    CLOCK_CHECK(Pump(5000, [&] { return Count(record, L"early") == 1; }) == S_OK);
    CLOCK_CHECK(State(inherited.Get()) == ClockState_Stopped);
    CLOCK_CHECK(Pump(400) == S_FALSE && record.order.size() == 1 && !record.bad);
    early->action = nullptr;
    CLOCK_CHECK(SUCCEEDED(inherited->Begin()));
    CLOCK_CHECK(Pump(5000, [&] { return Count(record, L"board") == 1; }) == S_OK);
    CLOCK_CHECK(!record.bad && Count(record, L"early") == 2 && Count(record, L"late") == 1);
    CLOCK_CHECK(record.order[1] == L"early" && record.order[2] == L"late");
    CLOCK_CHECK(SUCCEEDED(Unobserve(inherited.Get(), tokens[0])) && SUCCEEDED(Unobserve(first.Get(), tokens[1])) &&
                SUCCEEDED(Unobserve(second.Get(), tokens[2])));
    // Unresolvable targets fail before the clock changes state.
    const wchar_t* unresolved[] = {L"missing", L"untargeted", L"nonvisual"};
    for (auto* name : unresolved) {
        ComPtr<IStoryboard> storyboard;
        CLOCK_CHECK(SUCCEEDED(Find(name, &storyboard)));
        CLOCK_CHECK(storyboard->Begin() == AnimationError && State(storyboard.Get()) == ClockState_Stopped);
    }
    // Windows' Animations theme data gives FadeIn/FadeOut one 167 ms opacity transform.
    ComPtr<IStoryboard> automatic;
    ComPtr<Observer> observer;
    EventRegistrationToken token{};
    CLOCK_CHECK(SUCCEEDED(Find(L"automatic", &automatic)));
    CLOCK_CHECK(SUCCEEDED(Observe(automatic.Get(), &record, L"automatic", observer, &token)));
    auto started = Now();
    CLOCK_CHECK(SUCCEEDED(automatic->Begin()));
    CLOCK_CHECK(Pump(5000, [&] { return Count(record, L"automatic") == 1; }) == S_OK);
    CLOCK_CHECK(Now() - started >= 1670000 && !record.bad);
    CLOCK_CHECK(State(automatic.Get()) == ClockState_Filling && Time(automatic.Get()) == 1670000);
    CLOCK_CHECK(SUCCEEDED(Unobserve(automatic.Get(), token)));
    return S_OK;
}
HRESULT Durations() {
    // An explicit storyboard Duration ends the clock before a longer child.
    ComPtr<IStoryboard> clipped;
    CLOCK_CHECK(SUCCEEDED(Find(L"clipped", &clipped)));
    Record record;
    ComPtr<Observer> board;
    EventRegistrationToken token{};
    CLOCK_CHECK(SUCCEEDED(Observe(clipped.Get(), &record, L"board", board, &token)));
    auto started = Now();
    CLOCK_CHECK(SUCCEEDED(clipped->Begin()));
    CLOCK_CHECK(Pump(5000, [&] { return Count(record, L"board") == 1; }) == S_OK);
    CLOCK_CHECK(Now() - started >= 500000 && Now() - started < 50000000 && !record.bad);
    CLOCK_CHECK(State(clipped.Get()) == ClockState_Filling && Time(clipped.Get()) == 500000);
    CLOCK_CHECK(SUCCEEDED(Unobserve(clipped.Get(), token)));
    // A Forever child keeps an automatic storyboard active until stopped.
    ComPtr<IStoryboard> forever;
    CLOCK_CHECK(SUCCEEDED(Find(L"forever", &forever)));
    CLOCK_CHECK(SUCCEEDED(Observe(forever.Get(), &record, L"forever", board, &token)));
    CLOCK_CHECK(SUCCEEDED(forever->Begin()) && Pump(300) == S_FALSE && Count(record, L"forever") == 0);
    CLOCK_CHECK(State(forever.Get()) == ClockState_Active && Time(forever.Get()) >= 3000000);
    CLOCK_CHECK(SUCCEEDED(forever->Stop()) && State(forever.Get()) == ClockState_Stopped);
    CLOCK_CHECK(SUCCEEDED(Unobserve(forever.Get(), token)));
    // An empty storyboard completes on a later tick; FillBehavior Stop ends stopped.
    ComPtr<IStoryboard> empty;
    ComPtr<ITimeline> timeline;
    CLOCK_CHECK(SUCCEEDED(Find(L"empty", &empty)) && SUCCEEDED(empty.As(&timeline)));
    CLOCK_CHECK(SUCCEEDED(timeline->put_FillBehavior(FillBehavior_Stop)));
    CLOCK_CHECK(SUCCEEDED(Observe(empty.Get(), &record, L"empty", board, &token)));
    CLOCK_CHECK(SUCCEEDED(empty->Begin()) && Count(record, L"empty") == 0);
    CLOCK_CHECK(Pump(5000, [&] { return Count(record, L"empty") == 1; }) == S_OK);
    CLOCK_CHECK(!record.bad && State(empty.Get()) == ClockState_Stopped && Time(empty.Get()) == 0);
    CLOCK_CHECK(SUCCEEDED(timeline->put_FillBehavior(FillBehavior_HoldEnd)) &&
                SUCCEEDED(Unobserve(empty.Get(), token)));
    return S_OK;
}
HRESULT Unsupported() {
    ComPtr<IInspectable> object;
    CLOCK_CHECK(SUCCEEDED(animation_fixture::Activate(L"Windows.UI.Xaml.Media.Animation.Storyboard", &object)));
    ComPtr<IStoryboard> storyboard;
    ComPtr<ITimeline> timeline;
    CLOCK_CHECK(SUCCEEDED(object.As(&storyboard)) && SUCCEEDED(object.As(&timeline)));
    // Timing modifiers the clock does not model fail by name before any state change.
    CLOCK_CHECK(SUCCEEDED(timeline->put_SpeedRatio(2)) && storyboard->Begin() == E_NOTIMPL);
    CLOCK_CHECK(SUCCEEDED(timeline->put_SpeedRatio(1)) && SUCCEEDED(timeline->put_AutoReverse(true)) &&
                storyboard->Begin() == E_NOTIMPL);
    CLOCK_CHECK(SUCCEEDED(timeline->put_AutoReverse(false)) &&
                SUCCEEDED(timeline->put_RepeatBehavior({2, {0}, RepeatBehaviorType_Count})) &&
                storyboard->Begin() == E_NOTIMPL);
    CLOCK_CHECK(SUCCEEDED(timeline->put_RepeatBehavior({1, {0}, RepeatBehaviorType_Count})) &&
                SUCCEEDED(timeline->put_BeginTime(nullptr)) && storyboard->Begin() == E_NOTIMPL);
    CLOCK_CHECK(State(storyboard.Get()) == ClockState_Stopped);
    ComPtr<IInspectable> other;
    CLOCK_CHECK(SUCCEEDED(animation_fixture::Activate(L"Windows.UI.Xaml.Media.Animation.Storyboard", &other)));
    ComPtr<IStoryboard> fresh;
    ComPtr<ITimeline> nested;
    ComPtr<IVector<Timeline*>> children;
    CLOCK_CHECK(SUCCEEDED(other.As(&fresh)) && SUCCEEDED(fresh->get_Children(&children)));
    CLOCK_CHECK(SUCCEEDED(animation_fixture::Activate(L"Windows.UI.Xaml.Media.Animation.Storyboard", &object)) &&
                SUCCEEDED(object.As(&nested)) && SUCCEEDED(children->Append(nested.Get())));
    CLOCK_CHECK(fresh->Begin() == E_NOTIMPL && State(fresh.Get()) == ClockState_Stopped);
    // A programmatic theme child has no namescope to resolve its target in.
    ComPtr<IInspectable> fadeObject;
    ComPtr<ITimeline> fade;
    ComPtr<IInspectable> factoryObject;
    ComPtr<IActivationFactory> factory;
    ComPtr<IStoryboardStatics> statics;
    CLOCK_CHECK(SUCCEEDED(animation_fixture::Activate(L"Windows.UI.Xaml.Media.Animation.Storyboard", &factoryObject,
                                                      &factory)) &&
                SUCCEEDED(factory.As(&statics)));
    CLOCK_CHECK(
        SUCCEEDED(animation_fixture::Activate(L"Windows.UI.Xaml.Media.Animation.FadeInThemeAnimation", &fadeObject)) &&
        SUCCEEDED(fadeObject.As(&fade)) && SUCCEEDED(fade->put_Duration({{100000}, DurationType_TimeSpan})) &&
        SUCCEEDED(statics->SetTargetName(fade.Get(), Wrappers::HStringReference(L"leaf").Get())));
    CLOCK_CHECK(SUCCEEDED(children->Clear()) && SUCCEEDED(children->Append(fade.Get())));
    CLOCK_CHECK(fresh->Begin() == AnimationError && State(fresh.Get()) == ClockState_Stopped);
    // Clock calls belong to the creating thread.
    HRESULT results[4]{};
    std::thread worker([&] {
        ClockState state;
        TimeSpan time;
        results[0] = fresh->Begin();
        results[1] = fresh->Stop();
        results[2] = fresh->GetCurrentState(&state);
        results[3] = fresh->GetCurrentTime(&time);
    });
    worker.join();
    for (auto result : results) CLOCK_CHECK(result == RPC_E_WRONG_THREAD);
    // Seeking and pausing are separate, still-unimplemented clock operations.
    CLOCK_CHECK(fresh->Pause() == E_NOTIMPL && fresh->Resume() == E_NOTIMPL && fresh->Seek({0}) == E_NOTIMPL &&
                fresh->SeekAlignedToLastTick({0}) == E_NOTIMPL && fresh->SkipToFill() == E_NOTIMPL);
    return S_OK;
}
HRESULT Lifetime() {
    // The running clock owns its storyboard until completion, then lets it go.
    bool destroyed = false;
    Record record;
    {
        ComPtr<IInspectable> object;
        CLOCK_CHECK(SUCCEEDED(animation_fixture::Activate(L"Windows.UI.Xaml.Media.Animation.Storyboard", &object)));
        ComPtr<IStoryboard> storyboard;
        ComPtr<ITimeline> timeline;
        ComPtr<IUnknown> identity;
        CLOCK_CHECK(SUCCEEDED(object.As(&storyboard)) && SUCCEEDED(object.As(&timeline)) &&
                    SUCCEEDED(object.As(&identity)));
        CLOCK_CHECK(SUCCEEDED(timeline->put_Duration({{1000000}, DurationType_TimeSpan})));
        auto observer = Make<Observer>(&record, L"orphan", identity.Get(), &destroyed);
        EventRegistrationToken token{};
        CLOCK_CHECK(observer && SUCCEEDED(timeline->add_Completed(observer.Get(), &token)));
        CLOCK_CHECK(SUCCEEDED(storyboard->Begin()));
    }
    CLOCK_CHECK(!destroyed);
    CLOCK_CHECK(Pump(5000, [&] { return destroyed; }) == S_OK);
    CLOCK_CHECK(Count(record, L"orphan") == 1 && !record.bad);
    return S_OK;
}
#undef CLOCK_CHECK
}
extern "C" HRESULT __cdecl ExerciseAnimationClock(int stage) {
    switch (stage) {
    case 0:
        return clock_fixture::Lifecycle();
    case 1:
        return clock_fixture::Targets();
    case 2:
        return clock_fixture::Durations();
    case 3:
        return clock_fixture::Unsupported();
    case 4:
        return clock_fixture::Lifetime();
    }
    return E_INVALIDARG;
}
