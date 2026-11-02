#pragma once
#include <windows.foundation.h>
#include <string>
namespace appx {
MIDL_INTERFACE("A8437238-E252-4B30-9C0C-FC6A9D317F06") IApplicationSource : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CheckSourceThread() = 0;
    virtual HRESULT STDMETHODCALLTYPE CompleteSourceInitialization() = 0;
};
// S_FALSE means the package has no source at this path.
HRESULT LoadApplicationSourceXaml(IInspectable* component, const std::wstring& path);
HRESULT ResolveSourceResourceUri(ABI::Windows::Foundation::IUriRuntimeClass* uri, std::wstring& path);
HRESULT LoadSourceXaml(IInspectable* component, ABI::Windows::Foundation::IUriRuntimeClass* uri);
}
