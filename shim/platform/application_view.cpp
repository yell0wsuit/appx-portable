// The executable requests the Windows 8.1 ApplicationView ABI.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#else
#pragma warning(disable : 4996)
#endif
#include "generated/viewmanagement.h"
#include "com/factory.h"
#include "host/host.h"
#include "registry.h"
#include <atomic>
#include <cmath>
#include <mutex>
#include <wrl/event.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::ViewManagement;
namespace appx {
namespace {
ABI::Windows::Foundation::Rect VisibleBounds() {
    RECT r{0, 0, 1024, 768};
    if (auto* window = HostWindow()) GetClientRect(window, &r);
    return {0, 0, static_cast<float>(r.right - r.left), static_cast<float>(r.bottom - r.top)};
}
class View final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIApplicationView, DefaultIApplicationView2,
                                       DefaultIApplicationView3, FtmBase> {
    InspectableClass(L"Windows.UI.ViewManagement.ApplicationView", BaseTrust);
    std::atomic<boolean> capture_{true}, suppress_{false};
    std::atomic<ApplicationViewBoundsMode> boundsMode_{ApplicationViewBoundsMode_UseVisible};
    using ConsolidatedHandler =
        ABI::Windows::Foundation::ITypedEventHandler<ApplicationView*, ApplicationViewConsolidatedEventArgs*>;
    using BoundsHandler = ABI::Windows::Foundation::ITypedEventHandler<ApplicationView*, IInspectable*>;
    EventSource<ConsolidatedHandler> consolidated_;
    EventSource<BoundsHandler> boundsChanged_;
    std::mutex titleMutex_;
    std::wstring title_;

public:
    HRESULT STDMETHODCALLTYPE put_Title(HSTRING value) override {
        std::wstring title = WindowsGetStringRawBuffer(value, nullptr);
        {
            std::lock_guard<std::mutex> lock(titleMutex_);
            title_ = title;
        }
        SetHostTitle(title);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Title(HSTRING* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(titleMutex_);
        return WindowsCreateString(title_.data(), static_cast<UINT32>(title_.size()), value);
    }
    HRESULT STDMETHODCALLTYPE get_Orientation(ApplicationViewOrientation* v) override {
        if (!v) return E_POINTER;
        auto bounds = VisibleBounds();
        *v = bounds.Width >= bounds.Height ? ApplicationViewOrientation_Landscape : ApplicationViewOrientation_Portrait;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_AdjacentToLeftDisplayEdge(boolean* v) override {
        if (!v) return E_POINTER;
        *v = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_AdjacentToRightDisplayEdge(boolean* v) override {
        return get_AdjacentToLeftDisplayEdge(v);
    }
    // Windows 8.1 meaning: the view is not snapped. The host window is the
    // app's whole display at any size, matching get_Value's FullScreen state.
    // The game shows "Switch to fullscreen mode" while this is false.
    HRESULT STDMETHODCALLTYPE get_IsFullScreen(boolean* v) override {
        if (!v) return E_POINTER;
        *v = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsOnLockScreen(boolean* v) override { return get_AdjacentToLeftDisplayEdge(v); }
    HRESULT STDMETHODCALLTYPE get_IsScreenCaptureEnabled(boolean* v) override {
        if (!v) return E_POINTER;
        *v = capture_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsScreenCaptureEnabled(boolean v) override {
        capture_ = v;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Id(INT32* v) override {
        if (!v) return E_POINTER;
        *v = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_Consolidated(ConsolidatedHandler* h, EventRegistrationToken* t) override {
        return consolidated_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_Consolidated(EventRegistrationToken t) override { return consolidated_.Remove(t); }
    HRESULT STDMETHODCALLTYPE get_SuppressSystemOverlays(boolean* v) override {
        if (!v) return E_POINTER;
        *v = suppress_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_SuppressSystemOverlays(boolean v) override {
        suppress_ = v;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_VisibleBounds(ABI::Windows::Foundation::Rect* v) override {
        if (!v) return E_POINTER;
        *v = VisibleBounds();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_VisibleBoundsChanged(BoundsHandler* h, EventRegistrationToken* t) override {
        return boundsChanged_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_VisibleBoundsChanged(EventRegistrationToken t) override {
        return boundsChanged_.Remove(t);
    }
    HRESULT STDMETHODCALLTYPE SetDesiredBoundsMode(ApplicationViewBoundsMode mode, boolean* success) override {
        if (!success) return E_POINTER;
        *success = false;
        if (mode != ApplicationViewBoundsMode_UseVisible && mode != ApplicationViewBoundsMode_UseCoreWindow)
            return E_INVALIDARG;
        boundsMode_ = mode;
        *success = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_DesiredBoundsMode(ApplicationViewBoundsMode* v) override {
        if (!v) return E_POINTER;
        *v = boundsMode_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsFullScreenMode(boolean* v) override {
        if (!v) return E_POINTER;
        *v = IsHostFullscreen();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TryEnterFullScreenMode(boolean* success) override {
        if (!success) return E_POINTER;
        *success = false;
        if (!HostWindow()) return S_OK;
        auto hr = SetHostFullscreen(true);
        if (FAILED(hr)) return hr;
        *success = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ExitFullScreenMode() override { return HostWindow() ? SetHostFullscreen(false) : S_OK; }
    HRESULT STDMETHODCALLTYPE SetPreferredMinSize(ABI::Windows::Foundation::Size size) override {
        if (!std::isfinite(size.Width) || !std::isfinite(size.Height) || size.Width < 0 || size.Height < 0)
            return E_INVALIDARG;
        SetHostMinimumSize(size.Width, size.Height);
        return S_OK;
    }
};
class ViewFactory final
    : public Factory<DefaultIApplicationViewStatics, DefaultIApplicationViewStatics2, IApplicationViewStatics3> {
    ComPtr<View> view_ = Make<View>();
    std::atomic<boolean> terminate_{true};
    std::mutex preferencesMutex_;
    ApplicationViewWindowingMode launchMode_{ApplicationViewWindowingMode_Auto};
    ABI::Windows::Foundation::Size launchSize_{};

public:
    ViewFactory() : Factory(L"Windows.UI.ViewManagement.ApplicationView") {}
    HRESULT STDMETHODCALLTYPE get_PreferredLaunchWindowingMode(ApplicationViewWindowingMode* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(preferencesMutex_);
        *value = launchMode_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_PreferredLaunchWindowingMode(ApplicationViewWindowingMode value) override {
        if (value < ApplicationViewWindowingMode_Auto || value > ApplicationViewWindowingMode_CompactOverlay)
            return E_INVALIDARG;
        std::lock_guard<std::mutex> lock(preferencesMutex_);
        launchMode_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_PreferredLaunchViewSize(ABI::Windows::Foundation::Size* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(preferencesMutex_);
        *value = launchSize_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_PreferredLaunchViewSize(ABI::Windows::Foundation::Size value) override {
        if (!std::isfinite(value.Width) || !std::isfinite(value.Height) || value.Width < 0 || value.Height < 0)
            return E_INVALIDARG;
        std::lock_guard<std::mutex> lock(preferencesMutex_);
        launchSize_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetForCurrentView(IApplicationView** v) override {
        if (!v) return E_POINTER;
        *v = nullptr;
        return view_ ? view_.CopyTo(v) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_Value(ApplicationViewState* v) override {
        if (!v) return E_POINTER;
        auto bounds = VisibleBounds();
        *v = bounds.Width >= bounds.Height ? ApplicationViewState_FullScreenLandscape
                                           : ApplicationViewState_FullScreenPortrait;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TryUnsnap(boolean* success) override {
        if (!success) return E_POINTER;
        *success = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_TerminateAppOnFinalViewClose(boolean* v) override {
        if (!v) return E_POINTER;
        *v = terminate_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_TerminateAppOnFinalViewClose(boolean v) override {
        terminate_ = v;
        return S_OK;
    }
};
}
HRESULT MakeApplicationViewFactory(IActivationFactory** v) {
    static auto factory = Make<ViewFactory>();
    return factory ? factory.CopyTo(v) : E_OUTOFMEMORY;
}
// Desktop XAML Islands also need the portable view: the system view describes
// the island rather than the game's host and reports it as not fullscreen.
const ShimClass applicationViewClass{L"Windows.UI.ViewManagement.ApplicationView", MakeApplicationViewFactory,
                                     ShimPolicy::Replace, InIslands};
}
