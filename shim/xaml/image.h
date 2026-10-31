#pragma once
#include <windows.ui.xaml.media.h>
#include <wincodec.h>
#include <string>
namespace appx {
// Renderer access to package pixels; callers receive their own counted reference.
MIDL_INTERFACE("1957d4ea-213f-4ba8-92ac-a6ed888a9c09") IImagePixels : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetBitmap(IWICBitmapSource * *value) = 0;
};
HRESULT MakePackageImageSource(const std::wstring& uri, ABI::Windows::UI::Xaml::Media::IImageSource** value);
}
