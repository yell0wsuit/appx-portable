// EdgeHTML requires CoreWindow, which XAML Islands cannot supply. A WebView
// outer composes a system Grid and renders WebView2 into the island HWND.
#include "com/factory.h"
#include "islands.h"
#include "registry.h"
#include "package/package.h"
#include <WebView2.h>
#include <windows.data.json.h>
#include <windows.ui.xaml.media.h>
#include <windows.storage.h>
#include <wrl/event.h>
#include <memory>
#include <functional>
#include <cmath>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
namespace Xaml = ABI::Windows::UI::Xaml;
namespace Controls = Xaml::Controls;
namespace Foundation = ABI::Windows::Foundation;

namespace appx {
namespace {
std::wstring Text(HSTRING value) {
    UINT32 length;
    const auto* text = WindowsGetStringRawBuffer(value, &length);
    return {text, length};
}
HRESULT String(const std::wstring& text, HSTRING* value) {
    return value ? WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), value) : E_POINTER;
}
HRESULT Uri(const wchar_t* text, Foundation::IUriRuntimeClass** value) {
    std::wstring address(text);
    if (address.starts_with(L"https://appx.invalid/"))
        address.replace(0, wcslen(L"https://appx.invalid/"), L"ms-appx-web:///");
    ComPtr<Foundation::IUriRuntimeClassFactory> factory;
    auto hr = OsGetActivationFactory(HStringReference(L"Windows.Foundation.Uri").Get(), IID_PPV_ARGS(&factory));
    return SUCCEEDED(hr) ? factory->CreateUri(HStringReference(address.c_str()).Get(), value) : hr;
}
std::wstring MapPackageUris(std::wstring text) {
    for (const auto* prefix : {L"ms-appx-web:///", L"ms-appx:///"}) {
        size_t position = 0;
        while ((position = text.find(prefix, position)) != std::wstring::npos) {
            text.replace(position, wcslen(prefix), L"https://appx.invalid/");
            position += wcslen(L"https://appx.invalid/");
        }
    }
    return text;
}
HRESULT ProfileFolder(std::filesystem::path& path) {
    ComPtr<IActivationFactory> factory;
    ComPtr<ABI::Windows::Storage::IApplicationDataStatics> statics;
    ComPtr<ABI::Windows::Storage::IApplicationData> data;
    ComPtr<ABI::Windows::Storage::IStorageFolder> folder;
    ComPtr<ABI::Windows::Storage::IStorageItem> item;
    HString name;
    auto hr = MakeStorageFactory(&factory);
    if (SUCCEEDED(hr)) hr = factory.As(&statics);
    if (SUCCEEDED(hr)) hr = statics->get_Current(&data);
    if (SUCCEEDED(hr)) hr = data->get_LocalFolder(&folder);
    if (SUCCEEDED(hr)) hr = folder.As(&item);
    if (SUCCEEDED(hr)) hr = item->get_Path(name.GetAddressOf());
    if (SUCCEEDED(hr)) path = std::filesystem::path(Text(name.Get())) / L"WebView2";
    return hr;
}
class LoadArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>, Xaml::Navigation::INavigationEventArgs> {
    InspectableClass(L"Windows.UI.Xaml.Navigation.NavigationEventArgs", BaseTrust);
    ComPtr<Foundation::IUriRuntimeClass> uri_;

public:
    explicit LoadArgs(Foundation::IUriRuntimeClass* uri) : uri_(uri) {}
    HRESULT STDMETHODCALLTYPE get_Content(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Parameter(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SourcePageType(Xaml::Interop::TypeName* value) override {
        if (!value) return E_POINTER;
        *value = {};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_NavigationMode(Xaml::Navigation::NavigationMode* value) override {
        if (!value) return E_POINTER;
        *value = Xaml::Navigation::NavigationMode_New;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Uri(Foundation::IUriRuntimeClass** value) override { return uri_.CopyTo(value); }
    HRESULT STDMETHODCALLTYPE put_Uri(Foundation::IUriRuntimeClass* value) override {
        uri_ = value;
        return S_OK;
    }
};
class FailureArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>, Controls::IWebViewNavigationFailedEventArgs> {
    InspectableClass(L"Windows.UI.Xaml.Controls.WebViewNavigationFailedEventArgs", BaseTrust);
    ComPtr<Foundation::IUriRuntimeClass> uri_;
    ABI::Windows::Web::WebErrorStatus status_;

public:
    FailureArgs(Foundation::IUriRuntimeClass* uri, COREWEBVIEW2_WEB_ERROR_STATUS status) : uri_(uri) {
        using namespace ABI::Windows::Web;
        switch (status) {
        case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT:
            status_ = WebErrorStatus_CertificateCommonNameIsIncorrect;
            break;
        case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_EXPIRED:
            status_ = WebErrorStatus_CertificateExpired;
            break;
        case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_REVOKED:
            status_ = WebErrorStatus_CertificateRevoked;
            break;
        case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID:
            status_ = WebErrorStatus_CertificateIsInvalid;
            break;
        case COREWEBVIEW2_WEB_ERROR_STATUS_SERVER_UNREACHABLE:
            status_ = WebErrorStatus_ServerUnreachable;
            break;
        case COREWEBVIEW2_WEB_ERROR_STATUS_TIMEOUT:
            status_ = WebErrorStatus_Timeout;
            break;
        case COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_ABORTED:
            status_ = WebErrorStatus_ConnectionAborted;
            break;
        case COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_RESET:
            status_ = WebErrorStatus_ConnectionReset;
            break;
        case COREWEBVIEW2_WEB_ERROR_STATUS_DISCONNECTED:
            status_ = WebErrorStatus_Disconnected;
            break;
        case COREWEBVIEW2_WEB_ERROR_STATUS_CANNOT_CONNECT:
            status_ = WebErrorStatus_CannotConnect;
            break;
        case COREWEBVIEW2_WEB_ERROR_STATUS_HOST_NAME_NOT_RESOLVED:
            status_ = WebErrorStatus_HostNameNotResolved;
            break;
        case COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED:
            status_ = WebErrorStatus_OperationCanceled;
            break;
        default:
            status_ = WebErrorStatus_Unknown;
            break;
        }
    }
    HRESULT STDMETHODCALLTYPE get_Uri(Foundation::IUriRuntimeClass** value) override { return uri_.CopyTo(value); }
    HRESULT STDMETHODCALLTYPE get_WebErrorStatus(ABI::Windows::Web::WebErrorStatus* value) override {
        if (!value) return E_POINTER;
        *value = status_;
        return S_OK;
    }
};
// JSON quoting prevents string arguments from becoming code, including NULs.
std::wstring Quote(const std::wstring& text) {
    std::wstring result = L"\"";
    for (wchar_t c : text) {
        if (c == L'"' || c == L'\\') {
            result += L'\\';
            result += c;
        } else if (c < 32 || c == 0x2028 || c == 0x2029) {
            wchar_t escape[7];
            swprintf_s(escape, L"\\u%04x", static_cast<unsigned>(c));
            result += escape;
        } else
            result += c;
    }
    return result + L'"';
}
HRESULT ScriptResult(const wchar_t* json, HSTRING* value) {
    ComPtr<ABI::Windows::Data::Json::IJsonValueStatics> factory;
    ComPtr<ABI::Windows::Data::Json::IJsonValue> parsed;
    auto hr = OsGetActivationFactory(HStringReference(L"Windows.Data.Json.JsonValue").Get(), IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) hr = factory->Parse(HStringReference(json).Get(), &parsed);
    if (SUCCEEDED(hr)) {
        ABI::Windows::Data::Json::JsonValueType type;
        hr = parsed->get_ValueType(&type);
        if (SUCCEEDED(hr) && type == ABI::Windows::Data::Json::JsonValueType_String) return parsed->GetString(value);
    }
    return SUCCEEDED(hr) ? String(L"", value) : hr;
}
class NotifyArgs final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, Controls::INotifyEventArgs, Controls::INotifyEventArgs2> {
    InspectableClass(L"Windows.UI.Xaml.Controls.NotifyEventArgs", BaseTrust);
    std::wstring text_;
    ComPtr<Foundation::IUriRuntimeClass> uri_;

public:
    NotifyArgs(std::wstring text, Foundation::IUriRuntimeClass* uri) : text_(std::move(text)), uri_(uri) {}
    HRESULT STDMETHODCALLTYPE get_Value(HSTRING* value) override { return String(text_, value); }
    HRESULT STDMETHODCALLTYPE get_CallingUri(Foundation::IUriRuntimeClass** value) override {
        return uri_.CopyTo(value);
    }
};

class WebView final : public Controls::IWebView {
    std::atomic<ULONG> refs_{1};
    // Only retain the nondelegating inner: ordinary interfaces retain this
    // outer and would form a reference cycle if stored as members.
    ComPtr<IInspectable> inner_;
    struct Lifetime {
        WebView* owner;
    };
    std::shared_ptr<Lifetime> lifetime_ = std::make_shared<Lifetime>(Lifetime{this});
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> browser_;
    ComPtr<Foundation::IUriRuntimeClass> source_;
    EventSource<Controls::INotifyEventHandler> notify_;
    EventSource<Xaml::Navigation::ILoadCompletedEventHandler> loaded_;
    EventSource<Controls::IWebViewNavigationFailedEventHandler> failed_;
    EventRegistrationToken loadedToken_{}, unloadedToken_{}, layoutToken_{};
    EventRegistrationToken messageToken_{}, navigationToken_{};
    bool attached_ = false, starting_ = false, ready_ = false, pendingHtml_ = false, pending_ = false;
    HRESULT browserError_ = S_OK;
    std::wstring navigation_;

    template <class T> HRESULT Inner(ComPtr<T>& out) { return inner_.As(&out); }
    // Game code may invoke synchronous JS from ScriptNotify. Queue it on XAML
    // so it runs after WebView2's callback, where nested pumping is forbidden.
    void Dispatch(std::function<void(WebView*)> work) {
        ComPtr<Xaml::IDependencyObject> object;
        ComPtr<ABI::Windows::UI::Core::ICoreDispatcher> dispatcher;
        if (FAILED(Inner(object)) || FAILED(object->get_Dispatcher(&dispatcher))) return;
        auto weak = std::weak_ptr<Lifetime>(lifetime_);
        auto callback =
            Callback<Implements<RuntimeClassFlags<ClassicCom>, ABI::Windows::UI::Core::IDispatchedHandler, FtmBase>>(
                [weak, work]() -> HRESULT {
                    auto alive = weak.lock();
                    if (alive && alive->owner) {
                        ComPtr<Controls::IWebView> keep(alive->owner);
                        work(alive->owner);
                    }
                    return S_OK;
                });
        ComPtr<Foundation::IAsyncAction> action;
        auto hr = dispatcher->RunAsync(ABI::Windows::UI::Core::CoreDispatcherPriority_Normal, callback.Get(), &action);
        if (FAILED(hr)) Log("FAIL", "WebView2 event dispatch failed hr=%08lx", hr);
    }
    void UpdateBounds() {
        if (!controller_) return;
        ComPtr<Xaml::IFrameworkElement> element;
        ComPtr<Xaml::IUIElement> ui;
        if (FAILED(Inner(element)) || FAILED(Inner(ui))) return;
        Xaml::Visibility visibility = Xaml::Visibility_Collapsed;
        double width = 0, height = 0;
        element->get_ActualWidth(&width);
        element->get_ActualHeight(&height);
        ui->get_Visibility(&visibility);
        ComPtr<Xaml::Media::IGeneralTransform> transform;
        Foundation::Point origin{};
        auto hr = ui->TransformToVisual(nullptr, &transform);
        if (SUCCEEDED(hr)) hr = transform->TransformPoint({}, &origin);
        double scale = 1;
        ComPtr<Xaml::IUIElement10> modern;
        ComPtr<Xaml::IXamlRoot> root;
        if (SUCCEEDED(inner_.As(&modern)) && SUCCEEDED(modern->get_XamlRoot(&root)) && root)
            root->get_RasterizationScale(&scale);
        bool visible = attached_ && SUCCEEDED(hr) && visibility == Xaml::Visibility_Visible && width > 0 && height > 0;
        // An HWND does not inherit XAML's effective visibility. In particular,
        // collapsing a containing panel must hide the browser as well.
        ComPtr<Xaml::Media::IVisualTreeHelperStatics> tree;
        ComPtr<Xaml::IDependencyObject> ancestor;
        if (visible &&
            SUCCEEDED(OsGetActivationFactory(HStringReference(L"Windows.UI.Xaml.Media.VisualTreeHelper").Get(),
                                             IID_PPV_ARGS(&tree))) &&
            SUCCEEDED(Inner(ancestor))) {
            while (ancestor && visible) {
                ComPtr<Xaml::IUIElement> parentUi;
                if (SUCCEEDED(ancestor.As(&parentUi))) {
                    parentUi->get_Visibility(&visibility);
                    visible = visibility == Xaml::Visibility_Visible;
                }
                ComPtr<Xaml::IDependencyObject> parent;
                if (FAILED(tree->GetParent(ancestor.Get(), &parent))) break;
                ancestor = std::move(parent);
            }
        }
        if (visible) {
            RECT bounds{static_cast<LONG>(std::lround(origin.X * scale)),
                        static_cast<LONG>(std::lround(origin.Y * scale)),
                        static_cast<LONG>(std::lround((origin.X + width) * scale)),
                        static_cast<LONG>(std::lround((origin.Y + height) * scale))};
            controller_->put_Bounds(bounds);
        }
        controller_->put_IsVisible(visible);
    }
    HRESULT NavigatePending() {
        if (!ready_ || !pending_) return S_OK;
        pending_ = false;
        return pendingHtml_ ? browser_->NavigateToString(navigation_.c_str()) : browser_->Navigate(navigation_.c_str());
    }
    HRESULT Connect(ICoreWebView2Controller* controller) {
        controller_ = controller;
        auto hr = controller->get_CoreWebView2(&browser_);
        if (FAILED(hr)) return hr;
        ComPtr<ICoreWebView2_3> mapping;
        hr = browser_.As(&mapping);
        if (SUCCEEDED(hr))
            hr = mapping->SetVirtualHostNameToFolderMapping(L"appx.invalid", CurrentPackage().directory.c_str(),
                                                            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        if (FAILED(hr)) return hr;
        auto weak = std::weak_ptr<Lifetime>(lifetime_);
        auto message = Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [weak](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                auto alive = weak.lock();
                if (!alive || !alive->owner) return S_OK;
                LPWSTR text = nullptr, source = nullptr;
                auto hr = args->TryGetWebMessageAsString(&text);
                args->get_Source(&source);
                // Match legacy local-content trust. Remote pages do not gain
                // access to the application's ScriptNotify handlers.
                const std::wstring origin = source ? source : L"";
                const bool local = origin == L"about:blank" || origin.starts_with(L"about:blank#") ||
                                   origin.starts_with(L"https://appx.invalid/");
                if (SUCCEEDED(hr) && local) {
                    ComPtr<Foundation::IUriRuntimeClass> uri;
                    if (source) Uri(source, &uri);
                    auto event = Make<NotifyArgs>(std::wstring(text), uri.Get());
                    alive->owner->Dispatch([event](WebView* self) {
                        self->notify_.InvokeAll(static_cast<IInspectable*>(self), event.Get());
                    });
                }
                CoTaskMemFree(text);
                CoTaskMemFree(source);
                return S_OK;
            });
        hr = browser_->add_WebMessageReceived(message.Get(), &messageToken_);
        if (FAILED(hr)) return hr;
        auto completed = Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [weak](ICoreWebView2* browser, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                auto alive = weak.lock();
                if (!alive || !alive->owner) return S_OK;
                BOOL success = FALSE;
                args->get_IsSuccess(&success);
                Log(success ? "TRACE" : "FAIL", "WebView2 navigation completed success=%d", success);
                LPWSTR address = nullptr;
                ComPtr<Foundation::IUriRuntimeClass> uri;
                if (SUCCEEDED(browser->get_Source(&address)) && address) Uri(address, &uri);
                CoTaskMemFree(address);
                if (success) {
                    auto event = Make<LoadArgs>(uri.Get());
                    alive->owner->Dispatch([event](WebView* self) {
                        self->loaded_.InvokeAll(static_cast<IInspectable*>(self), event.Get());
                    });
                } else {
                    COREWEBVIEW2_WEB_ERROR_STATUS status{};
                    args->get_WebErrorStatus(&status);
                    auto event = Make<FailureArgs>(uri.Get(), status);
                    alive->owner->Dispatch([event](WebView* self) {
                        self->failed_.InvokeAll(static_cast<IInspectable*>(self), event.Get());
                    });
                }
                return S_OK;
            });
        hr = browser_->add_NavigationCompleted(completed.Get(), &navigationToken_);
        if (FAILED(hr)) return hr;
        auto script = Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
            [weak](HRESULT status, LPCWSTR) -> HRESULT {
                auto alive = weak.lock();
                if (!alive || !alive->owner) return S_OK;
                alive->owner->browserError_ = status;
                if (SUCCEEDED(status)) {
                    alive->owner->ready_ = true;
                    alive->owner->UpdateBounds();
                    return alive->owner->NavigatePending();
                }
                return S_OK;
            });
        return browser_->AddScriptToExecuteOnDocumentCreated(
            L"window.external = window.external || {}; window.external.notify = function(s) { "
            L"window.chrome.webview.postMessage(String(s)); };",
            script.Get());
    }
    HRESULT Start() {
        if (starting_ || browser_ || !attached_) return browserError_;
        const HWND parent = IslandWindow();
        if (!parent) return E_PENDING;
        starting_ = true;
        auto weak = std::weak_ptr<Lifetime>(lifetime_);
        auto environment = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [weak, parent](HRESULT status, ICoreWebView2Environment* env) -> HRESULT {
                auto alive = weak.lock();
                if (!alive || !alive->owner) return S_OK;
                if (FAILED(status)) {
                    alive->owner->browserError_ = status;
                    Log("FAIL", "WebView2 environment failed hr=%08lx (Evergreen runtime required)", status);
                    return S_OK;
                }
                auto complete = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [weak](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                        auto life = weak.lock();
                        if (!life || !life->owner) {
                            if (controller) controller->Close();
                            return S_OK;
                        }
                        if (SUCCEEDED(result)) result = life->owner->Connect(controller);
                        life->owner->browserError_ = result;
                        if (FAILED(result)) Log("FAIL", "WebView2 controller failed hr=%08lx", result);
                        return S_OK;
                    });
                auto hr = env->CreateCoreWebView2Controller(parent, complete.Get());
                if (FAILED(hr)) alive->owner->browserError_ = hr;
                return S_OK;
            });
        std::filesystem::path folder;
        auto hr = ProfileFolder(folder);
        if (SUCCEEDED(hr))
            hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, folder.c_str(), nullptr, environment.Get());
        if (FAILED(hr)) {
            browserError_ = hr;
            Log("FAIL", "WebView2 startup failed hr=%08lx", hr);
        }
        return hr;
    }

