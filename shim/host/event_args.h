#pragma once
#include <windows.ui.core.h>
#include <wrl.h>
namespace appx {
using namespace ABI::Windows::UI::Core;
class ActivationArgs final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>,
                                          IWindowActivatedEventArgs, ICoreWindowEventArgs> {
    InspectableClass(L"Windows.UI.Core.WindowActivatedEventArgs", BaseTrust);
    CoreWindowActivationState state_;
    boolean handled_ = false;

public:
    explicit ActivationArgs(CoreWindowActivationState state) : state_(state) {}
    HRESULT STDMETHODCALLTYPE get_WindowActivationState(CoreWindowActivationState* value) override {
        if (!value) return E_POINTER;
        *value = state_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = handled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean value) override {
        handled_ = value;
        return S_OK;
    }
};
class WindowSizeArgs final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>,
                                          IWindowSizeChangedEventArgs, ICoreWindowEventArgs> {
    InspectableClass(L"Windows.UI.Core.WindowSizeChangedEventArgs", BaseTrust);
    ABI::Windows::Foundation::Size size_;
    boolean handled_ = false;

public:
    explicit WindowSizeArgs(ABI::Windows::Foundation::Size size) : size_(size) {}
    HRESULT STDMETHODCALLTYPE get_Size(ABI::Windows::Foundation::Size* value) override {
        if (!value) return E_POINTER;
        *value = size_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = handled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean value) override {
        handled_ = value;
        return S_OK;
    }
};
class VisibilityArgs final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>,
                                          IVisibilityChangedEventArgs, ICoreWindowEventArgs> {
    InspectableClass(L"Windows.UI.Core.VisibilityChangedEventArgs", BaseTrust);
    boolean visible_, handled_ = false;

public:
    explicit VisibilityArgs(boolean visible) : visible_(visible) {}
    HRESULT STDMETHODCALLTYPE get_Visible(boolean* value) override {
        if (!value) return E_POINTER;
        *value = visible_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = handled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean value) override {
        handled_ = value;
        return S_OK;
    }
};
// CoreWindow pointer events.
class CorePointerArgs final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>, IPointerEventArgs,
                                          ICoreWindowEventArgs> {
    InspectableClass(L"Windows.UI.Core.PointerEventArgs", BaseTrust);
    Microsoft::WRL::ComPtr<ABI::Windows::UI::Input::IPointerPoint> point_;
    ABI::Windows::System::VirtualKeyModifiers modifiers_;
    boolean handled_ = false;

public:
    CorePointerArgs(ABI::Windows::UI::Input::IPointerPoint* point, ABI::Windows::System::VirtualKeyModifiers modifiers)
        : point_(point), modifiers_(modifiers) {}
    HRESULT STDMETHODCALLTYPE get_CurrentPoint(ABI::Windows::UI::Input::IPointerPoint** value) override {
        if (!value) return E_POINTER;
        return point_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_KeyModifiers(ABI::Windows::System::VirtualKeyModifiers* value) override {
        if (!value) return E_POINTER;
        *value = modifiers_;
        return S_OK;
    }
    // Win32 delivers no intermediate points; the current one is the history.
    HRESULT STDMETHODCALLTYPE GetIntermediatePoints(__FIVector_1_Windows__CUI__CInput__CPointerPoint** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = handled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean value) override {
        handled_ = value;
        return S_OK;
    }
};
// The key status Win32 keyboard messages carry in lParam.
inline CorePhysicalKeyStatus KeyStatus(LPARAM lparam, bool released) {
    CorePhysicalKeyStatus status{};
    status.RepeatCount = static_cast<UINT32>(lparam & 0xffff);
    status.ScanCode = static_cast<UINT32>((lparam >> 16) & 0xff);
    status.IsExtendedKey = (lparam & (1 << 24)) != 0;
    status.IsMenuKeyDown = (lparam & (1 << 29)) != 0;
    status.WasKeyDown = (lparam & (1 << 30)) != 0;
    status.IsKeyReleased = released;
    return status;
}
// CoreWindow.KeyDown and KeyUp.
class KeyArgs final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>, IKeyEventArgs,
                                          ICoreWindowEventArgs> {
    InspectableClass(L"Windows.UI.Core.KeyEventArgs", BaseTrust);
    ABI::Windows::System::VirtualKey key_;
    CorePhysicalKeyStatus status_;
    boolean handled_ = false;

public:
    KeyArgs(ABI::Windows::System::VirtualKey key, CorePhysicalKeyStatus status) : key_(key), status_(status) {}
    HRESULT STDMETHODCALLTYPE get_VirtualKey(ABI::Windows::System::VirtualKey* value) override {
        if (!value) return E_POINTER;
        *value = key_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_KeyStatus(CorePhysicalKeyStatus* value) override {
        if (!value) return E_POINTER;
        *value = status_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = handled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean value) override {
        handled_ = value;
        return S_OK;
    }
};
// CoreWindow.CharacterReceived.
class CharacterArgs final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>,
                                          ICharacterReceivedEventArgs, ICoreWindowEventArgs> {
    InspectableClass(L"Windows.UI.Core.CharacterReceivedEventArgs", BaseTrust);
    UINT32 code_;
    CorePhysicalKeyStatus status_;
    boolean handled_ = false;

public:
    CharacterArgs(UINT32 code, CorePhysicalKeyStatus status) : code_(code), status_(status) {}
    HRESULT STDMETHODCALLTYPE get_KeyCode(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = code_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_KeyStatus(CorePhysicalKeyStatus* value) override {
        if (!value) return E_POINTER;
        *value = status_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = handled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean value) override {
        handled_ = value;
        return S_OK;
    }
};
}
