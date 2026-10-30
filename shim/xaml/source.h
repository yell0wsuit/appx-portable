#pragma once
#include <windows.foundation.h>
namespace appx {
HRESULT LoadSourceXaml(IInspectable* component, ABI::Windows::Foundation::IUriRuntimeClass* uri);
}
