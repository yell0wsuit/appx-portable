#pragma once
#include <windows.ui.xaml.media.h>
namespace appx {
HRESULT MakeFontFamily(HSTRING source, ABI::Windows::UI::Xaml::Media::IFontFamily** value);
}
