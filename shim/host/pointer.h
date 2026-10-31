#pragma once
#include <windows.h>
#undef GetCurrentTime
#include <windows.ui.xaml.input.h>
#include <functional>
namespace appx {
// The desktop host selects a page as Window.Content; pointer events belong to
// the graphics panel inside that page, as they do in the XAML visual tree.
struct __declspec(uuid("a13bfb5a-f82f-4ffd-b1c3-b321debe31c0")) IPointerTargetProvider : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetPointerTarget(IInspectable** value) = 0;
};
// Borrowed UI-thread pointer; callers retaining it must AddRef.
IInspectable* CurrentPointerTarget() noexcept;
using PointerCallback = std::function<void(UINT, ABI::Windows::UI::Xaml::Input::IPointerRoutedEventArgs*)>;
void RegisterPointerTarget(IInspectable* target, PointerCallback callback);
void UnregisterPointerTarget(IInspectable* target) noexcept;
void SetPointerTarget(IInspectable* target);
void ClearPointerTarget(IInspectable* target);
HRESULT CaptureElementPointer(IInspectable* target, ABI::Windows::UI::Xaml::Input::IPointer* pointer, boolean* result);
HRESULT ReleaseElementPointer(IInspectable* target);
void PointerHostMessage(UINT message, WPARAM wparam, LPARAM lparam);
// Pressed, moved and released handlers on XAML elements. While there are none,
// a XAML game's mouse goes to CoreWindow events, where some Windows 8 games read it.
void CountElementPointerHandlers(long delta) noexcept;
bool ElementPointerHandlers() noexcept;
// The mouse as a PointerPoint at a client position, for CoreWindow events.
HRESULT MakeMousePoint(POINT position, WPARAM buttons, UINT message, ABI::Windows::UI::Input::IPointerPoint** value);
HRESULT MakePointerHistory(ABI::Windows::UI::Input::IPointerPoint* point,
                           __FIVector_1_Windows__CUI__CInput__CPointerPoint** value);
}
