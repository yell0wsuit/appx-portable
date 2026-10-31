#pragma once
#include <windows.ui.xaml.media.h>
#include <wrl.h>
#include <string>
#include <vector>
namespace appx {
struct BrushSourceData {
    ABI::Windows::Foundation::Point start{0, 0}, end{1, 1};
    ABI::Windows::UI::Color color{};
    double offset = 0, opacity = 1;
};
bool BrushDataType(const std::wstring& type);
HRESULT ParseBrushAttribute(const std::wstring& type, const std::wstring& name, const std::wstring& text,
                            BrushSourceData& data);
HRESULT ConfigureBrushData(IInspectable* object, const std::wstring& type, const BrushSourceData& data);
HRESULT SetSourceGradientStops(IInspectable* brush,
                               std::vector<Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::Media::IGradientStop>> stops);
}
