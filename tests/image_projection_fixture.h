#include <windows.ui.xaml.media.imaging.h>
#include "../shim/xaml/image.h"
#include <thread>
#include <limits>
namespace image_fixture {
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
using namespace ABI::Windows::UI::Xaml::Media;
using namespace ABI::Windows::UI::Xaml::Media::Imaging;
using namespace ABI::Windows::Foundation::Collections;
ComPtr<IImageSource> retained;
class Opened final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IRoutedEventHandler> {
public:
    HRESULT STDMETHODCALLTYPE Invoke(IInspectable*, IRoutedEventArgs*) override { return E_UNEXPECTED; }
};
HRESULT Pixels(IImageSource* source) {
    ComPtr<IBitmapSource> dimensions;
    ComPtr<appx::IImagePixels> pixels;
    if (!source || FAILED(source->QueryInterface(IID_PPV_ARGS(&dimensions))) ||
        FAILED(source->QueryInterface(IID_PPV_ARGS(&pixels))))
        return E_FAIL;
    INT32 width = 0, height = 0;
    if (FAILED(dimensions->get_PixelWidth(&width)) || FAILED(dimensions->get_PixelHeight(&height)) || width != 2 ||
        height != 1)
        return E_FAIL;
    ComPtr<IWICBitmapSource> bitmap;
    BYTE actual[8]{};
    const BYTE expected[8] = {0, 0, 255, 255, 0, 128, 0, 128};
    WICPixelFormatGUID format{};
    if (FAILED(pixels->GetBitmap(&bitmap)) || !bitmap || FAILED(bitmap->GetPixelFormat(&format)) ||
        format != GUID_WICPixelFormat32bppPBGRA || FAILED(bitmap->CopyPixels(nullptr, 8, 8, actual)) ||
        memcmp(actual, expected, 8))
        return E_FAIL;
    return S_OK;
}
HRESULT Check(int stage) {
    if (stage) {
        auto hr = Pixels(retained.Get());
        retained.Reset();
        return hr;
    }
    ComPtr<IUIElement> root, content;
    ComPtr<IUserControl> page;
    ComPtr<IPanel> panel;
    ComPtr<IVector<ABI::Windows::UI::Xaml::UIElement*>> children;
    ComPtr<IUIElement> first, second;
    ComPtr<IImage> image, empty;
    auto hr = WindowContent(&root);
    if (FAILED(hr) || FAILED(root.As(&page)) || FAILED(page->get_Content(&content)) || FAILED(content.As(&panel)) ||
        FAILED(panel->get_Children(&children)) || FAILED(children->GetAt(0, &first)) ||
        FAILED(children->GetAt(1, &second)) || FAILED(first.As(&image)) || FAILED(second.As(&empty)))
        return E_FAIL;
    ComPtr<IImageSource> source;
    if (FAILED(image->get_Source(&source)) || FAILED(Pixels(source.Get()))) return E_FAIL;
    retained = source;
    Stretch stretch{};
    if (FAILED(image->get_Stretch(&stretch)) || stretch != Stretch_Uniform || FAILED(empty->get_Stretch(&stretch)) ||
        stretch != Stretch_None)
        return E_FAIL;
    ComPtr<IImageSource> none;
    if (FAILED(empty->get_Source(&none)) || none) return E_FAIL;
    ComPtr<IFrameworkElement> element;
    VerticalAlignment vertical{};
    HorizontalAlignment horizontal{};
    Thickness margin{};
    double width = 0, height = 0;
    if (FAILED(first.As(&element)) || FAILED(element->get_VerticalAlignment(&vertical)) ||
        vertical != VerticalAlignment_Center || FAILED(element->get_HorizontalAlignment(&horizontal)) ||
        horizontal != HorizontalAlignment_Right || FAILED(element->get_Margin(&margin)) || margin.Left != 1 ||
        margin.Top != 2 || margin.Right != 3 || margin.Bottom != 4 || FAILED(element->get_Width(&width)) ||
        width != 620 || FAILED(element->get_Height(&height)) || height != 300)
        return E_FAIL;
    if (FAILED(second.As(&element)) || FAILED(element->get_Margin(&margin)) || margin.Left != 3 || margin.Top != 5 ||
        margin.Right != 3 || margin.Bottom != 5)
        return E_FAIL;
    ComPtr<IUIElement> third;
    ComPtr<IFrameworkElement> thirdElement;
    if (FAILED(children->GetAt(2, &third)) || FAILED(third.As(&thirdElement)) ||
        FAILED(thirdElement->get_Margin(&margin)) || margin.Left != 3 || margin.Top != 3 || margin.Right != 3 ||
        margin.Bottom != 3)
        return E_FAIL;
    if (FAILED(thirdElement->get_HorizontalAlignment(&horizontal)) || horizontal != HorizontalAlignment_Stretch ||
        FAILED(thirdElement->get_VerticalAlignment(&vertical)) || vertical != VerticalAlignment_Stretch ||
        thirdElement->put_Margin({std::numeric_limits<double>::infinity(), 0, 0, 0}) != E_INVALIDARG ||
        thirdElement->put_HorizontalAlignment(static_cast<HorizontalAlignment>(99)) != E_INVALIDARG ||
        FAILED(thirdElement->put_Margin({-1, 2, -3, 4})) || FAILED(thirdElement->get_Margin(&margin)) ||
        margin.Left != -1 || margin.Right != -3)
        return E_FAIL;
    if (image->put_Stretch(static_cast<Stretch>(99)) != E_INVALIDARG || FAILED(image->get_Stretch(&stretch)) ||
        stretch != Stretch_Uniform)
        return E_FAIL;
    if (FAILED(empty->put_Source(source.Get())) || FAILED(empty->get_Source(&none)) || none.Get() != source.Get() ||
        FAILED(empty->put_Source(nullptr)))
        return E_FAIL;
    EventRegistrationToken token{};
    auto opened = Make<Opened>();
    if (!opened || image->add_ImageOpened(opened.Get(), &token) != E_NOTIMPL) return E_FAIL;
    HRESULT wrong = S_OK, wrongPixels = S_OK;
    std::thread worker([&] {
        ComPtr<IImageSource> value;
        wrong = image->get_Source(&value);
        ComPtr<appx::IImagePixels> pixels;
        source.As(&pixels);
        ComPtr<IWICBitmapSource> bitmap;
        wrongPixels = pixels->GetBitmap(&bitmap);
    });
    worker.join();
    if (FAILED(image->put_Source(nullptr))) return E_FAIL;
    return wrong == RPC_E_WRONG_THREAD && wrongPixels == RPC_E_WRONG_THREAD ? S_OK : E_FAIL;
}
}
extern "C" HRESULT __cdecl ValidateImageData(INT32 stage) { return image_fixture::Check(stage); }
