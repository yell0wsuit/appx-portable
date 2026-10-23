#include "generated/coreapp.h"
#include "com/factory.h"
#include "host/host.h"
#include "package/package.h"
#include "registry.h"
#include <windows.applicationmodel.activation.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <atomic>
#include <memory>
#include <thread>
using namespace Microsoft::WRL;
using namespace ABI::Windows::ApplicationModel::Core;
using ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs;
namespace appx {
HRESULT MakeCoreWindow(ABI::Windows::UI::Core::ICoreWindow** value);
HRESULT MakeLaunchArgs(IActivatedEventArgs** value);
void DismissSplashScreen();
void RaiseApplicationSuspending(ABI::Windows::ApplicationModel::ISuspendingEventArgs* args);
void EnableCoreWindowInput() noexcept;
void CoreHostMessage(UINT message, WPARAM wparam, LPARAM lparam);
class CoreView final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultICoreApplicationView, FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.Core.CoreApplicationView", BaseTrust);
    EventSource<
        __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs>
        activated_;

public:
    HRESULT RaiseActivated(IActivatedEventArgs* args) { return activated_.InvokeAll(this, args); }
    HRESULT STDMETHODCALLTYPE get_CoreWindow(ABI::Windows::UI::Core::ICoreWindow** value) override {
        return MakeCoreWindow(value);
    }
    HRESULT STDMETHODCALLTYPE get_IsMain(boolean* value) override {
        if (!value) return E_POINTER;
        *value = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsHosted(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_Activated(
        __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return activated_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Activated(EventRegistrationToken token) override {
        return activated_.Remove(token);
    }
};
// Suspension, which Windows performs before it ends a Store app: handlers save
// their state, taking deferrals for work that finishes later.
class SuspendingDeferral final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, ABI::Windows::ApplicationModel::ISuspendingDeferral, FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.SuspendingDeferral", BaseTrust);
    std::shared_ptr<std::atomic<int>> pending_;
    std::atomic<bool> completed_{false};

public:
    explicit SuspendingDeferral(std::shared_ptr<std::atomic<int>> pending) : pending_(std::move(pending)) {}
    HRESULT STDMETHODCALLTYPE Complete() override {
        if (!completed_.exchange(true)) --*pending_;
        return S_OK;
    }
};
class SuspendingOperation final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, ABI::Windows::ApplicationModel::ISuspendingOperation, FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.SuspendingOperation", BaseTrust);
    std::shared_ptr<std::atomic<int>> pending_;
    ABI::Windows::Foundation::DateTime deadline_;

public:
    SuspendingOperation(std::shared_ptr<std::atomic<int>> pending, ABI::Windows::Foundation::DateTime deadline)
        : pending_(std::move(pending)), deadline_(deadline) {}
    HRESULT STDMETHODCALLTYPE GetDeferral(ABI::Windows::ApplicationModel::ISuspendingDeferral** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto deferral = Make<SuspendingDeferral>(pending_);
        if (!deferral) return E_OUTOFMEMORY;
        ++*pending_;
        return deferral.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_Deadline(ABI::Windows::Foundation::DateTime* value) override {
        if (!value) return E_POINTER;
        *value = deadline_;
        return S_OK;
    }
};
class SuspendingArgs final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, ABI::Windows::ApplicationModel::ISuspendingEventArgs, FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.SuspendingEventArgs", BaseTrust);
    ComPtr<SuspendingOperation> operation_;

public:
    explicit SuspendingArgs(ComPtr<SuspendingOperation> operation) : operation_(std::move(operation)) {}
    HRESULT STDMETHODCALLTYPE get_SuspendingOperation(ABI::Windows::ApplicationModel::ISuspendingOperation** value) override {
        if (!value) return E_POINTER;
        return operation_.CopyTo(value);
    }
};
static ComPtr<CoreView> MainView() {
    static auto view = Make<CoreView>();
    return view;
}
// The IFrameworkView lifecycle, on the UI thread inside the host. The view's
// Run is the game's own loop: it pumps the window through
// CoreDispatcher.ProcessEvents. Closing the window ends the process the way
// Windows ends a Store app (see ExitLikeStoreApp), so Run normally returns
// only when the game leaves its loop itself.
static HRESULT RunFrameworkView(IFrameworkViewSource* source) {
    ComPtr<IFrameworkView> view;
    HRESULT hr = source->CreateView(&view);
    if (FAILED(hr) || !view) return FAILED(hr) ? hr : E_UNEXPECTED;
    EnableCoreWindowInput();  // no XAML: input arrives as CoreWindow events
    auto core = MainView();
    if (!core) return E_OUTOFMEMORY;
    hr = view->Initialize(core.Get());
    Log("TRACE", "IFrameworkView.Initialize hr=%08lx", hr);
    if (FAILED(hr)) return hr;
    ComPtr<ABI::Windows::UI::Core::ICoreWindow> window;
    hr = MakeCoreWindow(&window);
    if (SUCCEEDED(hr)) hr = view->SetWindow(window.Get());
    Log("TRACE", "IFrameworkView.SetWindow hr=%08lx", hr);
    if (FAILED(hr)) return hr;
    const auto& entry = CurrentPackage().entryPoint;
    Wrappers::HStringReference entryPoint(entry.c_str(), static_cast<unsigned>(entry.size()));
    hr = view->Load(entryPoint.Get());
    Log("TRACE", "IFrameworkView.Load(%ls) hr=%08lx", entry.c_str(), hr);
    if (FAILED(hr)) return hr;
    ComPtr<IActivatedEventArgs> args;
    hr = MakeLaunchArgs(&args);
    if (SUCCEEDED(hr)) hr = core->RaiseActivated(args.Get());
    Log("TRACE", "CoreApplicationView.Activated hr=%08lx", hr);
    if (FAILED(hr)) return hr;
    DismissSplashScreen();
    hr = view->Run();
    Log("TRACE", "IFrameworkView.Run returned hr=%08lx", hr);
    view->Uninitialize();
    PostQuitMessage(0);  // the view's loop is over; end the host's
    return hr;
}
class CoreAppFactory final
    : public Factory<DefaultICoreApplication, DefaultICoreApplicationExit, DefaultICoreImmersiveApplication> {
    EventSource<ABI::Windows::Foundation::IEventHandler<ABI::Windows::ApplicationModel::SuspendingEventArgs*>>
        suspending_;
    EventSource<ABI::Windows::Foundation::IEventHandler<IInspectable*>> resuming_, exiting_;

public:
    HRESULT STDMETHODCALLTYPE get_MainView(ICoreApplicationView** value) override {
        if (!value) return E_POINTER;
        auto view = MainView();
        return view ? view.CopyTo(value) : E_OUTOFMEMORY;
    }
    // CoreWindow games (no XAML) start here. As with Application.Start, the
    // view gets its own single-threaded thread and the caller waits for it.
    HRESULT STDMETHODCALLTYPE Run(IFrameworkViewSource* source) override {
        if (!source) return E_INVALIDARG;
        static std::atomic_flag running = ATOMIC_FLAG_INIT;
        if (running.test_and_set()) return E_ILLEGAL_METHOD_CALL;
        Log("TRACE", "CoreApplication.Run");
        HRESULT hr = E_FAIL;
        ComPtr<IFrameworkViewSource> retained(source);
        try {
            std::thread view([&] {
                const auto initialized = RoInitialize(RO_INIT_SINGLETHREADED);
                if (FAILED(initialized)) {
                    hr = initialized;
                    return;
                }
                try {
                    hr = RunHost([&] { return RunFrameworkView(retained.Get()); });
                } catch (const std::bad_alloc&) {
                    hr = E_OUTOFMEMORY;
                } catch (...) {
                    hr = E_FAIL;
                }
                RoUninitialize();
            });
            view.join();
        } catch (const std::bad_alloc&) {
            hr = E_OUTOFMEMORY;
        } catch (...) {
            hr = E_FAIL;
        }
        running.clear();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentView(ICoreApplicationView** value) override { return get_MainView(value); }
    CoreAppFactory() : Factory(L"Windows.ApplicationModel.Core.CoreApplication") {}
    HRESULT STDMETHODCALLTYPE get_Id(HSTRING* value) override {
        if (!value) return E_POINTER;
        // Without a manifest, "App": the default application ID of Store packages.
        const auto& id = CurrentPackage().applicationId.empty() ? std::wstring(L"App") : CurrentPackage().applicationId;
        return WindowsCreateString(id.data(), static_cast<UINT32>(id.size()), value);
    }
    HRESULT STDMETHODCALLTYPE
    add_Suspending(ABI::Windows::Foundation::IEventHandler<ABI::Windows::ApplicationModel::SuspendingEventArgs*>* h,
                   EventRegistrationToken* t) override {
        return suspending_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_Suspending(EventRegistrationToken t) override { return suspending_.Remove(t); }
    // Raises Suspending and returns once every deferral completes or the
    // deadline passes, running UI-thread work meanwhile: games often finish
    // saving through their dispatcher.
    void Suspend(DWORD milliseconds) {
        FILETIME now{};
        GetSystemTimeAsFileTime(&now);
        const INT64 start = static_cast<INT64>(now.dwHighDateTime) << 32 | now.dwLowDateTime;
        auto pending = std::make_shared<std::atomic<int>>(0);
        auto operation = Make<SuspendingOperation>(
            pending, ABI::Windows::Foundation::DateTime{start + static_cast<INT64>(milliseconds) * 10000});
        auto args = operation ? Make<SuspendingArgs>(operation) : nullptr;
        if (!args) return;
        Log("INFO", "suspending before exit, as Windows does when a Store app closes");
        suspending_.InvokeAll(nullptr, args.Get());
        RaiseApplicationSuspending(args.Get());  // XAML games save in Application.Suspending
        const ULONGLONG deadline = GetTickCount64() + milliseconds;
        while (*pending > 0 && GetTickCount64() < deadline) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(10);
        }
        if (*pending > 0) Log("FAIL", "%d suspension deferral(s) still open at the deadline", pending->load());
    }
    HRESULT STDMETHODCALLTYPE add_Resuming(ABI::Windows::Foundation::IEventHandler<IInspectable*>* h,
                                           EventRegistrationToken* t) override {
        return resuming_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_Resuming(EventRegistrationToken t) override { return resuming_.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_Exiting(ABI::Windows::Foundation::IEventHandler<IInspectable*>* h,
                                          EventRegistrationToken* t) override {
        return exiting_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_Exiting(EventRegistrationToken t) override { return exiting_.Remove(t); }
    HRESULT STDMETHODCALLTYPE Exit() override {
        PostMessageW(HostWindow(), WM_CLOSE, 0, 0);
        return S_OK;
    }
};
static ComPtr<CoreAppFactory> SharedFactory() {
    static auto factory = Make<CoreAppFactory>();
    return factory;
}
HRESULT MakeCoreApplicationFactory(IActivationFactory** value) {
    auto factory = SharedFactory();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
// Closing a game's window: Windows 8 suspends a closed Store app so it can
// save, then ends the process; the game's own teardown never runs. Games
// depend on that (Fruit Ninja's teardown corrupts its heap; Cut the Rope 2's
// crashes), so do the same. Without a package (the shim's own tests), the
// host shuts down normally.
bool ExitLikeStoreApp() {
    if (!CurrentPackage().loaded) return false;
    SaveHostPlacement();  // termination skips WM_DESTROY, where it would be saved
    CoreHostMessage(WM_SHOWWINDOW, FALSE, 0);  // VisibilityChanged(false), now
    if (auto factory = SharedFactory()) factory->Suspend(5000);
    Log("INFO", "ending the process after suspension");
    TerminateProcess(GetCurrentProcess(), 0);
    return true;
}
}
