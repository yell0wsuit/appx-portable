// IImage still declares the deprecated PlayToSource property.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "image.h"
#include "element.h"
#include "source.h"
#include "com/factory.h"
#include "registry.h"
#include "package/resources.h"
#include <windows.ui.xaml.media.imaging.h>
#include <limits>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
using namespace ABI::Windows::UI::Xaml::Media;
using namespace ABI::Windows::UI::Xaml::Media::Imaging;
namespace appx {
namespace {
HRESULT Unsupported(const wchar_t* name) {
    Log("UNIMPL", "%ls hr=%08lx", name, E_NOTIMPL);
    return E_NOTIMPL;
}
class PackageBitmap final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IImageSource, IBitmapSource,
                                                DefaultIDependencyObject, IImagePixels> {
    InspectableClass(L"AppxPortable.Xaml.PackageBitmapSource", BaseTrust);
    const DWORD thread_ = GetCurrentThreadId();
    ComPtr<IWICBitmapSource> bitmap_;
    INT32 width_, height_;
    PropertyValues properties_;
    HRESULT CheckThread() const { return GetCurrentThreadId() == thread_ ? S_OK : RPC_E_WRONG_THREAD; }
    HRESULT Missing(const wchar_t* name) const {
        auto hr = CheckThread();
        return FAILED(hr) ? hr : Unsupported(name);
    }

public:
    PackageBitmap(IWICBitmapSource* bitmap, UINT width, UINT height)
        : bitmap_(bitmap), width_(width), height_(height) {}
    HRESULT STDMETHODCALLTYPE GetBitmap(IWICBitmapSource** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        return FAILED(hr) ? hr : bitmap_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_PixelWidth(INT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = width_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_PixelHeight(INT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = height_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetSource(ABI::Windows::Storage::Streams::IRandomAccessStream*) override {
        return Missing(L"PackageBitmapSource.SetSource");
    }
    HRESULT STDMETHODCALLTYPE SetSourceAsync(ABI::Windows::Storage::Streams::IRandomAccessStream*,
                                             ABI::Windows::Foundation::IAsyncAction** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return Missing(L"PackageBitmapSource.SetSourceAsync");
    }
    HRESULT STDMETHODCALLTYPE GetValue(IDependencyProperty* p, IInspectable** v) override {
        return properties_.Get(p, v);
    }
    HRESULT STDMETHODCALLTYPE SetValue(IDependencyProperty* p, IInspectable* v) override {
        return properties_.Set(this, p, v);
    }
    HRESULT STDMETHODCALLTYPE ClearValue(IDependencyProperty* p) override {
        return properties_.Set(this, p, nullptr, true);
    }
    HRESULT STDMETHODCALLTYPE ReadLocalValue(IDependencyProperty* p, IInspectable** v) override {
        return properties_.Get(p, v, true);
    }
    HRESULT STDMETHODCALLTYPE GetAnimationBaseValue(IDependencyProperty* p, IInspectable** v) override {
        return properties_.Get(p, v);
    }
};
class Image final : public Element<Image, IImage> {
    ComPtr<IImageSource> source_;
    Stretch stretch_ = Stretch_Uniform;
    HRESULT Missing(const wchar_t* name) {
        auto hr = CheckSourceThread();
        return FAILED(hr) ? hr : Unsupported(name);
    }

public:
    static constexpr auto Name = L"Windows.UI.Xaml.Controls.Image";
    explicit Image(IInspectable* outer) : Element(outer) {}
    HRESULT STDMETHODCALLTYPE get_Source(IImageSource** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckSourceThread();
        return FAILED(hr) ? hr : source_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Source(IImageSource* value) override {
        auto hr = CheckSourceThread();
        if (SUCCEEDED(hr)) source_ = value;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_Stretch(Stretch* value) override {
        if (!value) return E_POINTER;
        *value = {};
        auto hr = CheckSourceThread();
        if (SUCCEEDED(hr)) *value = stretch_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_Stretch(Stretch value) override {
        auto hr = CheckSourceThread();
        if (FAILED(hr)) return hr;
        if (value < Stretch_None || value > Stretch_UniformToFill) return E_INVALIDARG;
        stretch_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_NineGrid(Thickness* value) override {
        if (!value) return E_POINTER;
        *value = {};
        return Missing(L"Image.NineGrid");
    }
    HRESULT STDMETHODCALLTYPE put_NineGrid(Thickness) override { return Missing(L"Image.NineGrid"); }
    HRESULT STDMETHODCALLTYPE get_PlayToSource(ABI::Windows::Media::PlayTo::IPlayToSource** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return Missing(L"Image.PlayToSource");
    }
    HRESULT STDMETHODCALLTYPE add_ImageFailed(IExceptionRoutedEventHandler*, EventRegistrationToken* token) override {
        if (!token) return E_POINTER;
        *token = {};
        return Missing(L"Image.ImageFailed");
    }
    HRESULT STDMETHODCALLTYPE remove_ImageFailed(EventRegistrationToken) override {
        return Missing(L"Image.ImageFailed");
    }
    HRESULT STDMETHODCALLTYPE add_ImageOpened(IRoutedEventHandler*, EventRegistrationToken* token) override {
        if (!token) return E_POINTER;
        *token = {};
        return Missing(L"Image.ImageOpened");
    }
    HRESULT STDMETHODCALLTYPE remove_ImageOpened(EventRegistrationToken) override {
        return Missing(L"Image.ImageOpened");
    }
};
class ImageFactory final : public Factory<> {
public:
    ImageFactory() : Factory<>(Image::Name) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        try {
            ComPtr<IImage> image;
            auto hr = Compose<Image>(nullptr, nullptr, image.GetAddressOf());
            return FAILED(hr) ? hr : image.CopyTo(value);
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        }
    }
};
HRESULT MakeImageFactory(IActivationFactory** value) {
    auto factory = Make<ImageFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
HRESULT MakePackageImageSource(const std::wstring& text, IImageSource** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    try {
        ComPtr<ABI::Windows::Foundation::IUriRuntimeClassFactory> factory;
        auto hr =
            OsGetActivationFactory(Wrappers::HStringReference(L"Windows.Foundation.Uri").Get(), IID_PPV_ARGS(&factory));
        ComPtr<ABI::Windows::Foundation::IUriRuntimeClass> uri;
        if (SUCCEEDED(hr)) hr = factory->CreateUri(Wrappers::HStringReference(text.c_str()).Get(), &uri);
        std::wstring path;
        if (SUCCEEDED(hr)) hr = ResolveSourceResourceUri(uri.Get(), path);
        ComPtr<IStream> stream;
        if (SUCCEEDED(hr)) hr = OpenPackageResource(CurrentPackage(), path, &stream);
        if (hr == S_FALSE) hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        if (FAILED(hr)) return hr;
        ComPtr<IWICImagingFactory> wic;
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
        ComPtr<IWICBitmapDecoder> decoder;
        if (SUCCEEDED(hr))
            hr = wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
        GUID format{};
        UINT frames = 0;
        if (SUCCEEDED(hr)) hr = decoder->GetContainerFormat(&format);
        if (SUCCEEDED(hr)) hr = decoder->GetFrameCount(&frames);
        if (FAILED(hr)) return hr;
        if (format != GUID_ContainerFormatPng || frames != 1)
            return Unsupported(L"Image.Source container or frame count");
        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, &frame);
        UINT width = 0, height = 0;
        if (SUCCEEDED(hr)) hr = frame->GetSize(&width, &height);
        if (FAILED(hr)) return hr;
        if (!width || !height || width > INT_MAX || height > INT_MAX) return E_INVALIDARG;
        if (static_cast<UINT64>(width) * height > std::numeric_limits<UINT>::max() / 4) return E_OUTOFMEMORY;
        ComPtr<IWICFormatConverter> converted;
        hr = wic->CreateFormatConverter(&converted);
        if (SUCCEEDED(hr))
            hr = converted->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                       WICBitmapPaletteTypeCustom);
        ComPtr<IWICBitmap> bitmap;
        if (SUCCEEDED(hr)) hr = wic->CreateBitmapFromSource(converted.Get(), WICBitmapCacheOnLoad, &bitmap);
        if (FAILED(hr)) return hr;
        auto result = Make<PackageBitmap>(bitmap.Get(), width, height);
        Log("TRACE", "source image decoded %ls %ux%u PBGRA", path.c_str(), width, height);
        return result ? result.CopyTo(value) : E_OUTOFMEMORY;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
}
const ShimClass imageClass{Image::Name, MakeImageFactory, ShimPolicy::FillMissing};
}
