#pragma once
// Read and write the actual inherited SDK property, independently of generated C#.
static HRESULT VisibilityElement(INT32 child, ABI::Windows::UI::Xaml::IUIElement** value) {
    using namespace ABI::Windows::UI::Xaml;
    ComPtr<IUIElement> root;
    auto hr = WindowContent(&root);
    if (FAILED(hr)) return hr;
    if (!child) return root.CopyTo(value);
    ComPtr<Controls::IContentControl> frame;
    hr = root.As(&frame);
    ComPtr<IInspectable> content;
    if (SUCCEEDED(hr)) hr = frame->get_Content(&content);
    return FAILED(hr) ? hr : content ? content->QueryInterface(IID_PPV_ARGS(value)) : E_UNEXPECTED;
}
extern "C" HRESULT __cdecl CheckElementVisibility(INT32 child, INT32 expected) {
    ComPtr<ABI::Windows::UI::Xaml::IUIElement> element;
    auto hr = VisibilityElement(child, &element);
    ABI::Windows::UI::Xaml::Visibility value{};
    if (SUCCEEDED(hr)) hr = element->get_Visibility(&value);
    return FAILED(hr) ? hr : static_cast<INT32>(value) == expected ? S_OK : E_FAIL;
}
extern "C" HRESULT __cdecl SetElementVisibility(INT32 child, INT32 value) {
    ComPtr<ABI::Windows::UI::Xaml::IUIElement> element;
    auto hr = VisibilityElement(child, &element);
    return FAILED(hr) ? hr : element->put_Visibility(static_cast<ABI::Windows::UI::Xaml::Visibility>(value));
}
