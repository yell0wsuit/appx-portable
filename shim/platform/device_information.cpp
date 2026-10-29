// Windows.Devices.Enumeration.DeviceInformation, deferring to Windows. Wine
// keeps its audio endpoints out of the device interfaces it enumerates, so an
// audio selector finds nothing, and its watcher fails a Removed subscription.
// FMOD's Store build lists and watches the audio endpoints before it opens one
// and plays nothing without them; Where's My Water and Perry then ended
// entering a level. Under Wine, audio selectors are answered from the
// endpoints themselves, with the IDs MediaDevice gives for them.
#include "generated/device_enumeration.h"
#include "com/completed_operation.h"
#include "com/factory.h"
#include "registry.h"
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Devices::Enumeration;
using ABI::Windows::Foundation::ITypedEventHandler;
namespace appx {
namespace {
using DeviceListOperation = __FIAsyncOperation_1_Windows__CDevices__CEnumeration__CDeviceInformationCollection;
using DeviceList = __FIVectorView_1_Windows__CDevices__CEnumeration__CDeviceInformation;
// DEVINTERFACE_AUDIO_RENDER and DEVINTERFACE_AUDIO_CAPTURE, as MediaDevice's selectors name them.
constexpr wchar_t renderClass[] = L"{E6327CAD-DCEC-4949-AE8A-991E976A79D2}";
constexpr wchar_t captureClass[] = L"{2EEF81BE-33FA-4800-9670-1CD474972C3F}";
struct Endpoint {
    std::wstring id, name;
    bool preferred = false;
};
bool OnWine() noexcept {
    static const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
    return wine;
}
// The audio endpoints Wine cannot enumerate, or false for any other query.
bool AudioQuery(HSTRING filter, EDataFlow* flow) {
    if (!OnWine() || !filter) return false;
    std::wstring query(WindowsGetStringRawBuffer(filter, nullptr));
    for (auto& c : query) c = towupper(c);
    if (query.find(renderClass) != std::wstring::npos)
        *flow = eRender;
    else if (query.find(captureClass) != std::wstring::npos)
        *flow = eCapture;
    else
        return false;
    return true;
}
std::vector<Endpoint> Endpoints(EDataFlow flow) {
    std::vector<Endpoint> endpoints;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDeviceCollection> active;
    UINT count = 0;
    if (SUCCEEDED(
            CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enumerator))) &&
        SUCCEEDED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &active)) &&
        SUCCEEDED(active->GetCount(&count))) {
        std::wstring preferred;
        ComPtr<IMMDevice> fallback;
        LPWSTR id = nullptr;
        // MediaDevice's default ID is the console role's.
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &fallback)) &&
            SUCCEEDED(fallback->GetId(&id))) {
            preferred = id;
            CoTaskMemFree(id);
        }
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> device;
            if (FAILED(active->Item(i, &device)) || FAILED(device->GetId(&id))) continue;
            Endpoint endpoint;
            endpoint.preferred = preferred == id;
            endpoint.id =
                std::wstring(L"\\\\?\\SWD#MMDEVAPI#") + id + L"#" + (flow == eRender ? renderClass : captureClass);
            CoTaskMemFree(id);
            ComPtr<IPropertyStore> properties;
            PROPVARIANT name;
            PropVariantInit(&name);
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)) &&
                SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR)
                endpoint.name = name.pwszVal;
            PropVariantClear(&name);
            endpoints.push_back(std::move(endpoint));
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return endpoints;
}
class Device final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIDeviceInformation, FtmBase> {
    InspectableClass(L"Windows.Devices.Enumeration.DeviceInformation", BaseTrust);
    const Endpoint endpoint_;

public:
    explicit Device(Endpoint endpoint) : endpoint_(std::move(endpoint)) {}
    HRESULT STDMETHODCALLTYPE get_Id(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(endpoint_.id.data(), static_cast<UINT32>(endpoint_.id.size()), value);
    }
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(endpoint_.name.data(), static_cast<UINT32>(endpoint_.name.size()), value);
    }
    HRESULT STDMETHODCALLTYPE get_IsEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = true; // only active endpoints are listed
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsDefault(boolean* value) override {
        if (!value) return E_POINTER;
        *value = endpoint_.preferred;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_EnclosureLocation(IEnclosureLocation** value) override {
        if (!value) return E_POINTER;
        *value = nullptr; // an endpoint has no panel location
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Properties(__FIMapView_2_HSTRING_IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        try {
            auto properties = winrt::single_threaded_map<winrt::hstring, winrt::Windows::Foundation::IInspectable>();
            *value = reinterpret_cast<__FIMapView_2_HSTRING_IInspectable*>(winrt::detach_abi(properties.GetView()));
            return S_OK;
        } catch (...) {
            return winrt::to_hresult();
        }
    }
    HRESULT STDMETHODCALLTYPE Update(IDeviceInformationUpdate*) override { return S_OK; }
};
HRESULT MakeDevices(const std::vector<Endpoint>& endpoints, DeviceList** value) {
    *value = nullptr;
    try {
        auto devices = winrt::single_threaded_vector<winrt::Windows::Devices::Enumeration::DeviceInformation>();
        for (const auto& endpoint : endpoints) {
            ComPtr<IDeviceInformation> device = Make<Device>(endpoint);
            if (!device) return E_OUTOFMEMORY;
            winrt::Windows::Devices::Enumeration::DeviceInformation projected{nullptr};
            winrt::copy_from_abi(projected, device.Get());
            devices.Append(projected);
        }
        *value = reinterpret_cast<DeviceList*>(winrt::detach_abi(devices.GetView()));
        return S_OK;
    } catch (...) {
        return winrt::to_hresult();
    }
}
// Reports the endpoints as Added, then EnumerationCompleted, from its own
// thread as Windows' watcher does; endpoints arriving or leaving later are not
// reported.
class Watcher final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIDeviceWatcher, FtmBase> {
    InspectableClass(L"Windows.Devices.Enumeration.DeviceWatcher", BaseTrust);
    using DeviceHandler = ITypedEventHandler<DeviceWatcher*, DeviceInformation*>;
    using UpdateHandler = ITypedEventHandler<DeviceWatcher*, DeviceInformationUpdate*>;
    using StateHandler = ITypedEventHandler<DeviceWatcher*, IInspectable*>;
    EventSource<DeviceHandler> added_;
    EventSource<UpdateHandler> updated_, removed_;
    EventSource<StateHandler> enumerated_, stopped_;
    std::atomic<DeviceWatcherStatus> status_{DeviceWatcherStatus_Created};
    const EDataFlow flow_;

public:
    explicit Watcher(EDataFlow flow) : flow_(flow) {}
    HRESULT STDMETHODCALLTYPE add_Added(DeviceHandler* handler, EventRegistrationToken* token) override {
        return added_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Added(EventRegistrationToken token) override { return added_.Remove(token); }
    HRESULT STDMETHODCALLTYPE add_Updated(UpdateHandler* handler, EventRegistrationToken* token) override {
        return updated_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Updated(EventRegistrationToken token) override { return updated_.Remove(token); }
    HRESULT STDMETHODCALLTYPE add_Removed(UpdateHandler* handler, EventRegistrationToken* token) override {
        return removed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Removed(EventRegistrationToken token) override { return removed_.Remove(token); }
    HRESULT STDMETHODCALLTYPE add_EnumerationCompleted(StateHandler* handler, EventRegistrationToken* token) override {
        return enumerated_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_EnumerationCompleted(EventRegistrationToken token) override {
        return enumerated_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_Stopped(StateHandler* handler, EventRegistrationToken* token) override {
        return stopped_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Stopped(EventRegistrationToken token) override { return stopped_.Remove(token); }
    HRESULT STDMETHODCALLTYPE get_Status(DeviceWatcherStatus* status) override {
        if (!status) return E_POINTER;
        *status = status_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Start() override {
        auto expected = status_.load();
        if (expected != DeviceWatcherStatus_Created && expected != DeviceWatcherStatus_Stopped &&
            expected != DeviceWatcherStatus_Aborted)
            return E_ILLEGAL_METHOD_CALL;
        status_ = DeviceWatcherStatus_Started;
        Log("TRACE", "DeviceWatcher: reporting the %s endpoints", flow_ == eRender ? "render" : "capture");
        ComPtr<Watcher> self(this);
        try {
            std::thread([self] {
                for (auto& endpoint : Endpoints(self->flow_)) {
                    ComPtr<IDeviceInformation> device = Make<Device>(std::move(endpoint));
                    if (device) self->added_.InvokeAll(self.Get(), device.Get());
                }
                auto started = DeviceWatcherStatus_Started;
                if (self->status_.compare_exchange_strong(started, DeviceWatcherStatus_EnumerationCompleted))
                    self->enumerated_.InvokeAll(self.Get(), nullptr);
            }).detach();
        } catch (...) {
            status_ = DeviceWatcherStatus_Aborted;
            return E_OUTOFMEMORY;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Stop() override {
        auto status = status_.load();
        if (status != DeviceWatcherStatus_Started && status != DeviceWatcherStatus_EnumerationCompleted)
            return E_ILLEGAL_METHOD_CALL;
        status_ = DeviceWatcherStatus_Stopped;
        stopped_.InvokeAll(this, nullptr);
        return S_OK;
    }
};
class DeviceInformationFactory final : public Factory<DefaultIDeviceInformationStatics> {
    ComPtr<IDeviceInformationStatics> os_;

    static HRESULT Devices(EDataFlow flow, DeviceListOperation** operation) {
        Log("TRACE", "DeviceInformation: listing the %s endpoints", flow == eRender ? "render" : "capture");
        ComPtr<DeviceList> devices;
        HRESULT hr = MakeDevices(Endpoints(flow), &devices);
        if (FAILED(hr)) return hr;
        auto completed = Make<CompletedOperation<DeviceInformationCollection*, DeviceList*>>(
            [devices](DeviceList** value) { return devices.CopyTo(value); }, true);
        return completed ? completed.CopyTo(operation) : E_OUTOFMEMORY;
    }
    static HRESULT Watch(EDataFlow flow, IDeviceWatcher** watcher) {
        auto made = Make<Watcher>(flow);
        return made ? made.CopyTo(watcher) : E_OUTOFMEMORY;
    }

public:
    DeviceInformationFactory() : Factory(L"Windows.Devices.Enumeration.DeviceInformation") {
        OsGetActivationFactory(Wrappers::HStringReference(L"Windows.Devices.Enumeration.DeviceInformation").Get(),
                               IID_PPV_ARGS(&os_));
    }
    HRESULT STDMETHODCALLTYPE CreateFromIdAsync(
        HSTRING id, __FIAsyncOperation_1_Windows__CDevices__CEnumeration__CDeviceInformation** operation) override {
        return os_ ? os_->CreateFromIdAsync(id, operation) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE CreateFromIdAsyncAdditionalProperties(
        HSTRING id, __FIIterable_1_HSTRING* properties,
        __FIAsyncOperation_1_Windows__CDevices__CEnumeration__CDeviceInformation** operation) override {
        return os_ ? os_->CreateFromIdAsyncAdditionalProperties(id, properties, operation) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE FindAllAsync(DeviceListOperation** operation) override {
        return os_ ? os_->FindAllAsync(operation) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE FindAllAsyncDeviceClass(DeviceClass deviceClass,
                                                      DeviceListOperation** operation) override {
        return os_ ? os_->FindAllAsyncDeviceClass(deviceClass, operation) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE FindAllAsyncAqsFilter(HSTRING filter, DeviceListOperation** operation) override {
        if (!operation) return E_POINTER;
        EDataFlow flow;
        if (AudioQuery(filter, &flow)) return Devices(flow, operation);
        return os_ ? os_->FindAllAsyncAqsFilter(filter, operation) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE FindAllAsyncAqsFilterAndAdditionalProperties(HSTRING filter,
                                                                           __FIIterable_1_HSTRING* properties,
                                                                           DeviceListOperation** operation) override {
        if (!operation) return E_POINTER;
        EDataFlow flow;
        if (AudioQuery(filter, &flow)) return Devices(flow, operation);
        return os_ ? os_->FindAllAsyncAqsFilterAndAdditionalProperties(filter, properties, operation) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE CreateWatcher(IDeviceWatcher** watcher) override {
        return os_ ? os_->CreateWatcher(watcher) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE CreateWatcherDeviceClass(DeviceClass deviceClass, IDeviceWatcher** watcher) override {
        return os_ ? os_->CreateWatcherDeviceClass(deviceClass, watcher) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE CreateWatcherAqsFilter(HSTRING filter, IDeviceWatcher** watcher) override {
        if (!watcher) return E_POINTER;
        EDataFlow flow;
        if (AudioQuery(filter, &flow)) return Watch(flow, watcher);
        return os_ ? os_->CreateWatcherAqsFilter(filter, watcher) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE CreateWatcherAqsFilterAndAdditionalProperties(HSTRING filter,
                                                                            __FIIterable_1_HSTRING* properties,
                                                                            IDeviceWatcher** watcher) override {
        if (!watcher) return E_POINTER;
        EDataFlow flow;
        if (AudioQuery(filter, &flow)) return Watch(flow, watcher);
        return os_ ? os_->CreateWatcherAqsFilterAndAdditionalProperties(filter, properties, watcher) : E_NOTIMPL;
    }
};
HRESULT MakeDeviceInformationFactory(IActivationFactory** value) {
    static auto factory = Make<DeviceInformationFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
const ShimClass deviceInformationClass{L"Windows.Devices.Enumeration.DeviceInformation", MakeDeviceInformationFactory};
}
