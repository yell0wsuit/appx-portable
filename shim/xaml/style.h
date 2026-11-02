#pragma once
#include <windows.ui.xaml.controls.h>
#include <wrl/client.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace appx {
// Connects a template's ContentPresenters to their templated parent. The parent
// clears it when the template is replaced or the parent is destroyed.
struct TemplateLink {
    ABI::Windows::UI::Xaml::Controls::IContentControl* parent = nullptr;
};
// Applies one source Setter to a styled element.
using StyleSetter = std::function<HRESULT(IInspectable* target)>;
// Builds a fresh copy of a source template's content; null root for an empty template.
using TemplateFactory = std::function<HRESULT(const std::shared_ptr<TemplateLink>& link,
                                              Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::IUIElement>& root)>;
// Implemented by controls that can apply a template. S_FALSE: no template applied.
MIDL_INTERFACE("6428EDFC-008F-4BB7-A34E-19226A205F1B") ITemplatedElement : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE TemplateRoot(ABI::Windows::UI::Xaml::IUIElement * *root) = 0;
};
// targetType is a full runtime class name such as Windows.UI.Xaml.Controls.Frame.
HRESULT MakeSourceStyle(const std::wstring& targetType, std::vector<StyleSetter> setters,
                        ABI::Windows::UI::Xaml::IStyle** value);
HRESULT MakeSourceControlTemplate(const std::wstring& targetType, TemplateFactory factory,
                                  ABI::Windows::UI::Xaml::Controls::IControlTemplate** value);
HRESULT ApplySourceStyle(ABI::Windows::UI::Xaml::IStyle* style, IInspectable* target, const wchar_t* targetType);
HRESULT InstantiateSourceTemplate(ABI::Windows::UI::Xaml::Controls::IControlTemplate* value, const wchar_t* targetType,
                                  const std::shared_ptr<TemplateLink>& link,
                                  Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::IUIElement>& root);
HRESULT MakeContentPresenter(IInspectable** value);
HRESULT BindContentPresenter(IInspectable* presenter, const std::shared_ptr<TemplateLink>& link);
}
