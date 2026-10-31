#pragma once
#include <windows.ui.xaml.h>
namespace appx {
HRESULT SetSourceViewboxChild(IInspectable* viewbox, ABI::Windows::UI::Xaml::IUIElement* child);
}
