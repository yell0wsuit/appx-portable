// IImage's ABI still contains the deprecated PlayToSource getter.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#else
#pragma warning(disable : 4996)
#endif
#include "element.h"
#include "com/factory.h"
#include "registry.h"
#include <windows.ui.xaml.controls.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
namespace appx {
// Overlay image state for native DirectX views, without an OS XAML core.
// The desktop host does not render XAML image overlays yet.
class Image final : public Element<Image, IImage> {
    ComPtr<Media::IImageSource> source_;
    Media::Stretch stretch_ = Media::Stretch_Uniform;
    Thickness nineGrid_{};
    EventSource<IExceptionRoutedEventHandler> failed_;
    EventSource<IRoutedEventHandler> opened_;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.Image";
    explicit Image(IInspectable* outer) : Element(outer) {}
    HRESULT STDMETHODCALLTYPE get_Source(Media::IImageSource** value) override {
        if (!value) return E_POINTER;
        return source_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Source(Media::IImageSource* value) override {
        source_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Stretch(Media::Stretch* value) override {
        if (!value) return E_POINTER;
        *value = stretch_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Stretch(Media::Stretch value) override {
        if (value < Media::Stretch_None || value > Media::Stretch_UniformToFill) return E_INVALIDARG;
        stretch_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_NineGrid(Thickness* value) override {
        if (!value) return E_POINTER;
        *value = nineGrid_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_NineGrid(Thickness value) override {
        nineGrid_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_PlayToSource(ABI::Windows::Media::PlayTo::IPlayToSource** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_ImageFailed(IExceptionRoutedEventHandler* handler,
                                              EventRegistrationToken* token) override {
        return failed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_ImageFailed(EventRegistrationToken token) override {
        return failed_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_ImageOpened(IRoutedEventHandler* handler, EventRegistrationToken* token) override {
        return opened_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_ImageOpened(EventRegistrationToken token) override {
        return opened_.Remove(token);
    }
};
class ImageFactory final : public Factory<> {
public:
    ImageFactory() : Factory(Image::Name) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        ComPtr<IImage> image;
        const auto hr = Compose<Image>(nullptr, nullptr, image.GetAddressOf());
        return FAILED(hr) ? hr : image.CopyTo(value);
    }
};
HRESULT MakeImageFactory(IActivationFactory** value) {
    auto factory = Make<ImageFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
const ShimClass imageClass{Image::Name, MakeImageFactory};
}
