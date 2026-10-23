#pragma once
#include <windows.h>
#undef GetCurrentTime
#include <windows.ui.xaml.input.h>
#include <functional>
namespace appx {
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
// The mouse as a PointerPoint at a client position, for CoreWindow events.
HRESULT MakeMousePoint(POINT position, WPARAM buttons, UINT message, ABI::Windows::UI::Input::IPointerPoint** value);
}
