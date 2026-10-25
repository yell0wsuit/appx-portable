#include "generated/xaml.h"
#include "com/factory.h"
#include "com/composable.h"
#include "host/host.h"
#include "registry.h"
#include "islands/islands.h"
#include "package/package.h"
#include "element.h"
#include "compat/win10/xaml.h"
#include <shlwapi.h>
#include <xmllite.h>
#include <windows.applicationmodel.activation.h>
#include <windows.ui.xaml.markup.h>
#include <wrl/event.h>
#include <thread>
#include <atomic>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::ApplicationModel::Activation;
namespace appx {
// Windows 8 packages retain source XAML, including the generated connection
// IDs. Bind supported named elements and their handlers from those IDs.
static HRESULT ConnectSourceXaml(ABI::Windows::UI::Xaml::Markup::IComponentConnector* connector,
                                 IFrameworkElement* element, const wchar_t* filename) {
    const auto path = CurrentPackage().directory / filename;
    ComPtr<IStream> stream;
    auto hr = SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &stream);
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) return S_FALSE;
    if (FAILED(hr)) return hr;
    ComPtr<IXmlReader> reader;
    hr = CreateXmlReader(IID_PPV_ARGS(&reader), nullptr);
    if (SUCCEEDED(hr)) hr = reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit);
    if (SUCCEEDED(hr)) hr = reader->SetInput(stream.Get());
    if (FAILED(hr)) return hr;
    XmlNodeType type{};
    while ((hr = reader->Read(&type)) == S_OK) {
        if (type != XmlNodeType_Element) continue;
        const wchar_t* tag = nullptr;
        reader->GetLocalName(&tag, nullptr);
        if (!tag) continue;
        const std::wstring elementType = tag;
        std::wstring name, connection, visibility;
        if (reader->MoveToAttributeByName(L"Name", L"http://schemas.microsoft.com/winfx/2006/xaml") == S_OK) {
            const wchar_t* value = nullptr;
            reader->GetValue(&value, nullptr);
            if (value) name = value;
            reader->MoveToElement();
        }
        if (reader->MoveToAttributeByName(L"ConnectionId", L"http://schemas.microsoft.com/winfx/2006/xaml") == S_OK) {
            const wchar_t* value = nullptr;
            reader->GetValue(&value, nullptr);
            if (value) connection = value;
            reader->MoveToElement();
        }
        if (connection.empty()) continue;
        if (reader->MoveToAttributeByName(L"Visibility", nullptr) == S_OK) {
            const wchar_t* value = nullptr;
            reader->GetValue(&value, nullptr);
            if (value) visibility = value;
            reader->MoveToElement();
        }
        wchar_t* end = nullptr;
        const long id = wcstol(connection.c_str(), &end, 10);
        if (!end || *end || id <= 0 || id == LONG_MAX) return E_INVALIDARG;
        ComPtr<IInspectable> target;
        const wchar_t* canonical = elementType == L"MediaElement"               ? L"mediaElement"
                                   : elementType == L"SwapChainBackgroundPanel" ? L"SwapChainPanel"
                                                                                : L"swapChainPanel";
        if (elementType == L"UserControl") {
            hr = element->QueryInterface(IID_PPV_ARGS(&target));
        } else if (elementType == L"MediaElement" || elementType == L"SwapChainBackgroundPanel" ||
                   elementType == L"SwapChainPanel") {
            hr = element->FindName(Wrappers::HStringReference(canonical).Get(), &target);
        } else if (CurrentPackage().universal) {
            hr = MakeWin10XamlElement(elementType.c_str(), &target);
        } else {
            continue;
        }
        if (FAILED(hr)) return hr;
        if (!target) continue;
        ComPtr<INamedElementRegistry> names;
        hr = element->QueryInterface(IID_PPV_ARGS(&names));
        if (SUCCEEDED(hr) && !name.empty())
            hr = names->RegisterName(Wrappers::HStringReference(name.c_str()).Get(), target.Get());
        if (FAILED(hr)) return hr;
        if (visibility == L"Collapsed") {
            ComPtr<IUIElement> ui;
            hr = target.As(&ui);
            if (SUCCEEDED(hr)) hr = ui->put_Visibility(Visibility_Collapsed);
            if (FAILED(hr)) return hr;
        }
        hr = connector->Connect(id, target.Get());
        if (FAILED(hr)) return hr;
    }
    return hr == S_FALSE ? S_OK : hr;
}
static ComPtr<IApplication> current;
HRESULT CurrentApplication(IApplication** value) {
    if (!value) return E_POINTER;
    return current.CopyTo(value);
}
// The launch splash screen. Windows dismisses its splash once the view has
// activated; games wait for Dismissed (or subscribe unconditionally), so it
// must exist and must fire.
using DismissedHandler = __FITypedEventHandler_2_Windows__CApplicationModel__CActivation__CSplashScreen_IInspectable;
class Splash final : public RuntimeClass<RuntimeClassFlags<WinRt>, ISplashScreen, FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.Activation.SplashScreen", BaseTrust);
    EventSource<DismissedHandler> dismissed_;
    std::atomic<bool> fired_{false};

