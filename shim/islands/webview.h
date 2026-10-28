#pragma once
#include <inspectable.h>
#include <windows.ui.h>
namespace appx {
MIDL_INTERFACE("7299dbbf-5139-46e2-b145-a41038d4ce9d") IIslandWebView : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetBackgroundColor(ABI::Windows::UI::Color * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBackgroundColor(ABI::Windows::UI::Color value) = 0;
};
HRESULT CreateIslandWebView(IInspectable** value);
HRESULT HookIslandWebViewMetadata(IInspectable* application);
bool IsIslandWebViewWindow(HWND window);
bool RouteIslandWebViewInput(const MSG& message);
}
