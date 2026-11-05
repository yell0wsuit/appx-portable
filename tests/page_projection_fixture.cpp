// Test-only SDK ABI navigation caller for a managed Page's controlling outer.
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.navigation.h>
#include <windows.ui.xaml.markup.h>
#include <windows.ui.xaml.input.h>
#include <wrl.h>
#pragma warning(disable : 4996)
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.ApplicationSettings.h>
#include <winrt/Windows.UI.Popups.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml::Navigation;
struct FixtureSettingsRequest
    : winrt::implements<FixtureSettingsRequest, winrt::Windows::UI::ApplicationSettings::ISettingsPaneCommandsRequest> {
    winrt::Windows::Foundation::Collections::IVector<winrt::Windows::UI::ApplicationSettings::SettingsCommand>
        commands = winrt::single_threaded_vector<winrt::Windows::UI::ApplicationSettings::SettingsCommand>();
    auto ApplicationCommands() { return commands; }
};
extern "C" HRESULT __cdecl CreateSettingsCommandsRequest(IInspectable** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    try {
        *value = reinterpret_cast<IInspectable*>(winrt::detach_abi(winrt::make<FixtureSettingsRequest>()));
        return S_OK;
    } catch (...) {
        return winrt::to_hresult();
    }
}
extern "C" HRESULT __cdecl InvokeSettingsCommand(IInspectable* request, UINT32 index) {
    if (!request) return E_POINTER;
    try {
        winrt::Windows::UI::ApplicationSettings::SettingsPaneCommandsRequest value{nullptr};
        winrt::copy_from_abi(value, request);
        auto command = value.ApplicationCommands().GetAt(index).as<winrt::Windows::UI::Popups::IUICommand>();
        auto invoked = command.Invoked();
        if (!invoked) return E_UNEXPECTED;
        invoked(command);
        return S_OK;
    } catch (...) {
        return winrt::to_hresult();
    }
}
class TestKeyArgs final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, ABI::Windows::UI::Xaml::Input::IKeyRoutedEventArgs> {
    InspectableClass(L"Windows.UI.Xaml.Input.KeyRoutedEventArgs", BaseTrust);
    boolean handled_ = false;

public:
    HRESULT STDMETHODCALLTYPE get_Key(ABI::Windows::System::VirtualKey* value) override {
        if (!value) return E_POINTER;
        *value = ABI::Windows::System::VirtualKey_Escape;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_KeyStatus(ABI::Windows::UI::Core::CorePhysicalKeyStatus*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = handled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean value) override {
        handled_ = value;
        return S_OK;
    }
};
extern "C" HRESULT __cdecl InvokeKeyDown(IUnknown* control, boolean withArgs) {
    if (!control) return E_POINTER;
    ComPtr<ABI::Windows::UI::Xaml::Controls::IControlOverrides> callbacks;
    auto hr = control->QueryInterface(IID_PPV_ARGS(&callbacks));
    auto args = Make<TestKeyArgs>();
    return FAILED(hr) ? hr : callbacks->OnKeyDown(withArgs ? args.Get() : nullptr);
}
class TestNavigationArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>, INavigationEventArgs> {
    InspectableClass(L"Windows.UI.Xaml.Navigation.NavigationEventArgs", BaseTrust);
    ComPtr<ABI::Windows::Foundation::IUriRuntimeClass> uri_;

public:
    HRESULT STDMETHODCALLTYPE get_Content(IInspectable** value) override { if (!value) return E_POINTER; *value = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Parameter(IInspectable** value) override { if (!value) return E_POINTER; *value = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_SourcePageType(ABI::Windows::UI::Xaml::Interop::TypeName* value) override {
        if (!value) return E_POINTER;
        value->Name = nullptr; value->Kind = ABI::Windows::UI::Xaml::Interop::TypeKind_Custom; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_NavigationMode(NavigationMode* value) override { if (!value) return E_POINTER; *value = NavigationMode_New; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Uri(ABI::Windows::Foundation::IUriRuntimeClass** value) override { if (!value) return E_POINTER; return uri_.CopyTo(value); }
    HRESULT STDMETHODCALLTYPE put_Uri(ABI::Windows::Foundation::IUriRuntimeClass* value) override { uri_ = value; return S_OK; }
};
extern "C" HRESULT __cdecl InvokePageOverrides(IUnknown* page) {
    if (!page) return E_POINTER;
    ComPtr<ABI::Windows::UI::Xaml::Controls::IPageOverrides> callbacks;
    auto hr = page->QueryInterface(IID_PPV_ARGS(&callbacks));
    if (FAILED(hr)) return hr;
    auto args = Make<TestNavigationArgs>();
    if (!args) return E_OUTOFMEMORY;
    hr = callbacks->OnNavigatedTo(args.Get());
    if (SUCCEEDED(hr)) hr = callbacks->OnNavigatedFrom(args.Get());
    if (SUCCEEDED(hr)) hr = callbacks->OnNavigatingFrom(nullptr);
    return hr;
}

extern "C" HRESULT __cdecl InvokeComponentConnector(IUnknown* page, INT32 id, IInspectable* target) {
    if (!page) return E_POINTER;
    ComPtr<ABI::Windows::UI::Xaml::Markup::IComponentConnector> connector;
    auto hr = page->QueryInterface(IID_PPV_ARGS(&connector));
    return FAILED(hr) ? hr : connector->Connect(id, target);
}

// Resolve the same Window the external, marked consumer populated. This tests
// callbacks across Windows.dll's assembly boundary without exposing its internals.
static HRESULT WindowContent(ABI::Windows::UI::Xaml::IUIElement** value) {
    using namespace ABI::Windows::UI::Xaml;
    using GetFactory = HRESULT (WINAPI*)(HSTRING, IActivationFactory**);
    auto getFactory = reinterpret_cast<GetFactory>(GetProcAddress(GetModuleHandleW(L"appxportable.dll"), "ShimGetFactory"));
    if (!getFactory) return E_UNEXPECTED;
    ComPtr<IActivationFactory> factory;
    auto hr = getFactory(Wrappers::HStringReference(L"Windows.UI.Xaml.Window").Get(), &factory);
    ComPtr<IWindowStatics> statics;
    if (SUCCEEDED(hr)) hr = factory.As(&statics);
    ComPtr<IWindow> window;
    if (SUCCEEDED(hr)) hr = statics->get_Current(&window);
    return FAILED(hr) ? hr : window->get_Content(value);
}
extern "C" HRESULT __cdecl ConnectWindowContent(INT32 id) {
    ComPtr<ABI::Windows::UI::Xaml::IUIElement> content;
    auto hr = WindowContent(&content);
    ComPtr<IInspectable> target;
    if (SUCCEEDED(hr) && content) hr = content.As(&target);
    return FAILED(hr) ? hr : InvokeComponentConnector(content.Get(), id, target.Get());
}
extern "C" HRESULT __cdecl InvokeWindowKeyDown(boolean withArgs) {
    ComPtr<ABI::Windows::UI::Xaml::IUIElement> content;
    auto hr = WindowContent(&content);
    return FAILED(hr) ? hr : InvokeKeyDown(content.Get(), withArgs);
}

// The Window holds a Frame; its ContentControl.Content must be the managed Page's controlling outer.
extern "C" HRESULT __cdecl ConnectFrameContent(INT32 id) {
    ComPtr<ABI::Windows::UI::Xaml::IUIElement> root;
    auto hr = WindowContent(&root);
    ComPtr<ABI::Windows::UI::Xaml::Controls::IContentControl> frame;
    if (SUCCEEDED(hr)) hr = root.As(&frame);
    ComPtr<IInspectable> content;
    if (SUCCEEDED(hr)) hr = frame->get_Content(&content);
    if (SUCCEEDED(hr) && !content) hr = E_FAIL;
    return FAILED(hr) ? hr : InvokeComponentConnector(content.Get(), id, content.Get());
}

extern "C" HRESULT __cdecl ConnectNavigationArgs(IUnknown* page, INT32 id) {
    auto args = Make<TestNavigationArgs>();
    return args ? InvokeComponentConnector(page, id, args.Get()) : E_OUTOFMEMORY;
}

class NamelessTarget final : public RuntimeClass<RuntimeClassFlags<WinRt>, ABI::Windows::Foundation::IStringable> {
    HRESULT nameResult_;
public:
    explicit NamelessTarget(HRESULT result) : nameResult_(result) {}
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* value) noexcept override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return nameResult_;
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* value) noexcept override {
        if (!value) return E_POINTER;
        *value = BaseTrust;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ToString(HSTRING* value) override { return WindowsCreateString(L"nameless", 8, value); }
};
extern "C" HRESULT __cdecl ConnectNamelessTarget(IUnknown* page, INT32 id, HRESULT nameResult) {
    auto target = Make<NamelessTarget>(nameResult);
    return target ? InvokeComponentConnector(page, id, target.Get()) : E_OUTOFMEMORY;
}
extern "C" HRESULT __cdecl CheckNamelessTarget(IInspectable* target) {
    if (!target) return E_POINTER;
    ComPtr<ABI::Windows::Foundation::IStringable> stringable;
    auto hr = target->QueryInterface(IID_PPV_ARGS(&stringable));
    Wrappers::HString text;
    if (SUCCEEDED(hr)) hr = stringable->ToString(text.GetAddressOf());
    return FAILED(hr) ? hr : wcscmp(text.GetRawBuffer(nullptr), L"nameless") == 0 ? S_OK : E_FAIL;
}

extern "C" HRESULT __cdecl ValidateLoadedSource() {
    using namespace ABI::Windows::UI::Xaml;
    using namespace ABI::Windows::UI::Xaml::Controls;
    ComPtr<IUIElement> root;
    auto hr = WindowContent(&root);
    if (FAILED(hr)) return hr;
    ComPtr<IFrameworkElement> element;
    if (FAILED(root.As(&element))) return E_FAIL;
    double width = 0, height = 0;
    if (FAILED(element->get_Width(&width)) || width != 321 || FAILED(element->get_Height(&height)) || height != 123)
        return E_FAIL;
    ComPtr<IUserControl> page;
    if (FAILED(root.As(&page))) return E_FAIL;
    ComPtr<IUIElement> frame;
    if (FAILED(page->get_Content(&frame)) || !frame) return E_FAIL;
    if (FAILED(frame.As(&element)) || FAILED(element->get_Width(&width)) || width != 80.5) return E_FAIL;
    ComPtr<IInspectable> namedRoot;
    if (FAILED(element->FindName(Wrappers::HStringReference(L"root").Get(), &namedRoot)) || !namedRoot) return E_FAIL;
    ComPtr<IUnknown> rootIdentity, foundIdentity;
    if (FAILED(root.As(&rootIdentity)) || FAILED(namedRoot.As(&foundIdentity)) || rootIdentity != foundIdentity)
        return E_FAIL;
    Visibility visibility;
    if (FAILED(frame->get_Visibility(&visibility)) || visibility != Visibility_Collapsed) return E_FAIL;
    ComPtr<IContentControl> frameContent;
    if (FAILED(frame.As(&frameContent))) return E_FAIL;
    ComPtr<IInspectable> inner;
    if (FAILED(frameContent->get_Content(&inner)) || !inner || FAILED(inner.As(&page))) return E_FAIL;
    ComPtr<IUIElement> leaf;
    if (FAILED(page->get_Content(&leaf)) || !leaf) return E_FAIL;
    ComPtr<IGrid> grid;
    if (FAILED(leaf.As(&grid)) || FAILED(leaf.As(&element))) return E_FAIL;
    if (FAILED(element->get_Width(&width)) || width != 42) return E_FAIL;
    return S_OK;
}

#include "resource_projection_fixture.h"

#include "animation_projection_fixture.h"

#include "clock_projection_fixture.h"

#include "grid_projection_fixture.h"

#include "brush_projection_fixture.h"

#include "image_projection_fixture.h"

#include "progress_projection_fixture.h"

#include "text_projection_fixture.h"

#include "viewbox_projection_fixture.h"

#include "window_size_projection_fixture.h"

#include "application_view_projection_fixture.h"

#include "visibility_projection_fixture.h"

// Drives a managed app's XAML metadata CCWs through their SDK ABI. Each failed
// check returns its own code so the managed caller can name the step.
extern "C" HRESULT __cdecl ExerciseXamlMetadata() {
    using namespace ABI::Windows::UI::Xaml;
    using namespace ABI::Windows::UI::Xaml::Markup;
    using namespace ABI::Windows::UI::Xaml::Interop;
    using GetFactory = HRESULT (WINAPI*)(HSTRING, IActivationFactory**);
    auto getFactory = reinterpret_cast<GetFactory>(GetProcAddress(GetModuleHandleW(L"appxportable.dll"), "ShimGetFactory"));
    if (!getFactory) return E_UNEXPECTED;
    ComPtr<IActivationFactory> activation;
    auto hr = getFactory(Wrappers::HStringReference(L"Windows.UI.Xaml.Application").Get(), &activation);
    ComPtr<IApplicationStatics> statics;
    if (SUCCEEDED(hr)) hr = activation.As(&statics);
    ComPtr<IApplication> application;
    if (SUCCEEDED(hr)) hr = statics->get_Current(&application);
    ComPtr<IXamlMetadataProvider> provider;
    if (SUCCEEDED(hr)) hr = application.As(&provider);
    if (FAILED(hr)) return hr;
    auto step = [](int number) { return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x200 + number); };
    auto same = [](const wchar_t* expected, HSTRING value) {
        return wcscmp(expected, WindowsGetStringRawBuffer(value, nullptr)) == 0;
    };
    ComPtr<IXamlType> type;
    hr = provider->GetXamlTypeByFullName(Wrappers::HStringReference(L"DerivedPage").Get(), &type);
    if (FAILED(hr)) return hr;
    if (!type) return step(1);
    Wrappers::HString text;
    if (FAILED(hr = type->get_FullName(text.GetAddressOf()))) return hr;
    if (!same(L"DerivedPage", text.Get())) return step(2);
    TypeName underlying{};
    if (FAILED(hr = type->get_UnderlyingType(&underlying))) return hr;
    bool custom = underlying.Kind == TypeKind_Custom && wcsncmp(WindowsGetStringRawBuffer(underlying.Name, nullptr), L"DerivedPage, ", 13) == 0;
    WindowsDeleteString(underlying.Name);
    if (!custom) return step(3);
    boolean flag = false;
    if (FAILED(hr = type->get_IsConstructible(&flag))) return hr;
    if (!flag) return step(4);
    if (FAILED(hr = type->get_IsBindable(&flag))) return hr;
    if (!flag) return step(5);
    for (auto getter : {&IXamlType::get_IsArray, &IXamlType::get_IsCollection, &IXamlType::get_IsDictionary,
                        &IXamlType::get_IsMarkupExtension}) {
        flag = true;
        if (FAILED(hr = (type.Get()->*getter)(&flag))) return hr;
        if (flag) return step(6);
    }
    ComPtr<IXamlType> none;
    if (FAILED(hr = type->get_BaseType(&none))) return hr;
    if (none) return step(7);
    if (FAILED(hr = type->get_ItemType(&none)) || FAILED(hr = type->get_KeyType(&none))) return hr;
    if (none) return step(8);
    ComPtr<IXamlMember> member;
    if (FAILED(hr = type->get_ContentProperty(&member))) return hr;
    if (!member) return step(9);
    if (FAILED(hr = member->get_Name(text.ReleaseAndGetAddressOf()))) return hr;
    if (!same(L"Content", text.Get())) return step(10);
    if (FAILED(hr = member->get_IsReadOnly(&flag))) return hr;
    if (!flag) return step(11);
    if (FAILED(hr = member->get_IsAttachable(&flag))) return hr;
    if (flag) return step(12);
    if (FAILED(hr = member->get_IsDependencyProperty(&flag))) return hr;
    if (flag) return step(13);
    ComPtr<IXamlType> owner;
    if (FAILED(hr = member->get_TargetType(&owner))) return hr;
    ComPtr<IUnknown> left, right;
    type.As(&left);
    if (owner) owner.As(&right);
    if (left != right) return step(14);
    if (FAILED(hr = member->get_Type(&none))) return hr;
    if (none) return step(15);
    ComPtr<IXamlMember> again, missing;
    if (FAILED(hr = type->GetMember(Wrappers::HStringReference(L"Content").Get(), &again))) return hr;
    if (again != member) return step(16);
    if (FAILED(hr = type->GetMember(Wrappers::HStringReference(L"Missing").Get(), &missing))) return hr;
    if (missing) return step(17);
    if (FAILED(hr = type->RunInitializer())) return hr;
    ComPtr<IInspectable> instance;
    if (FAILED(hr = type->ActivateInstance(&instance))) return hr;
    // Under Wine, WPF backs managed pages: the instance is the managed page's CCW.
    if (!instance) return step(18);
    ComPtr<IInspectable> created, value;
    if (type->CreateFromString(Wrappers::HStringReference(L"x").Get(), &created) != E_NOTIMPL) return step(19);
    if (type->AddToVector(instance.Get(), instance.Get()) != E_NOTIMPL) return step(20);
    if (type->AddToMap(instance.Get(), instance.Get(), instance.Get()) != E_NOTIMPL) return step(21);
    if (member->GetValue(instance.Get(), &value) != E_NOTIMPL) return step(22);
    if (member->SetValue(instance.Get(), instance.Get()) != E_NOTIMPL) return step(23);
    UINT32 count = 1;
    XmlnsDefinition* definitions = reinterpret_cast<XmlnsDefinition*>(1);
    if (provider->GetXmlnsDefinitions(&count, &definitions) != E_NOTIMPL || count || definitions) return step(24);
    if (FAILED(hr = provider->GetXamlTypeByFullName(Wrappers::HStringReference(L"Missing").Get(), &none))) return hr;
    if (none) return step(25);
    // Primitive, Metadata and Custom TypeName kinds reach the managed provider as CLR types.
    TypeName names[] = {{Wrappers::HStringReference(L"Int32").Get(), TypeKind_Primitive},
                        {Wrappers::HStringReference(L"Windows.UI.Xaml.Controls.Page").Get(), TypeKind_Metadata},
                        {Wrappers::HStringReference(L"Missing").Get(), TypeKind_Custom}};
    for (auto& name : names) {
        if (FAILED(hr = provider->GetXamlType(name, &none))) return hr;
        if (none) return step(26);
    }
    return S_OK;
}
