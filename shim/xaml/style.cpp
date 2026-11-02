#include "style.h"
#include "element.h"
#include "generated/controls.h"
#include "generated/xaml_media.h"
#include "com/factory.h"
#include "registry.h"
#include <wrl/wrappers/corewrappers.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
using namespace ABI::Windows::UI::Xaml::Interop;
namespace appx {
namespace {
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
MIDL_INTERFACE("B246845D-3A40-4BDC-8C09-B1A83CBE23EF") ISourceStyle : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE ApplyTo(IInspectable * target, const wchar_t* targetType) = 0;
};
MIDL_INTERFACE("0093762A-1F98-45E0-B79F-C2F7BE56F533") ISourceTemplate : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Instantiate(const wchar_t* targetType, const std::shared_ptr<TemplateLink>& link,
                                                  ComPtr<IUIElement>& root) = 0;
};
MIDL_INTERFACE("6AE924CF-2137-469D-B7EC-B5C8E06D67FE") IPresenterSource : IUnknown {
    virtual void STDMETHODCALLTYPE Bind(const std::shared_ptr<TemplateLink>& link) = 0;
};
// Style and template objects: DependencyObjects with local values and thread affinity.
template <class Derived, class Primary, class... Interfaces>
class SourceObject : public Composable<Derived, Primary, DefaultIDependencyObject, Interfaces...> {
    PropertyValues properties_;
    const DWORD thread_ = GetCurrentThreadId();

protected:
    explicit SourceObject(IInspectable* outer) : Composable<Derived, Primary, DefaultIDependencyObject, Interfaces...>(outer) {}
    HRESULT CheckThread() const { return thread_ == GetCurrentThreadId() ? S_OK : RPC_E_WRONG_THREAD; }
    static HRESULT CopyType(const std::wstring& name, TypeName* value) {
        value->Kind = TypeKind_Metadata;
        return WindowsCreateString(name.c_str(), static_cast<UINT32>(name.size()), &value->Name);
    }

public:
    HRESULT STDMETHODCALLTYPE GetValue(IDependencyProperty* p, IInspectable** v) override { return properties_.Get(p, v); }
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
// A source-loaded Style. Its setters are resolved by (TargetType, property name)
// when the source loads, because framework properties have no DependencyProperty
// identities here; the public Setters collection is therefore not exposed.
class Style final : public SourceObject<Style, IStyle, ISourceStyle> {
    std::wstring targetType_;
    std::vector<StyleSetter> setters_;
    bool sealed_ = false;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Style";
    explicit Style(IInspectable* outer) : SourceObject(outer) {}
    void Initialize(std::wstring targetType, std::vector<StyleSetter> setters) {
        targetType_ = std::move(targetType);
        setters_ = std::move(setters);
    }
    HRESULT STDMETHODCALLTYPE get_IsSealed(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) *value = sealed_;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_Setters(ISetterBaseCollection** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckThread();
        return FAILED(hr) ? hr : Unsupported(L"Style.Setters");
    }
    HRESULT STDMETHODCALLTYPE get_TargetType(TypeName* value) override {
        if (!value) return E_POINTER;
        *value = {};
        auto hr = CheckThread();
        return FAILED(hr) ? hr : Guard([&] { return CopyType(targetType_, value); });
    }
    HRESULT STDMETHODCALLTYPE put_TargetType(TypeName) override {
        auto hr = CheckThread();
        return FAILED(hr) ? hr : sealed_ ? E_ILLEGAL_METHOD_CALL : Unsupported(L"Style.TargetType");
    }
    HRESULT STDMETHODCALLTYPE get_BasedOn(IStyle** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return CheckThread();
    }
    HRESULT STDMETHODCALLTYPE put_BasedOn(IStyle*) override {
        auto hr = CheckThread();
        return FAILED(hr) ? hr : sealed_ ? E_ILLEGAL_METHOD_CALL : Unsupported(L"Style.BasedOn");
    }
    HRESULT STDMETHODCALLTYPE Seal() override {
        auto hr = CheckThread();
        if (SUCCEEDED(hr)) sealed_ = true;
        return hr;
    }
    // Only exact target types are matched: base-class targets need type
    // inheritance information the native elements do not carry.
    HRESULT STDMETHODCALLTYPE ApplyTo(IInspectable* target, const wchar_t* targetType) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (targetType_ != targetType) {
            Log("UNIMPL", "Style.TargetType %ls on %ls hr=%08lx", targetType_.c_str(), targetType, E_NOTIMPL);
            return E_NOTIMPL;
        }
        sealed_ = true;
        for (auto& setter : setters_) {
            hr = setter(target);
            if (FAILED(hr)) return hr;
        }
        return S_OK;
    }
};
class ControlTemplate final : public SourceObject<ControlTemplate, IControlTemplate, IFrameworkTemplate, ISourceTemplate> {
    std::wstring targetType_;
    TemplateFactory factory_;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.ControlTemplate";
    explicit ControlTemplate(IInspectable* outer) : SourceObject(outer) {}
    void Initialize(std::wstring targetType, TemplateFactory factory) {
        targetType_ = std::move(targetType);
        factory_ = std::move(factory);
    }
    HRESULT STDMETHODCALLTYPE get_TargetType(TypeName* value) override {
        if (!value) return E_POINTER;
        *value = {};
        auto hr = CheckThread();
        if (FAILED(hr) || targetType_.empty()) return hr;
        return Guard([&] { return CopyType(targetType_, value); });
    }
    HRESULT STDMETHODCALLTYPE put_TargetType(TypeName) override {
        auto hr = CheckThread();
        return FAILED(hr) ? hr : Unsupported(L"ControlTemplate.TargetType");
    }
    HRESULT STDMETHODCALLTYPE Instantiate(const wchar_t* targetType, const std::shared_ptr<TemplateLink>& link,
                                          ComPtr<IUIElement>& root) override {
        auto hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (!targetType_.empty() && targetType_ != targetType) {
            Log("UNIMPL", "ControlTemplate.TargetType %ls on %ls hr=%08lx", targetType_.c_str(), targetType, E_NOTIMPL);
            return E_NOTIMPL;
        }
        return factory_(link, root);
    }
};
// Shows its templated parent's Content unless given its own.
class ContentPresenter final : public Element<ContentPresenter, DefaultIContentPresenter, IPresenterSource> {
    std::shared_ptr<TemplateLink> link_;
    ComPtr<IInspectable> content_;
    bool hasContent_ = false;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.ContentPresenter";
    explicit ContentPresenter(IInspectable* outer) : Element(outer) {}
    void STDMETHODCALLTYPE Bind(const std::shared_ptr<TemplateLink>& link) override { link_ = link; }
    HRESULT STDMETHODCALLTYPE get_Content(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto hr = CheckSourceThread();
        if (FAILED(hr)) return hr;
        if (hasContent_) return content_.CopyTo(value);
        return link_ && link_->parent ? link_->parent->get_Content(value) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Content(IInspectable* value) override {
        auto hr = CheckSourceThread();
        if (FAILED(hr)) return hr;
        content_ = value;
        hasContent_ = true;
        return S_OK;
    }
};
// The visual children of the element types the shim builds. Controls without
// an applied template, and other types, fail rather than guess at a default template.
HRESULT VisualChildren(IDependencyObject* reference, std::vector<ComPtr<IDependencyObject>>& children) {
    if (!reference) return E_INVALIDARG;
    ComPtr<INamedElementRegistry> element;
    if (FAILED(reference->QueryInterface(IID_PPV_ARGS(&element)))) return Unsupported(L"VisualTreeHelper reference type");
    auto hr = element->CheckSourceThread();
    if (FAILED(hr)) return hr;
    auto add = [&](IInspectable* child) -> HRESULT {
        if (!child) return S_OK;
        ComPtr<IDependencyObject> value;
        auto result = child->QueryInterface(IID_PPV_ARGS(&value));
        if (SUCCEEDED(result)) children.push_back(std::move(value));
        return result;
    };
    ComPtr<ITemplatedElement> templated;
    if (SUCCEEDED(reference->QueryInterface(IID_PPV_ARGS(&templated)))) {
        ComPtr<IUIElement> root;
        hr = templated->TemplateRoot(&root);
        if (hr == S_OK) return add(root.Get());
        if (FAILED(hr)) return hr;
        return Unsupported(L"VisualTreeHelper children of an untemplated control");
    }
    ComPtr<IContentPresenter> presenter;
    if (SUCCEEDED(reference->QueryInterface(IID_PPV_ARGS(&presenter)))) {
        ComPtr<IInspectable> content;
        hr = presenter->get_Content(&content);
        if (FAILED(hr) || !content) return hr;
        ComPtr<IUIElement> visual;
        if (FAILED(content.As(&visual))) return Unsupported(L"VisualTreeHelper non-element ContentPresenter content");
        return add(visual.Get());
    }
    ComPtr<IPanel> panel;
    if (SUCCEEDED(reference->QueryInterface(IID_PPV_ARGS(&panel)))) {
        ComPtr<ABI::Windows::Foundation::Collections::IVector<UIElement*>> items;
        hr = panel->get_Children(&items);
        UINT32 size = 0;
        if (SUCCEEDED(hr)) hr = items->get_Size(&size);
        for (UINT32 i = 0; SUCCEEDED(hr) && i < size; ++i) {
            ComPtr<IUIElement> item;
            hr = items->GetAt(i, &item);
            if (SUCCEEDED(hr)) hr = add(item.Get());
        }
        return hr;
    }
    ComPtr<IViewbox> viewbox;
    if (SUCCEEDED(reference->QueryInterface(IID_PPV_ARGS(&viewbox)))) {
        ComPtr<IUIElement> child;
        hr = viewbox->get_Child(&child);
        return FAILED(hr) ? hr : add(child.Get());
    }
    ComPtr<IUserControl> user;
    if (SUCCEEDED(reference->QueryInterface(IID_PPV_ARGS(&user)))) {
        ComPtr<IUIElement> child;
        hr = user->get_Content(&child);
        return FAILED(hr) ? hr : add(child.Get());
    }
    ComPtr<IUnknown> leaf;
    if (SUCCEEDED(reference->QueryInterface(__uuidof(IMediaElement), &leaf)) ||
        SUCCEEDED(reference->QueryInterface(__uuidof(IImage), &leaf)) ||
        SUCCEEDED(reference->QueryInterface(__uuidof(ITextBlock), &leaf)))
        return S_OK;
    return Unsupported(L"VisualTreeHelper children of this element type");
}
class VisualTreeHelper final : public Factory<ABI::Windows::UI::Xaml::Media::DefaultIVisualTreeHelperStatics> {
public:
    VisualTreeHelper() : Factory(L"Windows.UI.Xaml.Media.VisualTreeHelper") {}
    HRESULT STDMETHODCALLTYPE GetChild(IDependencyObject* reference, INT32 index, IDependencyObject** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return Guard([&]() -> HRESULT {
            std::vector<ComPtr<IDependencyObject>> children;
            auto hr = VisualChildren(reference, children);
            if (FAILED(hr)) return hr;
            if (index < 0 || static_cast<size_t>(index) >= children.size()) return E_INVALIDARG;
            return children[index].CopyTo(result);
        });
    }
    HRESULT STDMETHODCALLTYPE GetChildrenCount(IDependencyObject* reference, INT32* result) override {
        if (!result) return E_POINTER;
        *result = 0;
        return Guard([&]() -> HRESULT {
            std::vector<ComPtr<IDependencyObject>> children;
            auto hr = VisualChildren(reference, children);
            if (SUCCEEDED(hr)) *result = static_cast<INT32>(children.size());
            return hr;
        });
    }
};
HRESULT MakeVisualTreeHelperFactory(IActivationFactory** value) {
    auto factory = Make<VisualTreeHelper>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
HRESULT MakeSourceStyle(const std::wstring& targetType, std::vector<StyleSetter> setters, IStyle** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    return Guard([&]() -> HRESULT {
        ComPtr<IStyle> style;
        auto hr = Compose<Style>(nullptr, nullptr, style.GetAddressOf());
        if (FAILED(hr)) return hr;
        static_cast<Style*>(style.Get())->Initialize(targetType, std::move(setters));
        *value = style.Detach();
        return S_OK;
    });
}
HRESULT MakeSourceControlTemplate(const std::wstring& targetType, TemplateFactory factory, IControlTemplate** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    return Guard([&]() -> HRESULT {
        ComPtr<IControlTemplate> result;
        auto hr = Compose<ControlTemplate>(nullptr, nullptr, result.GetAddressOf());
        if (FAILED(hr)) return hr;
        static_cast<ControlTemplate*>(result.Get())->Initialize(targetType, std::move(factory));
        *value = result.Detach();
        return S_OK;
    });
}
HRESULT ApplySourceStyle(IStyle* style, IInspectable* target, const wchar_t* targetType) {
    ComPtr<ISourceStyle> source;
    if (!style || FAILED(style->QueryInterface(IID_PPV_ARGS(&source)))) return Unsupported(L"non-source Style");
    return source->ApplyTo(target, targetType);
}
HRESULT InstantiateSourceTemplate(IControlTemplate* value, const wchar_t* targetType,
                                  const std::shared_ptr<TemplateLink>& link, ComPtr<IUIElement>& root) {
    ComPtr<ISourceTemplate> source;
    if (!value || FAILED(value->QueryInterface(IID_PPV_ARGS(&source)))) return Unsupported(L"non-source ControlTemplate");
    return source->Instantiate(targetType, link, root);
}
HRESULT MakeContentPresenter(IInspectable** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    return Guard([&]() -> HRESULT {
        ComPtr<IContentPresenter> presenter;
        auto hr = Compose<ContentPresenter>(nullptr, nullptr, presenter.GetAddressOf());
        return FAILED(hr) ? hr : presenter.CopyTo(value);
    });
}
HRESULT BindContentPresenter(IInspectable* presenter, const std::shared_ptr<TemplateLink>& link) {
    ComPtr<IPresenterSource> source;
    auto hr = presenter ? presenter->QueryInterface(IID_PPV_ARGS(&source)) : E_INVALIDARG;
    if (SUCCEEDED(hr)) source->Bind(link);
    return hr;
}
const ShimClass visualTreeHelperClass{L"Windows.UI.Xaml.Media.VisualTreeHelper", MakeVisualTreeHelperFactory};
}
