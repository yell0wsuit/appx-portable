#pragma once
#include <windows.h>
#undef GetCurrentTime
#include <windows.ui.xaml.media.animation.h>
#include <memory>
#include <string>
namespace appx {
struct SourceNameScope;
bool AnimationType(const std::wstring& type);
HRESULT ParseAnimationDuration(const std::wstring& text, ABI::Windows::UI::Xaml::Duration* value);
HRESULT ConfigureAnimation(IInspectable* object, const ABI::Windows::UI::Xaml::Duration* duration,
                           const std::wstring* targetName, const std::wstring* themeTargetName,
                           const std::wstring* targetProperty);
HRESULT BindAnimationScope(IInspectable* object, const std::shared_ptr<SourceNameScope>& scope);
HRESULT AppendAnimation(IInspectable* storyboard, IInspectable* child);
}
