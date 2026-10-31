#pragma once
#include <windows.foundation.h>
#include <string>
namespace appx {
HRESULT ResolveSourceResourceUri(ABI::Windows::Foundation::IUriRuntimeClass* uri, std::wstring& path);
HRESULT LoadSourceXaml(IInspectable* component, ABI::Windows::Foundation::IUriRuntimeClass* uri);
}
