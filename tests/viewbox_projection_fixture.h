#include <windows.ui.xaml.media.h>
#include <thread>
namespace viewbox_fixture {
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
using namespace ABI::Windows::UI::Xaml::Media;
using namespace ABI::Windows::Foundation::Collections;
ComPtr<IUIElement> retained;
HRESULT Check(int stage) {
    if (stage) {
        ComPtr<IFrameworkElement> element;
        Wrappers::HString name;
        auto ok = retained && SUCCEEDED(retained.As(&element)) && SUCCEEDED(element->get_Name(name.GetAddressOf())) &&
                  !wcscmp(name.GetRawBuffer(nullptr), L"inside");
        retained.Reset();
        return ok ? S_OK : E_FAIL;
    }
    ComPtr<IUIElement> root, content, first, second, third;
    ComPtr<IUserControl> page;
    ComPtr<IPanel> panel;
    ComPtr<IVector<ABI::Windows::UI::Xaml::UIElement*>> children;
    auto hr = WindowContent(&root);
    if (FAILED(hr) || FAILED(root.As(&page)) || FAILED(page->get_Content(&content)) || FAILED(content.As(&panel)) ||
        FAILED(panel->get_Children(&children)) || FAILED(children->GetAt(0, &first)) ||
        FAILED(children->GetAt(1, &second)) || FAILED(children->GetAt(2, &third)))
        return E_FAIL;
    ComPtr<IViewbox> box, explicitBox, empty;
    if (FAILED(first.As(&box)) || FAILED(second.As(&explicitBox)) || FAILED(third.As(&empty))) return E_FAIL;
    ComPtr<IUIElement> child, explicitChild, none;
    if (FAILED(box->get_Child(&child)) || !child || FAILED(explicitBox->get_Child(&explicitChild)) || !explicitChild ||
        FAILED(empty->get_Child(&none)) || none)
        return E_FAIL;
    ComPtr<IGrid> grid;
    ComPtr<ITextBlock> text;
    Wrappers::HString caption;
    if (FAILED(child.As(&grid)) || FAILED(explicitChild.As(&text)) || FAILED(text->get_Text(caption.GetAddressOf())) ||
        wcscmp(caption.GetRawBuffer(nullptr), L"caption"))
        return E_FAIL;
    retained = child;
    Stretch stretch{};
    StretchDirection direction{};
    if (FAILED(box->get_Stretch(&stretch)) || stretch != Stretch_None ||
        FAILED(box->get_StretchDirection(&direction)) || direction != StretchDirection_DownOnly ||
        FAILED(empty->get_Stretch(&stretch)) || stretch != Stretch_Uniform ||
        FAILED(empty->get_StretchDirection(&direction)) || direction != StretchDirection_Both)
        return E_FAIL;
    if (FAILED(box->put_Stretch(Stretch_UniformToFill)) || FAILED(box->put_StretchDirection(StretchDirection_UpOnly)) ||
        box->put_Stretch(static_cast<Stretch>(99)) != E_INVALIDARG ||
        box->put_StretchDirection(static_cast<StretchDirection>(99)) != E_INVALIDARG ||
        FAILED(box->get_Stretch(&stretch)) || stretch != Stretch_UniformToFill ||
        FAILED(box->get_StretchDirection(&direction)) || direction != StretchDirection_UpOnly)
        return E_FAIL;
    if (box->put_Child(child.Get()) != E_NOTIMPL || box->put_Child(nullptr) != E_NOTIMPL) return E_FAIL;
    ComPtr<IFrameworkElement> element;
    double width = 0;
    HorizontalAlignment horizontal{};
    Visibility visibility{};
    if (FAILED(first.As(&element)) || FAILED(element->get_Width(&width)) || width != 320 ||
        FAILED(element->get_HorizontalAlignment(&horizontal)) || horizontal != HorizontalAlignment_Left ||
        FAILED(first->get_Visibility(&visibility)) || visibility != Visibility_Collapsed)
        return E_FAIL;
    HRESULT wrong = S_OK;
    std::thread worker([&] {
        ComPtr<IUIElement> value;
        wrong = box->get_Child(&value);
    });
    worker.join();
    return wrong == RPC_E_WRONG_THREAD ? S_OK : E_FAIL;
}
}
extern "C" HRESULT __cdecl ValidateViewboxData(INT32 stage) { return viewbox_fixture::Check(stage); }
