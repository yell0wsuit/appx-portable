#include <windows.ui.xaml.media.h>
#include <limits>
#include <thread>
namespace brush_fixture {
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Media;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using Vector = IVector<GradientStop*>;
using View = IVectorView<GradientStop*>;
using Iterable = IIterable<GradientStop*>;
using Iterator = IIterator<GradientStop*>;
ComPtr<IBrush> retained;
ComPtr<View> retainedView;
HRESULT Standalone(Vector* source) {
    ComPtr<IInspectable> object;
    if (FAILED(animation_fixture::Activate(L"Windows.UI.Xaml.Media.GradientStop", &object))) return E_FAIL;
    ComPtr<IGradientStop> stop;
    ABI::Windows::UI::Color color{};
    double offset = 99;
    if (FAILED(object.As(&stop)) || FAILED(stop->get_Color(&color)) || color.A || color.R || color.G || color.B ||
        FAILED(stop->get_Offset(&offset)) || offset != 0 || FAILED(stop->put_Offset(2)) ||
        stop->put_Offset(std::numeric_limits<double>::infinity()) != E_INVALIDARG ||
        FAILED(stop->get_Offset(&offset)) || offset != 2)
        return E_FAIL;
    if (FAILED(animation_fixture::Activate(L"Windows.UI.Xaml.Media.LinearGradientBrush", &object))) return E_FAIL;
    ComPtr<IBrush> brush;
    ComPtr<ILinearGradientBrush> linear;
    ComPtr<IGradientBrush> gradient;
    Point point{};
    double opacity = 99;
    GradientSpreadMethod spread{};
    BrushMappingMode mapping{};
    ColorInterpolationMode interpolation{};
    if (FAILED(object.As(&brush)) || FAILED(object.As(&linear)) || FAILED(object.As(&gradient)) ||
        FAILED(linear->get_StartPoint(&point)) || point.X || point.Y || FAILED(linear->get_EndPoint(&point)) ||
        point.X != 1 || point.Y != 1 || FAILED(brush->get_Opacity(&opacity)) || opacity != 1 ||
        FAILED(gradient->get_SpreadMethod(&spread)) || spread != GradientSpreadMethod_Pad ||
        FAILED(gradient->get_MappingMode(&mapping)) || mapping != BrushMappingMode_RelativeToBoundingBox ||
        FAILED(gradient->get_ColorInterpolationMode(&interpolation)) ||
        interpolation != ColorInterpolationMode_SRgbLinearInterpolation)
        return E_FAIL;
    ComPtr<Vector> stops;
    UINT32 count = 99;
    if (FAILED(gradient->get_GradientStops(&stops)) || FAILED(stops->get_Size(&count)) || count ||
        FAILED(gradient->put_GradientStops(source)) || FAILED(gradient->get_GradientStops(&stops)) ||
        stops.Get() != source || FAILED(brush->put_Opacity(.25)) || brush->put_Opacity(-1) != E_INVALIDARG ||
        FAILED(brush->get_Opacity(&opacity)) || opacity != .25 ||
        linear->put_StartPoint({std::numeric_limits<float>::infinity(), 0}) != E_INVALIDARG ||
        FAILED(gradient->put_SpreadMethod(GradientSpreadMethod_Reflect)) ||
        FAILED(gradient->get_SpreadMethod(&spread)) || spread != GradientSpreadMethod_Reflect ||
        FAILED(gradient->put_MappingMode(BrushMappingMode_Absolute)) || FAILED(gradient->get_MappingMode(&mapping)) ||
        mapping != BrushMappingMode_Absolute ||
        FAILED(gradient->put_ColorInterpolationMode(ColorInterpolationMode_ScRgbLinearInterpolation)) ||
        FAILED(gradient->get_ColorInterpolationMode(&interpolation)) ||
        interpolation != ColorInterpolationMode_ScRgbLinearInterpolation)
        return E_FAIL;
    return S_OK;
}
HRESULT Check(int stage) {
    ComPtr<IUIElement> root, content;
    ComPtr<ABI::Windows::UI::Xaml::Controls::IUserControl> page;
    ComPtr<ABI::Windows::UI::Xaml::Controls::IPanel> panel;
    ComPtr<IBrush> brush;
    auto hr = WindowContent(&root);
    if (FAILED(hr) || FAILED(root.As(&page)) || FAILED(page->get_Content(&content)) || FAILED(content.As(&panel)) ||
        FAILED(panel->get_Background(&brush)) || !brush)
        return E_FAIL;
    ComPtr<ILinearGradientBrush> linear;
    ComPtr<IGradientBrush> gradient;
    ComPtr<Vector> stops;
    Point point{};
    double opacity = 0;
    if (FAILED(brush.As(&linear)) || FAILED(brush.As(&gradient)) || FAILED(gradient->get_GradientStops(&stops)))
        return E_FAIL;
    UINT32 count = 99;
    if (FAILED(stops->get_Size(&count)) || count != (stage ? 0 : 2)) return E_FAIL;
    if (stage) {
        if (!retained || FAILED(retained.As(&gradient)) || FAILED(gradient->get_GradientStops(&stops)) ||
            FAILED(stops->get_Size(&count)) || count != 2)
            return E_FAIL;
        retained.Reset();
        gradient.Reset();
        stops.Reset();
        ComPtr<IGradientStop> saved;
        if (!retainedView || FAILED(retainedView->GetAt(1, &saved))) return E_FAIL;
        ABI::Windows::UI::Color color{};
        if (FAILED(saved->get_Color(&color)) || color.A != 0x80 || color.B != 0x56) return E_FAIL;
        retainedView.Reset();
        return S_OK;
    }
    ComPtr<IFrameworkElement> rootElement;
    if (FAILED(root.As(&rootElement))) return E_FAIL;
    for (const auto& expected : {std::make_pair(L"shortRGB", ABI::Windows::UI::Color{255, 0xAA, 0xBB, 0xCC}),
                                 std::make_pair(L"shortARGB", ABI::Windows::UI::Color{0x11, 0x22, 0x33, 0x44})}) {
        ComPtr<IInspectable> named;
        ComPtr<IGradientStop> namedStop;
        ABI::Windows::UI::Color color{};
        if (FAILED(rootElement->FindName(Wrappers::HStringReference(expected.first).Get(), &named)) || !named ||
            FAILED(named.As(&namedStop)) || FAILED(namedStop->get_Color(&color)) || color.A != expected.second.A ||
            color.R != expected.second.R || color.G != expected.second.G || color.B != expected.second.B)
            return E_FAIL;
    }
    ComPtr<IInspectable> namedBrush;
    ComPtr<IGradientBrush> resource;
    ComPtr<Vector> resourceStops;
    if (FAILED(rootElement->FindName(Wrappers::HStringReference(L"resourceBrush").Get(), &namedBrush)) || !namedBrush ||
        FAILED(namedBrush.As(&resource)) || FAILED(resource->get_GradientStops(&resourceStops)) ||
        FAILED(resourceStops->get_Size(&count)) || count != 1)
        return E_FAIL;
    retained = brush;
    if (FAILED(linear->get_StartPoint(&point)) || point.X != .5f || point.Y != 0 ||
        FAILED(linear->get_EndPoint(&point)) || point.X != .5f || point.Y != 1 ||
        FAILED(brush->get_Opacity(&opacity)) || opacity != .75)
        return E_FAIL;
    ComPtr<IGradientStop> stop;
    ABI::Windows::UI::Color color{};
    double offset = 0;
    if (FAILED(stops->GetAt(0, &stop)) || FAILED(stop->get_Color(&color)) || color.A != 255 || color.R != 0xED ||
        color.G != 0x1C || color.B != 0x24 || FAILED(stop->get_Offset(&offset)) || offset != 1)
        return E_FAIL;
    if (FAILED(stops->GetAt(1, &stop)) || FAILED(stop->get_Color(&color)) || color.A != 0x80 || color.R != 0x12 ||
        color.G != 0x34 || color.B != 0x56 || FAILED(stop->get_Offset(&offset)) || offset != -.25)
        return E_FAIL;
    ComPtr<View> view;
    ComPtr<Iterable> iterable;
    ComPtr<Iterator> iterator;
    if (FAILED(stops->GetView(&view)) || FAILED(stops.As(&iterable)) || FAILED(iterable->First(&iterator)))
        return E_FAIL;
    retainedView = view;
    if (FAILED(Standalone(stops.Get()))) return E_FAIL;
    IGradientStop* values[3]{};
    UINT32 actual = 99;
    if (FAILED(iterator->GetMany(3, values, &actual)) || actual != 2) return E_FAIL;
    for (auto* value : values)
        if (value) value->Release();
    if (stops->GetAt(2, &stop) != E_BOUNDS || stops->Clear() != E_NOTIMPL) return E_FAIL;
    HRESULT wrong = S_OK;
    std::thread worker([&] { wrong = brush->get_Opacity(&opacity); });
    worker.join();
    return wrong == RPC_E_WRONG_THREAD ? S_OK : E_FAIL;
}
}
extern "C" HRESULT __cdecl ValidateBrushData(INT32 stage) { return brush_fixture::Check(stage); }
