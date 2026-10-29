// Windows.Media.Devices.MediaDevice, deferring to Windows. Wine stubs the
// device selectors with E_NOTIMPL, and FMOD's Store build asks for the render
// selector before it opens a device: without one it plays nothing, and Where's
// My Water and Perry then ended entering a level, missing a sound they expected.
// Only then does the shim answer, with the selectors Windows returns.
#include "com/factory.h"
#include "registry.h"
#include <windows.media.devices.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Media::Devices;
namespace appx {
namespace {
// Enabled device interfaces of the audio render or capture class.
constexpr wchar_t renderSelector[] = L"System.Devices.InterfaceClassGuid:=\"{E6327CAD-DCEC-4949-AE8A-991E976A79D2}\""
                                     L" AND System.Devices.InterfaceEnabled:=System.StructuredQueryType.Boolean#True";
constexpr wchar_t captureSelector[] = L"System.Devices.InterfaceClassGuid:=\"{2EEF81BE-33FA-4800-9670-1CD474972C3F}\""
                                      L" AND System.Devices.InterfaceEnabled:=System.StructuredQueryType.Boolean#True";
class MediaDeviceFactory final : public Factory<IMediaDeviceStatics> {
    ComPtr<IMediaDeviceStatics> os_;

    static HRESULT Selector(HRESULT hr, const wchar_t* fallback, HSTRING* selector) noexcept {
        if (hr != E_NOTIMPL) return hr;
        Log("TRACE", "MediaDevice selector: not implemented by Windows; answering %ls", fallback);
        return WindowsCreateString(fallback, static_cast<UINT32>(wcslen(fallback)), selector);
    }

public:
    MediaDeviceFactory() : Factory(L"Windows.Media.Devices.MediaDevice") {
        OsGetActivationFactory(Wrappers::HStringReference(L"Windows.Media.Devices.MediaDevice").Get(),
                               IID_PPV_ARGS(&os_));
    }
    HRESULT STDMETHODCALLTYPE GetAudioCaptureSelector(HSTRING* selector) override {
        if (!selector) return E_POINTER;
        return Selector(os_ ? os_->GetAudioCaptureSelector(selector) : E_NOTIMPL, captureSelector, selector);
    }
    HRESULT STDMETHODCALLTYPE GetAudioRenderSelector(HSTRING* selector) override {
        if (!selector) return E_POINTER;
        return Selector(os_ ? os_->GetAudioRenderSelector(selector) : E_NOTIMPL, renderSelector, selector);
    }
    HRESULT STDMETHODCALLTYPE GetVideoCaptureSelector(HSTRING* selector) override {
        return os_ ? os_->GetVideoCaptureSelector(selector) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetDefaultAudioCaptureId(AudioDeviceRole role, HSTRING* deviceId) override {
        return os_ ? os_->GetDefaultAudioCaptureId(role, deviceId) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetDefaultAudioRenderId(AudioDeviceRole role, HSTRING* deviceId) override {
        return os_ ? os_->GetDefaultAudioRenderId(role, deviceId) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE add_DefaultAudioCaptureDeviceChanged(
        __FITypedEventHandler_2_IInspectable_Windows__CMedia__CDevices__CDefaultAudioCaptureDeviceChangedEventArgs*
            handler,
        EventRegistrationToken* cookie) override {
        return os_ ? os_->add_DefaultAudioCaptureDeviceChanged(handler, cookie) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE remove_DefaultAudioCaptureDeviceChanged(EventRegistrationToken cookie) override {
        return os_ ? os_->remove_DefaultAudioCaptureDeviceChanged(cookie) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE add_DefaultAudioRenderDeviceChanged(
        __FITypedEventHandler_2_IInspectable_Windows__CMedia__CDevices__CDefaultAudioRenderDeviceChangedEventArgs*
            handler,
        EventRegistrationToken* cookie) override {
        return os_ ? os_->add_DefaultAudioRenderDeviceChanged(handler, cookie) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE remove_DefaultAudioRenderDeviceChanged(EventRegistrationToken cookie) override {
        return os_ ? os_->remove_DefaultAudioRenderDeviceChanged(cookie) : E_NOTIMPL;
    }
};
HRESULT MakeMediaDeviceFactory(IActivationFactory** value) {
    static auto factory = Make<MediaDeviceFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
const ShimClass mediaDeviceClass{L"Windows.Media.Devices.MediaDevice", MakeMediaDeviceFactory};
}
