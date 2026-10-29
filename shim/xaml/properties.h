#pragma once
#include <windows.h>
#undef GetCurrentTime
#include <windows.ui.xaml.h>
#include <wrl.h>
#include <map>
namespace appx {
// Per-instance local values. A missing entry is different from an explicit null.
// Values and property identities are retained independently of their callers.
class PropertyValues {
    struct Entry {
        Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::IDependencyProperty> property;
        Microsoft::WRL::ComPtr<IInspectable> value;
    };
    std::map<IUnknown*, Entry> local_;
    DWORD thread_ = GetCurrentThreadId();

public:
    HRESULT Get(ABI::Windows::UI::Xaml::IDependencyProperty* property, IInspectable** value, bool local = false);
    HRESULT Set(ABI::Windows::UI::Xaml::IDependencyObject* owner, ABI::Windows::UI::Xaml::IDependencyProperty* property,
                IInspectable* value, bool clear = false);
};
}
