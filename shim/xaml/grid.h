#pragma once
#include <windows.ui.xaml.controls.h>
#include <wrl.h>
#include <memory>
#include <vector>
namespace appx {
using VisualChildren = std::vector<Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::IUIElement>>;
using GridChildren = ABI::Windows::Foundation::Collections::IVector<ABI::Windows::UI::Xaml::UIElement*>;
HRESULT MakeGridChildren(GridChildren** value);
HRESULT SetGridChildren(IInspectable* grid, const std::shared_ptr<const VisualChildren>& children);
}