public:
    HRESULT STDMETHODCALLTYPE get_ImageLocation(ABI::Windows::Foundation::Rect* value) override {
        if (!value) return E_POINTER;
        // The 620x300 Windows 8 splash image, centered in the window, in DIPs.
        RECT client{0, 0, 1024, 768};
        if (HostWindow()) GetClientRect(HostWindow(), &client);
        const float width = 620, height = 300;
        *value = {(client.right - width) / 2, (client.bottom - height) / 2, width, height};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_Dismissed(DismissedHandler* handler, EventRegistrationToken* token) override {
        return dismissed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Dismissed(EventRegistrationToken token) override {
        return dismissed_.Remove(token);
    }
    void Dismiss() {
        if (fired_.exchange(true)) return;
        Log("TRACE", "SplashScreen.Dismissed");
        dismissed_.InvokeAll(this, nullptr);
    }
};
static ComPtr<Splash> SharedSplash() {
    static auto splash = Make<Splash>();
    return splash;
}
// Queued behind the activation, so handlers run after the view is up.
void DismissSplashScreen() {
    PostHostWork(0, [] {
        if (auto splash = SharedSplash()) splash->Dismiss();
    });
}
class LaunchArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>, ILaunchActivatedEventArgs, IActivatedEventArgs,
                                             IPrelaunchActivatedEventArgs> {
    InspectableClass(L"Windows.ApplicationModel.Activation.LaunchActivatedEventArgs", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE get_PrelaunchActivated(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Arguments(HSTRING* v) override {
        if (!v) return E_POINTER;
        *v = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_TileId(HSTRING* v) override {
        if (!v) return E_POINTER;
        *v = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Kind(ActivationKind* v) override {
        if (!v) return E_POINTER;
        *v = ActivationKind_Launch;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_PreviousExecutionState(ApplicationExecutionState* v) override {
        if (!v) return E_POINTER;
        *v = ApplicationExecutionState_NotRunning;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SplashScreen(ISplashScreen** v) override {
        if (!v) return E_POINTER;
        *v = nullptr;
        auto splash = SharedSplash();
        return splash ? splash.CopyTo(v) : E_OUTOFMEMORY;
    }
};
class InitParams final : public RuntimeClass<RuntimeClassFlags<WinRt>, IApplicationInitializationCallbackParams> {
    InspectableClass(L"Windows.UI.Xaml.ApplicationInitializationCallbackParams", BaseTrust);
};
class Application;
static Application* live = nullptr; // the game's Application, for Suspending
class Application final : public Composable<Application, DefaultIApplication, DefaultIApplicationOverrides> {
    EventSource<ISuspendingEventHandler> suspending_;
    EventSource<ABI::Windows::Foundation::IEventHandler<IInspectable*>> resuming_;
    EventSource<IUnhandledExceptionEventHandler> unhandled_;
    ComPtr<IResourceDictionary> resources_;
    ApplicationTheme theme_ = ApplicationTheme_Dark;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Application";
    explicit Application(IInspectable* outer) : Composable(outer) { live = this; }
    ~Application() {
        if (live == this) live = nullptr;
    }
    HRESULT RaiseSuspending(IInspectable* sender, ABI::Windows::ApplicationModel::ISuspendingEventArgs* args) {
        return suspending_.InvokeAll(sender, args);
    }
    HRESULT STDMETHODCALLTYPE get_Resources(IResourceDictionary** value) override {
        if (!value) return E_POINTER;
        return resources_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Resources(IResourceDictionary* value) override {
        resources_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_RequestedTheme(ApplicationTheme* value) override {
        if (!value) return E_POINTER;
        *value = theme_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_RequestedTheme(ApplicationTheme value) override {
        theme_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_UnhandledException(IUnhandledExceptionEventHandler* handler,
                                                     EventRegistrationToken* token) override {
        return unhandled_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_UnhandledException(EventRegistrationToken token) override {
        return unhandled_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_Suspending(ISuspendingEventHandler* handler, EventRegistrationToken* token) override {
        return suspending_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Suspending(EventRegistrationToken token) override {
        return suspending_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_Resuming(ABI::Windows::Foundation::IEventHandler<IInspectable*>* handler,
                                           EventRegistrationToken* token) override {
        return resuming_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Resuming(EventRegistrationToken token) override { return resuming_.Remove(token); }
    HRESULT STDMETHODCALLTYPE Exit() override {
        PostMessageW(HostWindow(), WM_CLOSE, 0, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnLaunched(ILaunchActivatedEventArgs*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnActivated(IActivatedEventArgs*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnWindowCreated(IWindowCreatedEventArgs*) override { return S_OK; }
};
class ApplicationFactory final : public Factory<DefaultIApplicationFactory, DefaultIApplicationStatics> {
public:
    ApplicationFactory() : Factory(L"Windows.UI.Xaml.Application") {}
    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* outer, IInspectable** inner, IApplication** value) override {
        auto hr = Compose<Application>(outer, inner, value);
        if (SUCCEEDED(hr) && HostWindow()) current = *value;
        Log("TRACE", "ApplicationFactory::CreateInstance hr=%08lx", hr);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_Current(IApplication** value) override {
        if (!value) return E_POINTER;
        return current.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE LoadComponent(IInspectable* component,
                                            ABI::Windows::Foundation::IUriRuntimeClass* uri) override {
        if (!uri) return E_INVALIDARG;
        HSTRING text{};
        uri->get_AbsoluteUri(&text);
        Log("TRACE", "Application::LoadComponent %ls", WindowsGetStringRawBuffer(text, nullptr));
        const bool movie = wcscmp(WindowsGetStringRawBuffer(text, nullptr), L"ms-appx:///MovieMgrPage.xaml") == 0;
        const bool directX = wcscmp(WindowsGetStringRawBuffer(text, nullptr), L"ms-appx:///DirectXPage.xaml") == 0;
        if (component && CurrentPackage().universal) {
            ComPtr<ABI::Windows::UI::Xaml::Markup::IComponentConnector> connector;
            if (SUCCEEDED(component->QueryInterface(IID_PPV_ARGS(&connector)))) {
                ComPtr<IFrameworkElement> element;
                auto hr = component->QueryInterface(IID_PPV_ARGS(&element));
                std::wstring path = WindowsGetStringRawBuffer(text, nullptr);
                const std::wstring prefix = L"ms-appx:///";
                if (SUCCEEDED(hr) && path.rfind(prefix, 0) == 0) {
                    path.erase(0, prefix.size());
                    if (!path.empty() && path.find_first_of(L"\\/:") == std::wstring::npos) {
                        hr = ConnectSourceXaml(connector.Get(), element.Get(), path.c_str());
                        if (hr != S_FALSE) {
                            WindowsDeleteString(text);
                            return hr;
                        }
                    }
                }
            }
        }
        WindowsDeleteString(text);
        if ((movie || directX) && component) {
            // Connection 1 installs the movie events or the DirectX panel's
            // pointer handlers. FindName alone only binds the generated field.
            ComPtr<ABI::Windows::UI::Xaml::Markup::IComponentConnector> connector;
            auto hr = component->QueryInterface(IID_PPV_ARGS(&connector));
            if (FAILED(hr)) return hr;
            ComPtr<IFrameworkElement> element;
            hr = component->QueryInterface(IID_PPV_ARGS(&element));
            if (FAILED(hr)) return hr;
            if (directX) {
                hr = ConnectSourceXaml(connector.Get(), element.Get(), L"DirectXPage.xaml");
                if (hr != S_FALSE) return hr;
            }
            ComPtr<IInspectable> target;
            hr = element->FindName(Wrappers::HStringReference(movie ? L"mediaElement" : L"SwapChainPanel").Get(),
                                   &target);
            if (FAILED(hr)) return hr;
            return connector->Connect(1, target.Get());
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE LoadComponentWithResourceLocation(
        IInspectable* component, ABI::Windows::Foundation::IUriRuntimeClass* uri,
        ABI::Windows::UI::Xaml::Controls::Primitives::ComponentResourceLocation) override {
        return LoadComponent(component, uri);
    }
    HRESULT STDMETHODCALLTYPE Start(IApplicationInitializationCallback* callback) override {
        if (!callback) return E_INVALIDARG;
        static std::atomic_flag running = ATOMIC_FLAG_INIT;
        if (running.test_and_set()) return E_ILLEGAL_METHOD_CALL;
        Log("TRACE", "ApplicationStatics::Start");
        HRESULT hr = E_FAIL;
        ComPtr<IApplicationInitializationCallback> retained(callback);
        try {
            std::thread view([&] {
                auto initialized = RoInitialize(RO_INIT_SINGLETHREADED);
                if (FAILED(initialized)) {
                    hr = initialized;
                    return;
                }
                try {
                    hr = RunHost([&]() -> HRESULT {
                        auto params = Make<InitParams>();
                        if (!params) return E_OUTOFMEMORY;
                        auto result = retained->Invoke(params.Get());
                        if (FAILED(result)) return result;
                        if (!current) return E_UNEXPECTED;
                        ComPtr<IApplicationOverrides> overrides;
                        result = current.As(&overrides);
                        if (FAILED(result)) return result;
                        auto args = Make<LaunchArgs>();
                        if (!args) return E_OUTOFMEMORY;
                        result = overrides->OnLaunched(args.Get());
                        Log("TRACE", "OnLaunched hr=%08lx", result);
                        if (SUCCEEDED(result)) DismissSplashScreen();
                        return result;
                    });
                } catch (const std::bad_alloc&) {
                    hr = E_OUTOFMEMORY;
                } catch (...) {
                    hr = E_FAIL;
                }
                current.Reset();
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
};
HRESULT MakeApplicationFactory(IActivationFactory** value) {
    auto factory = Make<ApplicationFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
// Application.Suspending, which XAML games save in.
void RaiseApplicationSuspending(ABI::Windows::ApplicationModel::ISuspendingEventArgs* args) {
    if (IslandsMode())
        RaiseIslandSuspending(args);
    else if (live)
        live->RaiseSuspending(current.Get(), args);
}
HRESULT MakeLaunchArgs(IActivatedEventArgs** value) {
    if (!value) return E_POINTER;
    auto args = Make<LaunchArgs>();
    return args ? args.CopyTo(value) : E_OUTOFMEMORY;
}
}
