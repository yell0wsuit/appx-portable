// Windows.UI.Xaml.Data.PropertyChangedEventArgs for Wine, which has no XAML:
// C++/CX view models create one each time they raise PropertyChanged.
#include "generated/xaml_data.h"
#include "com/composable.h"
#include "com/factory.h"
#include "registry.h"
#include <wrl/wrappers/corewrappers.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml::Data;
namespace appx {
namespace {
class PropertyChangedEventArgs final : public Composable<PropertyChangedEventArgs, DefaultIPropertyChangedEventArgs> {
    Wrappers::HString name_;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Data.PropertyChangedEventArgs";
    explicit PropertyChangedEventArgs(IInspectable* outer) : Composable(outer) {}
    HRESULT SetName(HSTRING name) { return name_.Set(name); }
    HRESULT STDMETHODCALLTYPE get_PropertyName(HSTRING* value) override {
        if (!value) return E_POINTER;
        return name_.CopyTo(value);
    }
};
class PropertyChangedEventArgsFactory final : public Factory<DefaultIPropertyChangedEventArgsFactory> {
public:
    PropertyChangedEventArgsFactory() : Factory(PropertyChangedEventArgs::Name) {}
    HRESULT STDMETHODCALLTYPE CreateInstance(HSTRING name, IInspectable* outer, IInspectable** inner,
                                             IPropertyChangedEventArgs** value) override {
        auto hr = Compose<PropertyChangedEventArgs>(outer, inner, value);
        if (SUCCEEDED(hr)) hr = static_cast<PropertyChangedEventArgs*>(*value)->SetName(name);
        if (FAILED(hr) && value && *value) {
            (*value)->Release();
            *value = nullptr;
            if (inner && *inner) {
                (*inner)->Release();
                *inner = nullptr;
            }
        }
        return hr;
    }
};
HRESULT MakePropertyChangedEventArgsFactory(IActivationFactory** value) {
    auto factory = Make<PropertyChangedEventArgsFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
const ShimClass propertyChangedEventArgsClass{PropertyChangedEventArgs::Name, MakePropertyChangedEventArgsFactory,
                                              ShimPolicy::FillMissing};
}
