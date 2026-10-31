#include "brush.h"
#include "properties.h"
#include "generated/xaml.h"
#include "com/composable.h"
#include "com/factory.h"
#include "registry.h"
#include <algorithm>
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
class Linear final : public BrushObject<Linear, ILinearGradientBrush, IGradientBrush, IBrush> {
    Point start_{0, 0}, end_{1, 1};
    double opacity_ = 1;
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
    explicit Linear(IInspectable* outer) : BrushObject(outer) {
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
HRESULT ParseColor(const std::wstring& text, ABI::Windows::UI::Color& color) {
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
bool BrushDataType(const std::wstring& type) { return type == L"LinearGradientBrush" || type == L"GradientStop"; }
HRESULT ParseBrushAttribute(const std::wstring& type, const std::wstring& name, const std::wstring& text,
                            BrushSourceData& data) {
    if (!text.empty() && text.front() == L'{') return E_NOTIMPL;
    if (type == L"GradientStop") {
        if (name == L"Color") return ParseColor(text, data.color);
        if (name == L"Offset") return Number(text, data.offset) ? S_OK : E_INVALIDARG;
    } else {
        if (name == L"StartPoint") return ParsePoint(text, data.start);
        if (name == L"EndPoint") return ParsePoint(text, data.end);
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
        if (SUCCEEDED(hr)) hr = stop->put_Color(data.color);
        if (SUCCEEDED(hr)) hr = stop->put_Offset(data.offset);
        return hr;
    }
    ComPtr<ILinearGradientBrush> linear;
    auto hr = object->QueryInterface(IID_PPV_ARGS(&linear));
    if (SUCCEEDED(hr)) hr = linear->put_StartPoint(data.start);
    if (SUCCEEDED(hr)) hr = linear->put_EndPoint(data.end);
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
const ShimClass linearBrushClass{Linear::Name, LinearFactory, ShimPolicy::FillMissing};
const ShimClass gradientStopClass{Stop::Name, StopFactory, ShimPolicy::FillMissing};
}
