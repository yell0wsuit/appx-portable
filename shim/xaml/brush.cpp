#include "brush.h"
#include "properties.h"
#include "generated/xaml.h"
#include "com/composable.h"
#include "com/factory.h"
#include "registry.h"
#include <algorithm>
#include <d2d1.h>
#include <cmath>
#include <locale>
#include <limits>
#include <memory>
#include <sstream>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Media;
namespace appx {
namespace {
using Vector = IVector<GradientStop*>;
using View = IVectorView<GradientStop*>;
using Iterable = IIterable<GradientStop*>;
using Iterator = IIterator<GradientStop*>;
using StopItems = std::vector<ComPtr<IGradientStop>>;
using Snapshot = std::shared_ptr<const StopItems>;
template <class F> HRESULT Guard(F&& action) {
    try {
        return action();
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
}
HRESULT Unsupported(const wchar_t* member) {
    Log("UNIMPL", "%ls hr=%08lx", member, E_NOTIMPL);
    return E_NOTIMPL;
}
HRESULT Copy(const StopItems& items, UINT32 start, UINT32 capacity, IGradientStop** values, UINT32* actual) {
    if (!actual) return E_POINTER;
    *actual = 0;
    if (capacity && !values) return E_POINTER;
    if (start > items.size()) return E_BOUNDS;
    auto count = static_cast<UINT32>(std::min<size_t>(capacity, items.size() - start));
    for (UINT32 i = 0; i < count; ++i) items[start + i].CopyTo(&values[i]);
    *actual = count;
    return S_OK;
}
class StopsIterator final : public RuntimeClass<RuntimeClassFlags<WinRt>, Iterator> {
    InspectableClass(L"AppxPortable.Xaml.StopsIterator", BaseTrust);
    Snapshot items_;
    UINT32 index_ = 0;
    const DWORD thread_ = GetCurrentThreadId();
    HRESULT CheckThread() const { return thread_ == GetCurrentThreadId() ? S_OK : RPC_E_WRONG_THREAD; }

public:
    explicit StopsIterator(Snapshot items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE get_Current(IGradientStop** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return index_ < items_->size() ? (*items_)[index_].CopyTo(value) : E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = index_ < items_->size();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (index_ < items_->size()) ++index_;
        *value = index_ < items_->size();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 capacity, IGradientStop** values, UINT32* actual) override {
        if (!actual) return E_POINTER;
        *actual = 0;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) hr = Copy(*items_, index_, capacity, values, actual);
        if (SUCCEEDED(hr)) index_ += *actual;
        return hr;
    }
};
template <class Primary, class... Interfaces>
class StopsReader : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Primary, Iterable, Interfaces...> {
protected:
    Snapshot items_;
    const DWORD thread_ = GetCurrentThreadId();
    explicit StopsReader(Snapshot items) : items_(std::move(items)) {}
    HRESULT CheckThread() const { return thread_ == GetCurrentThreadId() ? S_OK : RPC_E_WRONG_THREAD; }

public:
    HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, IGradientStop** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        return index < items_->size() ? (*items_)[index].CopyTo(value) : E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = static_cast<UINT32>(items_->size());
        return hr;
    }
    HRESULT STDMETHODCALLTYPE IndexOf(IGradientStop* value, UINT32* index, boolean* found) override {
        if (!index || !found) return E_POINTER;
        *index = 0;
        *found = false;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        ComPtr<IUnknown> identity;
        if (value) {
            hr = value->QueryInterface(IID_PPV_ARGS(&identity));
            if (FAILED(hr)) return hr;
        }
        for (UINT32 i = 0; i < items_->size(); ++i) {
            ComPtr<IUnknown> candidate;
            hr = (*items_)[i].As(&candidate);
            if (FAILED(hr)) return hr;
            if (identity == candidate) {
                *index = i;
                *found = true;
                break;
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 start, UINT32 capacity, IGradientStop** values, UINT32* actual) override {
        if (!actual) return E_POINTER;
        *actual = 0;
        auto hr = CheckThread();
        return FAILED(hr) ? hr : Copy(*items_, start, capacity, values, actual);
    }
    HRESULT STDMETHODCALLTYPE First(Iterator** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        auto result = Make<StopsIterator>(items_);
        return result ? result.CopyTo(value) : E_OUTOFMEMORY;
    }
};
class StopsView final : public StopsReader<View> {
    InspectableClass(L"AppxPortable.Xaml.StopsView", BaseTrust);

public:
    explicit StopsView(Snapshot items) : StopsReader(std::move(items)) {}
};
class StopsCollection final : public StopsReader<Vector> {
    InspectableClass(L"Windows.UI.Xaml.Media.GradientStopCollection", BaseTrust);
    HRESULT Missing(const wchar_t* name) {
        auto hr = CheckThread();
        return FAILED(hr) ? hr : Unsupported(name);
    }

public:
    explicit StopsCollection(Snapshot items) : StopsReader(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE GetView(View** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        auto view = Make<StopsView>(items_);
        return view ? view.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE SetAt(UINT32, IGradientStop*) override { return Missing(L"GradientStops.SetAt"); }
    HRESULT STDMETHODCALLTYPE InsertAt(UINT32, IGradientStop*) override { return Missing(L"GradientStops.InsertAt"); }
    HRESULT STDMETHODCALLTYPE RemoveAt(UINT32) override { return Missing(L"GradientStops.RemoveAt"); }
    HRESULT STDMETHODCALLTYPE Append(IGradientStop*) override { return Missing(L"GradientStops.Append"); }
    HRESULT STDMETHODCALLTYPE RemoveAtEnd() override { return Missing(L"GradientStops.RemoveAtEnd"); }
    HRESULT STDMETHODCALLTYPE Clear() override { return Missing(L"GradientStops.Clear"); }
    HRESULT STDMETHODCALLTYPE ReplaceAll(UINT32, IGradientStop**) override {
        return Missing(L"GradientStops.ReplaceAll");
    }
};
template <class Derived, class Primary, class... Interfaces>
class BrushObject : public Composable<Derived, Primary, DefaultIDependencyObject, Interfaces...> {
    PropertyValues properties_;
    const DWORD thread_ = GetCurrentThreadId();

protected:
    explicit BrushObject(IInspectable* outer)
        : Composable<Derived, Primary, DefaultIDependencyObject, Interfaces...>(outer) {}
    HRESULT CheckThread() const { return thread_ == GetCurrentThreadId() ? S_OK : RPC_E_WRONG_THREAD; }

public:
    HRESULT STDMETHODCALLTYPE GetValue(IDependencyProperty* p, IInspectable** v) override {
        return properties_.Get(p, v);
    }
    HRESULT STDMETHODCALLTYPE SetValue(IDependencyProperty* p, IInspectable* v) override {
        return properties_.Set(this, p, v);
    }
    HRESULT STDMETHODCALLTYPE ClearValue(IDependencyProperty* p) override {
        return properties_.Set(this, p, nullptr, true);
    }
    HRESULT STDMETHODCALLTYPE ReadLocalValue(IDependencyProperty* p, IInspectable** v) override {
        return properties_.Get(p, v, true);
    }
    HRESULT STDMETHODCALLTYPE GetAnimationBaseValue(IDependencyProperty* p, IInspectable** v) override {
        return properties_.Get(p, v);
    }
};
class Stop final : public BrushObject<Stop, IGradientStop> {
    ABI::Windows::UI::Color color_{};
    double offset_ = 0;

public:
    static constexpr auto Name = L"Windows.UI.Xaml.Media.GradientStop";
    explicit Stop(IInspectable* outer) : BrushObject(outer) {}
    HRESULT STDMETHODCALLTYPE get_Color(ABI::Windows::UI::Color* value) override {
        if (!value) return E_POINTER;
        *value = {};
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = color_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_Color(ABI::Windows::UI::Color value) override {
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) color_ = value;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_Offset(double* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = offset_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_Offset(double value) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (!std::isfinite(value)) return E_INVALIDARG;
        offset_ = value;
        return S_OK;
    }
};
template <class Derived, class Primary, class... Interfaces>
class PaintBrush : public BrushObject<Derived, Primary, IBrush, Interfaces...> {
    double opacity_ = 1;

protected:
    using Base = BrushObject<Derived, Primary, IBrush, Interfaces...>;
    using Base::CheckThread;
    explicit PaintBrush(IInspectable* outer) : Base(outer) {}

public:
    HRESULT STDMETHODCALLTYPE get_Opacity(double* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = opacity_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_Opacity(double value) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (!std::isfinite(value) || value < 0 || value > 1) return E_INVALIDARG;
        opacity_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Transform(ITransform** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return CheckThread();
    }
    HRESULT STDMETHODCALLTYPE put_Transform(ITransform* value) override {
        auto hr = CheckThread();
        return FAILED(hr) ? hr : value ? Unsupported(L"Brush.Transform") : S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_RelativeTransform(ITransform** value) override { return get_Transform(value); }
    HRESULT STDMETHODCALLTYPE put_RelativeTransform(ITransform* value) override {
        auto hr = CheckThread();
        return FAILED(hr) ? hr : value ? Unsupported(L"Brush.RelativeTransform") : S_OK;
    }
};
class Solid final : public PaintBrush<Solid, ISolidColorBrush> {
    ABI::Windows::UI::Color color_{0, 255, 255, 255};

public:
    static constexpr auto Name = L"Windows.UI.Xaml.Media.SolidColorBrush";
    explicit Solid(IInspectable* outer) : PaintBrush(outer) {}
    HRESULT STDMETHODCALLTYPE get_Color(ABI::Windows::UI::Color* value) override {
        if (!value) return E_POINTER;
        *value = {};
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = color_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_Color(ABI::Windows::UI::Color value) override {
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) color_ = value;
        return hr;
    }
};
HRESULT NewSolid(const ABI::Windows::UI::Color* color, ISolidColorBrush** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    return Guard([&]() -> HRESULT {
        ComPtr<ISolidColorBrush> brush;
        auto hr = Compose<Solid>(nullptr, nullptr, brush.GetAddressOf());
        if (SUCCEEDED(hr) && color) hr = brush->put_Color(*color);
        return FAILED(hr) ? hr : brush.CopyTo(value);
    });
}
class SolidFactory final : public Factory<ISolidColorBrushFactory> {
public:
    SolidFactory() : Factory<ISolidColorBrushFactory>(Solid::Name) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        ComPtr<ISolidColorBrush> brush;
        auto hr = NewSolid(nullptr, brush.GetAddressOf());
        return FAILED(hr) ? hr : brush.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE CreateInstanceWithColor(ABI::Windows::UI::Color color,
                                                      ISolidColorBrush** value) override {
        return NewSolid(&color, value);
    }
};
HRESULT MakeSolidFactory(IActivationFactory** value) {
    auto factory = Make<SolidFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
class Linear final : public PaintBrush<Linear, ILinearGradientBrush, IGradientBrush> {
    Point start_{0, 0}, end_{1, 1};
    GradientSpreadMethod spread_ = GradientSpreadMethod_Pad;
    BrushMappingMode mapping_ = BrushMappingMode_RelativeToBoundingBox;
    ColorInterpolationMode interpolation_ = ColorInterpolationMode_SRgbLinearInterpolation;
    ComPtr<Vector> stops_;
    HRESULT PointValue(Point value, Point& target) {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (!std::isfinite(value.X) || !std::isfinite(value.Y)) return E_INVALIDARG;
        target = value;
        return S_OK;
    }

public:
    static constexpr auto Name = L"Windows.UI.Xaml.Media.LinearGradientBrush";
    explicit Linear(IInspectable* outer) : PaintBrush(outer) {
        auto collection = Make<StopsCollection>(std::make_shared<StopItems>());
        if (!collection) throw std::bad_alloc();
        stops_ = collection;
    }
    HRESULT STDMETHODCALLTYPE get_StartPoint(Point* value) override {
        if (!value) return E_POINTER;
        *value = {};
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = start_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_StartPoint(Point value) override { return PointValue(value, start_); }
    HRESULT STDMETHODCALLTYPE get_EndPoint(Point* value) override {
        if (!value) return E_POINTER;
        *value = {};
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = end_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_EndPoint(Point value) override { return PointValue(value, end_); }
    HRESULT STDMETHODCALLTYPE get_GradientStops(Vector** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        return FAILED(hr) ? hr : stops_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_GradientStops(Vector* value) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (!value) return E_INVALIDARG;
        stops_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SpreadMethod(GradientSpreadMethod* value) override {
        if (!value) return E_POINTER;
        *value = {};
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = spread_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_SpreadMethod(GradientSpreadMethod value) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (value < GradientSpreadMethod_Pad || value > GradientSpreadMethod_Repeat) return E_INVALIDARG;
        spread_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_MappingMode(BrushMappingMode* value) override {
        if (!value) return E_POINTER;
        *value = {};
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = mapping_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_MappingMode(BrushMappingMode value) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (value != BrushMappingMode_Absolute && value != BrushMappingMode_RelativeToBoundingBox) return E_INVALIDARG;
        mapping_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ColorInterpolationMode(ColorInterpolationMode* value) override {
        if (!value) return E_POINTER;
        *value = {};
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = interpolation_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE put_ColorInterpolationMode(ColorInterpolationMode value) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (value != ColorInterpolationMode_ScRgbLinearInterpolation &&
            value != ColorInterpolationMode_SRgbLinearInterpolation)
            return E_INVALIDARG;
        interpolation_ = value;
        return S_OK;
    }
};
template <class T, class Primary> class BrushFactory final : public Factory<> {
public:
    BrushFactory() : Factory<>(T::Name) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return Guard([&]() -> HRESULT {
            ComPtr<Primary> object;
            auto hr = Compose<T>(nullptr, nullptr, object.GetAddressOf());
            return FAILED(hr) ? hr : object.CopyTo(value);
        });
    }
};
HRESULT LinearFactory(IActivationFactory** value) {
    auto factory = Make<BrushFactory<Linear, ILinearGradientBrush>>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
HRESULT StopFactory(IActivationFactory** value) {
    auto factory = Make<BrushFactory<Stop, IGradientStop>>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
bool Number(const std::wstring& text, double& value) {
    std::wistringstream number(text);
    number.imbue(std::locale::classic());
    return (number >> value) && (number >> std::ws).eof() && std::isfinite(value);
}
HRESULT ParsePoint(const std::wstring& text, Point& value) {
    auto comma = text.find(L',');
    if (comma == std::wstring::npos) return E_INVALIDARG;
    double x, y;
    if (!Number(text.substr(0, comma), x) || !Number(text.substr(comma + 1), y)) return E_INVALIDARG;
    if (std::abs(x) > std::numeric_limits<float>::max() || std::abs(y) > std::numeric_limits<float>::max())
        return E_INVALIDARG;
    Point next{static_cast<float>(x), static_cast<float>(y)};
    if (!std::isfinite(next.X) || !std::isfinite(next.Y)) return E_INVALIDARG;
    value = next;
    return S_OK;
}
// Names follow Windows.UI.IColorsStatics; RGB values use the public D2D SDK enum.
struct NamedColor {
    const wchar_t* name;
    UINT32 rgb;
};
constexpr NamedColor namedColors[] = {
    {L"AliceBlue", D2D1::ColorF::AliceBlue},
    {L"AntiqueWhite", D2D1::ColorF::AntiqueWhite},
    {L"Aqua", D2D1::ColorF::Aqua},
    {L"Aquamarine", D2D1::ColorF::Aquamarine},
    {L"Azure", D2D1::ColorF::Azure},
    {L"Beige", D2D1::ColorF::Beige},
    {L"Bisque", D2D1::ColorF::Bisque},
    {L"Black", D2D1::ColorF::Black},
    {L"BlanchedAlmond", D2D1::ColorF::BlanchedAlmond},
    {L"Blue", D2D1::ColorF::Blue},
    {L"BlueViolet", D2D1::ColorF::BlueViolet},
    {L"Brown", D2D1::ColorF::Brown},
    {L"BurlyWood", D2D1::ColorF::BurlyWood},
    {L"CadetBlue", D2D1::ColorF::CadetBlue},
    {L"Chartreuse", D2D1::ColorF::Chartreuse},
    {L"Chocolate", D2D1::ColorF::Chocolate},
    {L"Coral", D2D1::ColorF::Coral},
    {L"CornflowerBlue", D2D1::ColorF::CornflowerBlue},
    {L"Cornsilk", D2D1::ColorF::Cornsilk},
    {L"Crimson", D2D1::ColorF::Crimson},
    {L"Cyan", D2D1::ColorF::Cyan},
    {L"DarkBlue", D2D1::ColorF::DarkBlue},
    {L"DarkCyan", D2D1::ColorF::DarkCyan},
    {L"DarkGoldenrod", D2D1::ColorF::DarkGoldenrod},
    {L"DarkGray", D2D1::ColorF::DarkGray},
    {L"DarkGreen", D2D1::ColorF::DarkGreen},
    {L"DarkKhaki", D2D1::ColorF::DarkKhaki},
    {L"DarkMagenta", D2D1::ColorF::DarkMagenta},
    {L"DarkOliveGreen", D2D1::ColorF::DarkOliveGreen},
    {L"DarkOrange", D2D1::ColorF::DarkOrange},
    {L"DarkOrchid", D2D1::ColorF::DarkOrchid},
    {L"DarkRed", D2D1::ColorF::DarkRed},
    {L"DarkSalmon", D2D1::ColorF::DarkSalmon},
    {L"DarkSeaGreen", D2D1::ColorF::DarkSeaGreen},
    {L"DarkSlateBlue", D2D1::ColorF::DarkSlateBlue},
    {L"DarkSlateGray", D2D1::ColorF::DarkSlateGray},
    {L"DarkTurquoise", D2D1::ColorF::DarkTurquoise},
    {L"DarkViolet", D2D1::ColorF::DarkViolet},
    {L"DeepPink", D2D1::ColorF::DeepPink},
    {L"DeepSkyBlue", D2D1::ColorF::DeepSkyBlue},
    {L"DimGray", D2D1::ColorF::DimGray},
    {L"DodgerBlue", D2D1::ColorF::DodgerBlue},
    {L"Firebrick", D2D1::ColorF::Firebrick},
    {L"FloralWhite", D2D1::ColorF::FloralWhite},
    {L"ForestGreen", D2D1::ColorF::ForestGreen},
    {L"Fuchsia", D2D1::ColorF::Fuchsia},
    {L"Gainsboro", D2D1::ColorF::Gainsboro},
    {L"GhostWhite", D2D1::ColorF::GhostWhite},
    {L"Gold", D2D1::ColorF::Gold},
    {L"Goldenrod", D2D1::ColorF::Goldenrod},
    {L"Gray", D2D1::ColorF::Gray},
    {L"Green", D2D1::ColorF::Green},
    {L"GreenYellow", D2D1::ColorF::GreenYellow},
    {L"Honeydew", D2D1::ColorF::Honeydew},
    {L"HotPink", D2D1::ColorF::HotPink},
    {L"IndianRed", D2D1::ColorF::IndianRed},
    {L"Indigo", D2D1::ColorF::Indigo},
    {L"Ivory", D2D1::ColorF::Ivory},
    {L"Khaki", D2D1::ColorF::Khaki},
    {L"Lavender", D2D1::ColorF::Lavender},
    {L"LavenderBlush", D2D1::ColorF::LavenderBlush},
    {L"LawnGreen", D2D1::ColorF::LawnGreen},
    {L"LemonChiffon", D2D1::ColorF::LemonChiffon},
    {L"LightBlue", D2D1::ColorF::LightBlue},
    {L"LightCoral", D2D1::ColorF::LightCoral},
    {L"LightCyan", D2D1::ColorF::LightCyan},
    {L"LightGoldenrodYellow", D2D1::ColorF::LightGoldenrodYellow},
    {L"LightGreen", D2D1::ColorF::LightGreen},
    {L"LightGray", D2D1::ColorF::LightGray},
    {L"LightPink", D2D1::ColorF::LightPink},
    {L"LightSalmon", D2D1::ColorF::LightSalmon},
    {L"LightSeaGreen", D2D1::ColorF::LightSeaGreen},
    {L"LightSkyBlue", D2D1::ColorF::LightSkyBlue},
    {L"LightSlateGray", D2D1::ColorF::LightSlateGray},
    {L"LightSteelBlue", D2D1::ColorF::LightSteelBlue},
    {L"LightYellow", D2D1::ColorF::LightYellow},
    {L"Lime", D2D1::ColorF::Lime},
    {L"LimeGreen", D2D1::ColorF::LimeGreen},
    {L"Linen", D2D1::ColorF::Linen},
    {L"Magenta", D2D1::ColorF::Magenta},
    {L"Maroon", D2D1::ColorF::Maroon},
    {L"MediumAquamarine", D2D1::ColorF::MediumAquamarine},
    {L"MediumBlue", D2D1::ColorF::MediumBlue},
    {L"MediumOrchid", D2D1::ColorF::MediumOrchid},
    {L"MediumPurple", D2D1::ColorF::MediumPurple},
    {L"MediumSeaGreen", D2D1::ColorF::MediumSeaGreen},
    {L"MediumSlateBlue", D2D1::ColorF::MediumSlateBlue},
    {L"MediumSpringGreen", D2D1::ColorF::MediumSpringGreen},
    {L"MediumTurquoise", D2D1::ColorF::MediumTurquoise},
    {L"MediumVioletRed", D2D1::ColorF::MediumVioletRed},
    {L"MidnightBlue", D2D1::ColorF::MidnightBlue},
    {L"MintCream", D2D1::ColorF::MintCream},
    {L"MistyRose", D2D1::ColorF::MistyRose},
    {L"Moccasin", D2D1::ColorF::Moccasin},
    {L"NavajoWhite", D2D1::ColorF::NavajoWhite},
    {L"Navy", D2D1::ColorF::Navy},
    {L"OldLace", D2D1::ColorF::OldLace},
    {L"Olive", D2D1::ColorF::Olive},
    {L"OliveDrab", D2D1::ColorF::OliveDrab},
    {L"Orange", D2D1::ColorF::Orange},
    {L"OrangeRed", D2D1::ColorF::OrangeRed},
    {L"Orchid", D2D1::ColorF::Orchid},
    {L"PaleGoldenrod", D2D1::ColorF::PaleGoldenrod},
    {L"PaleGreen", D2D1::ColorF::PaleGreen},
    {L"PaleTurquoise", D2D1::ColorF::PaleTurquoise},
    {L"PaleVioletRed", D2D1::ColorF::PaleVioletRed},
    {L"PapayaWhip", D2D1::ColorF::PapayaWhip},
    {L"PeachPuff", D2D1::ColorF::PeachPuff},
    {L"Peru", D2D1::ColorF::Peru},
    {L"Pink", D2D1::ColorF::Pink},
    {L"Plum", D2D1::ColorF::Plum},
    {L"PowderBlue", D2D1::ColorF::PowderBlue},
    {L"Purple", D2D1::ColorF::Purple},
    {L"Red", D2D1::ColorF::Red},
    {L"RosyBrown", D2D1::ColorF::RosyBrown},
    {L"RoyalBlue", D2D1::ColorF::RoyalBlue},
    {L"SaddleBrown", D2D1::ColorF::SaddleBrown},
    {L"Salmon", D2D1::ColorF::Salmon},
    {L"SandyBrown", D2D1::ColorF::SandyBrown},
    {L"SeaGreen", D2D1::ColorF::SeaGreen},
    {L"SeaShell", D2D1::ColorF::SeaShell},
    {L"Sienna", D2D1::ColorF::Sienna},
    {L"Silver", D2D1::ColorF::Silver},
    {L"SkyBlue", D2D1::ColorF::SkyBlue},
    {L"SlateBlue", D2D1::ColorF::SlateBlue},
    {L"SlateGray", D2D1::ColorF::SlateGray},
    {L"Snow", D2D1::ColorF::Snow},
    {L"SpringGreen", D2D1::ColorF::SpringGreen},
    {L"SteelBlue", D2D1::ColorF::SteelBlue},
    {L"Tan", D2D1::ColorF::Tan},
    {L"Teal", D2D1::ColorF::Teal},
    {L"Thistle", D2D1::ColorF::Thistle},
    {L"Tomato", D2D1::ColorF::Tomato},
    {L"Turquoise", D2D1::ColorF::Turquoise},
    {L"Violet", D2D1::ColorF::Violet},
    {L"Wheat", D2D1::ColorF::Wheat},
    {L"White", D2D1::ColorF::White},
    {L"WhiteSmoke", D2D1::ColorF::WhiteSmoke},
    {L"Yellow", D2D1::ColorF::Yellow},
    {L"YellowGreen", D2D1::ColorF::YellowGreen},
};
bool ColorName(const std::wstring& text, const wchar_t* name) {
    if (text.size() != wcslen(name)) return false;
    auto lower = [](wchar_t c) { return c >= L'A' && c <= L'Z' ? c - L'A' + L'a' : c; };
    for (size_t i = 0; i < text.size(); ++i)
        if (lower(text[i]) != lower(name[i])) return false;
    return true;
}
HRESULT ParseColor(const std::wstring& input, ABI::Windows::UI::Color& color) {
    auto first = input.find_first_not_of(L" \t\r\n"), last = input.find_last_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return E_INVALIDARG;
    auto text = input.substr(first, last - first + 1);
    if (text.front() == L'{' || text.starts_with(L"sc#")) return E_NOTIMPL;
    if (ColorName(text, L"Transparent")) {
        color = {0, 255, 255, 255};
        return S_OK;
    }
    for (const auto& named : namedColors)
        if (ColorName(text, named.name)) {
            color = {255, static_cast<BYTE>(named.rgb >> 16), static_cast<BYTE>(named.rgb >> 8),
                     static_cast<BYTE>(named.rgb)};
            return S_OK;
        }

    if (text.empty() || text[0] != L'#' ||
        (text.size() != 4 && text.size() != 5 && text.size() != 7 && text.size() != 9))
        return E_INVALIDARG;
    UINT32 value = 0;
    for (size_t i = 1; i < text.size(); ++i) {
        auto c = text[i];
        int digit = c >= L'0' && c <= L'9'   ? c - L'0'
                    : c >= L'a' && c <= L'f' ? c - L'a' + 10
                    : c >= L'A' && c <= L'F' ? c - L'A' + 10
                                             : -1;
        if (digit < 0) return E_INVALIDARG;
        value = (value << 4) | digit;
    }
    if (text.size() == 4 || text.size() == 5) {
        UINT32 expanded = 0;
        for (int i = 0; i < 4; ++i) expanded |= ((value >> (i * 4)) & 15) * 17 << (i * 8);
        value = expanded;
    }
    if (text.size() == 4 || text.size() == 7) value |= 0xff000000;
    color = {static_cast<BYTE>(value >> 24), static_cast<BYTE>(value >> 16), static_cast<BYTE>(value >> 8),
             static_cast<BYTE>(value)};
    return S_OK;
}
}
bool BrushType(const std::wstring& type) { return type == L"LinearGradientBrush" || type == L"SolidColorBrush"; }
bool BrushDataType(const std::wstring& type) { return BrushType(type) || type == L"GradientStop"; }
HRESULT ParseSourceColor(const std::wstring& text, ABI::Windows::UI::Color& color) { return ParseColor(text, color); }
HRESULT MakeSolidColorBrush(ABI::Windows::UI::Color color, IBrush** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    ComPtr<ISolidColorBrush> brush;
    auto hr = NewSolid(&color, brush.GetAddressOf());
    return FAILED(hr) ? hr : brush.CopyTo(value);
}
HRESULT ParseBrushAttribute(const std::wstring& type, const std::wstring& name, const std::wstring& text,
                            BrushSourceData& data) {
    if (!text.empty() && text.front() == L'{') return E_NOTIMPL;
    if (name == L"Color" && (type == L"GradientStop" || type == L"SolidColorBrush")) {
        auto hr = ParseColor(text, data.color);
        if (SUCCEEDED(hr)) data.hasColor = true;
        return hr;
    }
    if (type == L"GradientStop") {
        if (name == L"Offset") return Number(text, data.offset) ? S_OK : E_INVALIDARG;
    } else {
        if (type == L"LinearGradientBrush" && name == L"StartPoint") return ParsePoint(text, data.start);
        if (type == L"LinearGradientBrush" && name == L"EndPoint") return ParsePoint(text, data.end);
        if (name == L"Opacity")
            return Number(text, data.opacity) && data.opacity >= 0 && data.opacity <= 1 ? S_OK : E_INVALIDARG;
    }
    return E_NOTIMPL;
}
HRESULT ConfigureBrushData(IInspectable* object, const std::wstring& type, const BrushSourceData& data) {
    if (!object) return E_INVALIDARG;
    if (type == L"GradientStop") {
        ComPtr<IGradientStop> stop;
        auto hr = object->QueryInterface(IID_PPV_ARGS(&stop));
        if (SUCCEEDED(hr) && data.hasColor) hr = stop->put_Color(data.color);
        if (SUCCEEDED(hr)) hr = stop->put_Offset(data.offset);
        return hr;
    }
    HRESULT hr = S_OK;
    if (type == L"SolidColorBrush") {
        ComPtr<ISolidColorBrush> solid;
        hr = object->QueryInterface(IID_PPV_ARGS(&solid));
        if (SUCCEEDED(hr) && data.hasColor) hr = solid->put_Color(data.color);
    } else {
        ComPtr<ILinearGradientBrush> linear;
        hr = object->QueryInterface(IID_PPV_ARGS(&linear));
        if (SUCCEEDED(hr)) hr = linear->put_StartPoint(data.start);
        if (SUCCEEDED(hr)) hr = linear->put_EndPoint(data.end);
    }
    ComPtr<IBrush> brush;
    if (SUCCEEDED(hr)) hr = object->QueryInterface(IID_PPV_ARGS(&brush));
    return FAILED(hr) ? hr : brush->put_Opacity(data.opacity);
}
HRESULT SetSourceGradientStops(IInspectable* brush, StopItems stops) {
    if (!brush) return E_INVALIDARG;
    return Guard([&]() -> HRESULT {
        ComPtr<IGradientBrush> gradient;
        auto hr = brush->QueryInterface(IID_PPV_ARGS(&gradient));
        if (FAILED(hr)) return hr;
        auto collection = Make<StopsCollection>(std::make_shared<StopItems>(std::move(stops)));
        return collection ? gradient->put_GradientStops(collection.Get()) : E_OUTOFMEMORY;
    });
}
const ShimClass solidBrushClass{Solid::Name, MakeSolidFactory, ShimPolicy::FillMissing};
const ShimClass linearBrushClass{Linear::Name, LinearFactory, ShimPolicy::FillMissing};
const ShimClass gradientStopClass{Stop::Name, StopFactory, ShimPolicy::FillMissing};
}