public:
    ~WebView() {
        lifetime_->owner = nullptr;
        ComPtr<Xaml::IFrameworkElement> element;
        if (inner_ && SUCCEEDED(Inner(element))) {
            element->remove_Loaded(loadedToken_);
            element->remove_Unloaded(unloadedToken_);
            element->remove_LayoutUpdated(layoutToken_);
        }
        if (browser_) {
            browser_->remove_WebMessageReceived(messageToken_);
            browser_->remove_NavigationCompleted(navigationToken_);
        }
        if (controller_) controller_->Close();
    }
    HRESULT Initialize() {
        ComPtr<Controls::IGridFactory> factory;
        auto hr =
            OsGetActivationFactory(HStringReference(L"Windows.UI.Xaml.Controls.Grid").Get(), IID_PPV_ARGS(&factory));
        ComPtr<Controls::IGrid> grid;
        if (SUCCEEDED(hr)) hr = factory->CreateInstance(this, &inner_, &grid);
        if (FAILED(hr)) return hr;
        ComPtr<Xaml::IFrameworkElement> element;
        hr = Inner(element);
        if (FAILED(hr)) return hr;
        auto weak = std::weak_ptr<Lifetime>(lifetime_);
        auto loaded = Callback<Xaml::IRoutedEventHandler>([weak](IInspectable*, Xaml::IRoutedEventArgs*) -> HRESULT {
            auto life = weak.lock();
            if (life && life->owner) {
                life->owner->attached_ = true;
                life->owner->Start();
                life->owner->UpdateBounds();
            }
            return S_OK;
        });
        auto unloaded = Callback<Xaml::IRoutedEventHandler>([weak](IInspectable*, Xaml::IRoutedEventArgs*) -> HRESULT {
            auto life = weak.lock();
            if (life && life->owner) {
                life->owner->attached_ = false;
                life->owner->UpdateBounds();
            }
            return S_OK;
        });
        auto layout =
            Callback<Foundation::IEventHandler<IInspectable*>>([weak](IInspectable*, IInspectable*) -> HRESULT {
                auto life = weak.lock();
                if (life && life->owner) life->owner->UpdateBounds();
                return S_OK;
            });
        hr = element->add_Loaded(loaded.Get(), &loadedToken_);
        if (SUCCEEDED(hr)) hr = element->add_Unloaded(unloaded.Get(), &unloadedToken_);
        if (SUCCEEDED(hr)) hr = element->add_LayoutUpdated(layout.Get(), &layoutToken_);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable) || iid == __uuidof(Controls::IWebView)) {
            *value = static_cast<Controls::IWebView*>(this);
            AddRef();
            return S_OK;
        }
        return inner_ ? inner_->QueryInterface(iid, value) : E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto n = --refs_;
        if (!n) {
            // Cleanup queries the inner for event removal. Its delegating
            // interfaces briefly retain the outer while it is being destroyed.
            refs_ = 1;
            delete this;
        }
        return n;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** ids) override {
        if (!count || !ids) return E_POINTER;
        *count = 0;
        *ids = static_cast<IID*>(CoTaskMemAlloc(sizeof(IID)));
        if (!*ids) return E_OUTOFMEMORY;
        **ids = __uuidof(Controls::IWebView);
        *count = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* value) override {
        return String(L"Windows.UI.Xaml.Controls.WebView", value);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* value) override {
        if (!value) return E_POINTER;
        *value = BaseTrust;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Source(Foundation::IUriRuntimeClass** value) override {
        return source_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Source(Foundation::IUriRuntimeClass* value) override { return Navigate(value); }
    HRESULT STDMETHODCALLTYPE get_AllowedScriptNotifyUris(__FIVector_1_Windows__CFoundation__CUri** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE put_AllowedScriptNotifyUris(__FIVector_1_Windows__CFoundation__CUri*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE
    get_DataTransferPackage(ABI::Windows::ApplicationModel::DataTransfer::IDataPackage** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE add_LoadCompleted(Xaml::Navigation::ILoadCompletedEventHandler* handler,
                                                EventRegistrationToken* token) override {
        return loaded_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_LoadCompleted(EventRegistrationToken token) override {
        return loaded_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_ScriptNotify(Controls::INotifyEventHandler* handler,
                                               EventRegistrationToken* token) override {
        return notify_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_ScriptNotify(EventRegistrationToken token) override {
        return notify_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_NavigationFailed(Controls::IWebViewNavigationFailedEventHandler* handler,
                                                   EventRegistrationToken* token) override {
        return failed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_NavigationFailed(EventRegistrationToken token) override {
        return failed_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE Navigate(Foundation::IUriRuntimeClass* source) override {
        if (!source) return E_INVALIDARG;
        HString uri;
        auto hr = source->get_AbsoluteUri(uri.GetAddressOf());
        if (FAILED(hr)) return hr;
        source_ = source;
        navigation_ = MapPackageUris(Text(uri.Get()));
        pendingHtml_ = false;
        pending_ = true;
        return FAILED(browserError_) ? browserError_ : NavigatePending();
    }
    HRESULT STDMETHODCALLTYPE NavigateToString(HSTRING text) override {
        navigation_ = MapPackageUris(Text(text));
        pendingHtml_ = true;
        pending_ = true;
        source_.Reset();
        return FAILED(browserError_) ? browserError_ : NavigatePending();
    }
    HRESULT STDMETHODCALLTYPE InvokeScript(HSTRING name, UINT32 count, HSTRING* arguments, HSTRING* value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (count && !arguments) return E_POINTER;
        if (FAILED(browserError_)) return browserError_;
        if (!browser_) return E_PENDING;
        // A caller can remove the element while the nested loop dispatches UI.
        ComPtr<Controls::IWebView> keep(this);
        const auto script = Text(name);
        if (script.empty()) return E_INVALIDARG;
        // Resolve dotted function names without interpreting the name as code.
        std::wstring expression = L"(()=>{let f=window,o=window;for(const k of " + Quote(script) +
                                  L".split('.')){o=f;f=f[k];}return f.apply(o,[";
        for (UINT32 i = 0; i < count; ++i) {
            if (i) expression += L',';
            expression += Quote(Text(arguments[i]));
        }
        expression += L"]);})()";
        struct Result {
            bool done = false;
            HRESULT status = E_PENDING;
            std::wstring json;
        };
        auto result = std::make_shared<Result>();
        auto complete =
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>([result](HRESULT status, LPCWSTR json) -> HRESULT {
                result->status = status;
                result->json = json ? json : L"null";
                result->done = true;
                return S_OK;
            });
        auto hr = browser_->ExecuteScript(expression.c_str(), complete.Get());
        if (FAILED(hr)) return hr;
        const auto deadline = GetTickCount64() + 10000;
        while (!result->done) {
            if (GetTickCount64() >= deadline) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            MSG message;
            while (!result->done && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    PostQuitMessage(static_cast<int>(message.wParam));
                    return E_ABORT;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!result->done) MsgWaitForMultipleObjectsEx(0, nullptr, 25, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        return SUCCEEDED(result->status) ? ScriptResult(result->json.c_str(), value) : result->status;
    }
};

class WebViewFactory final : public Factory<Controls::IWebViewFactory4> {
    ComPtr<IActivationFactory> real_;

public:
    WebViewFactory() : Factory(L"Windows.UI.Xaml.Controls.WebView") {}
    HRESULT Initialize() {
        return OsGetActivationFactory(HStringReference(L"Windows.UI.Xaml.Controls.WebView").Get(),
                                      IID_PPV_ARGS(&real_));
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) noexcept override {
        if (!out) return E_POINTER;
        if (iid == __uuidof(Controls::IWebViewStatics) || iid == __uuidof(Controls::IWebViewStatics2) ||
            iid == __uuidof(Controls::IWebViewStatics3) || iid == __uuidof(Controls::IWebViewStatics4) ||
            iid == __uuidof(Controls::IWebViewStatics5))
            return real_->QueryInterface(iid, out);
        return Factory::QueryInterface(iid, out);
    }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto* web = new (std::nothrow) WebView;
        if (!web) return E_OUTOFMEMORY;
        auto hr = web->Initialize();
        if (SUCCEEDED(hr))
            *value = web;
        else
            web->Release();
        Log(SUCCEEDED(hr) ? "TRACE" : "FAIL", "WebView2 stand-in activation hr=%08lx", hr);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateInstanceWithExecutionMode(Controls::WebViewExecutionMode mode,
                                                              Controls::IWebView** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (mode < Controls::WebViewExecutionMode_SameThread || mode > Controls::WebViewExecutionMode_SeparateProcess)
            return E_INVALIDARG;
        ComPtr<IInspectable> object;
        auto hr = ActivateInstance(&object);
        return SUCCEEDED(hr) ? object.CopyTo(value) : hr;
    }
};
HRESULT MakeWebViewFactory(IActivationFactory** value) {
    if (!IslandsMode()) return CLASS_E_CLASSNOTAVAILABLE;
    auto factory = Make<WebViewFactory>();
    if (!factory) return E_OUTOFMEMORY;
    const auto hr = factory->Initialize();
    return SUCCEEDED(hr) ? factory.CopyTo(value) : hr;
}
const ShimClass webViewClass{L"Windows.UI.Xaml.Controls.WebView", MakeWebViewFactory, ShimPolicy::Replace, InIslands};
}
}
