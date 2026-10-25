#pragma once
#include <windows.ui.core.h>
namespace appx {
HRESULT MakeDispatcher(ABI::Windows::UI::Core::ICoreDispatcher** value);
HRESULT MakeThreadDispatcher(ABI::Windows::UI::Core::ICoreDispatcher** value);
}
