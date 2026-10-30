#pragma once
#include <windows.ui.xaml.h>
namespace appx {
HRESULT MakeResourceDictionary(ABI::Windows::UI::Xaml::IResourceDictionary** value);
HRESULT AddSourceResource(ABI::Windows::UI::Xaml::IResourceDictionary* dictionary, HSTRING key, IInspectable* value);
}
