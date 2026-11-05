#pragma once
#include <inspectable.h>
#include <activation.h>
namespace appx {
HRESULT MakeWin10XamlElement(const wchar_t* tag, IInspectable** value);
HRESULT MakeWin10GridFactory(IActivationFactory** value);
MIDL_INTERFACE("C0A83443-02DC-4725-8C9F-86F90B4C5B55") IGridPlacement : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetPlacement(UINT index, INT32 * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPlacement(UINT index, INT32 value) = 0;
};
MIDL_INTERFACE("CEDA4BA5-16C5-4C62-92BB-8604AC503453") ICanvasPlacement : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetCanvasPosition(UINT index, DOUBLE * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCanvasPosition(UINT index, DOUBLE value) = 0;
};
}
