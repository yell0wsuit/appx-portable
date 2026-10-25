#pragma once
#include <windows.ui.core.h>
namespace appx {
HRESULT MakeIndependentInputSource(ABI::Windows::UI::Core::CoreInputDeviceTypes devices,
                                   ABI::Windows::UI::Core::ICoreInputSourceBase** value);
void IndependentInputHostMessage(UINT message, WPARAM wparam, LPARAM lparam);
}
