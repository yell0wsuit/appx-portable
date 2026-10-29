// Dependency properties through the same WinRT factories and interfaces games use.
#include <windows.h>
#undef GetCurrentTime
#include <roapi.h>
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.controls.h>
#include <wrl.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <thread>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Interop;
using namespace ABI::Windows::Foundation;
static int failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)
static bool Same(IUnknown* a, IUnknown* b) {
    ComPtr<IUnknown> left, right;
    if (a) a->QueryInterface(IID_PPV_ARGS(&left));
    if (b) b->QueryInterface(IID_PPV_ARGS(&right));
    return left.Get() == right.Get();
}
int main() {
    RoInitialize(RO_INIT_SINGLETHREADED);
    auto dll = LoadLibraryW(L"appxportable.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory, metadataFactory, frameFactory;
    auto hr = get(HStringReference(L"Windows.UI.Xaml.DependencyProperty").Get(), &factory);
    CHECK(SUCCEEDED(hr));
    if (!factory) {
        printf("dependencytest: DependencyProperty factory missing hr=%08lx\n", hr);
        return 1;
    }
    ComPtr<IDependencyPropertyStatics> properties;
    CHECK(SUCCEEDED(factory.As(&properties)));
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.PropertyMetadata").Get(), &metadataFactory)));
    ComPtr<IPropertyMetadataFactory> metadata;
    CHECK(metadataFactory && SUCCEEDED(metadataFactory.As(&metadata)));
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Controls.Frame").Get(), &frameFactory)));
    ComPtr<IInspectable> object;
    CHECK(frameFactory && SUCCEEDED(frameFactory->ActivateInstance(&object)));
    ComPtr<IDependencyObject> element;
    CHECK(object && SUCCEEDED(object.As(&element)));
    ComPtr<IPropertyValueStatics> box;
    CHECK(SUCCEEDED(
        RoGetActivationFactory(HStringReference(L"Windows.Foundation.PropertyValue").Get(), IID_PPV_ARGS(&box))));
    if (!metadata || !properties || !element || !box) return 1;
    ComPtr<IInspectable> initial, next, equal, number, unset, unsetAgain;
    CHECK(SUCCEEDED(box->CreateString(HStringReference(L"initial").Get(), &initial)));
    CHECK(SUCCEEDED(box->CreateString(HStringReference(L"next").Get(), &next)));
    CHECK(SUCCEEDED(box->CreateString(HStringReference(L"next").Get(), &equal)));
    CHECK(SUCCEEDED(box->CreateInt32(42, &number)));
    CHECK(SUCCEEDED(properties->get_UnsetValue(&unset)) && unset);
    CHECK(SUCCEEDED(properties->get_UnsetValue(&unsetAgain)) && Same(unset.Get(), unsetAgain.Get()));
    CHECK(properties->get_UnsetValue(nullptr) == E_POINTER);
    unsigned changes = 0;
    IDependencyObject* expectedSender = element.Get();
    ComPtr<IDependencyProperty> property;
    ComPtr<IInspectable> old, changed;
    auto callback = Callback<IPropertyChangedCallback>(
        [&](IDependencyObject* sender, IDependencyPropertyChangedEventArgs* args) -> HRESULT {
            CHECK(Same(sender, expectedSender));
            ComPtr<IDependencyProperty> reported;
            CHECK(SUCCEEDED(args->get_Property(&reported)) && Same(reported.Get(), property.Get()));
            old.Reset();
            changed.Reset();
            CHECK(SUCCEEDED(args->get_OldValue(&old)) && SUCCEEDED(args->get_NewValue(&changed)));
            ++changes;
            return S_OK;
        });
    ComPtr<IPropertyMetadata> info;
    CHECK(SUCCEEDED(
        metadata->CreateInstanceWithDefaultValueAndCallback(initial.Get(), callback.Get(), nullptr, nullptr, &info)));
    HStringReference typeName(L"String"), ownerName(L"Tests.Owner");
    TypeName type{typeName.Get(), TypeKind_Primitive};
    TypeName owner{ownerName.Get(), TypeKind_Custom};
    CHECK(
        SUCCEEDED(properties->RegisterAttached(HStringReference(L"Label").Get(), type, owner, info.Get(), &property)));
    if (!property) return 1;
    ComPtr<IPropertyMetadata> found;
    CHECK(SUCCEEDED(property->GetMetadata(owner, &found)) && Same(info.Get(), found.Get()));
    ComPtr<IDependencyProperty> duplicate;
    CHECK(FAILED(properties->Register(HStringReference(L"Label").Get(), type, owner, info.Get(), &duplicate)) &&
          !duplicate);
    ComPtr<IInspectable> value;
    CHECK(SUCCEEDED(element->GetValue(property.Get(), &value)) && Same(initial.Get(), value.Get()));
    value.Reset();
    CHECK(SUCCEEDED(element->ReadLocalValue(property.Get(), &value)) && Same(unset.Get(), value.Get()));
    CHECK(SUCCEEDED(element->SetValue(property.Get(), next.Get())) && changes == 1);
    CHECK(Same(old.Get(), initial.Get()) && Same(changed.Get(), next.Get()));
    CHECK(SUCCEEDED(element->SetValue(property.Get(), equal.Get())) && changes == 1);
    CHECK(element->SetValue(property.Get(), number.Get()) == E_INVALIDARG && changes == 1);
    value.Reset();
    CHECK(SUCCEEDED(element->GetValue(property.Get(), &value)) && Same(equal.Get(), value.Get()));
    CHECK(SUCCEEDED(element->SetValue(property.Get(), nullptr)) && changes == 2);
    value.Reset();
    CHECK(SUCCEEDED(element->ReadLocalValue(property.Get(), &value)) && !value);
    CHECK(SUCCEEDED(element->ClearValue(property.Get())) && changes == 3 && Same(changed.Get(), initial.Get()));
    CHECK(SUCCEEDED(element->ClearValue(property.Get())) && changes == 3);
    CHECK(SUCCEEDED(element->SetValue(property.Get(), next.Get())) && changes == 4);
    CHECK(SUCCEEDED(element->SetValue(property.Get(), unset.Get())) && changes == 5);
    CHECK(element->GetValue(nullptr, &value) == E_INVALIDARG);
    CHECK(element->GetValue(property.Get(), nullptr) == E_POINTER);
    // Null metadata is a real default, not the UnsetValue sentinel.
    ComPtr<IDependencyProperty> nullDefault;
    CHECK(SUCCEEDED(
        properties->RegisterAttached(HStringReference(L"NoDefault").Get(), type, owner, nullptr, &nullDefault)));
    value.Reset();
    CHECK(SUCCEEDED(element->GetValue(nullDefault.Get(), &value)) && !value);
    // Each object owns its own local-value store.
    ComPtr<IInspectable> another;
    ComPtr<IDependencyObject> other;
    CHECK(SUCCEEDED(frameFactory->ActivateInstance(&another)) && SUCCEEDED(another.As(&other)));
    expectedSender = other.Get();
    CHECK(SUCCEEDED(other->SetValue(property.Get(), next.Get())));
    value.Reset();
    CHECK(SUCCEEDED(element->GetValue(property.Get(), &value)) && Same(value.Get(), initial.Get()));
    // The store is UI-thread-affine even when callers hold a raw COM reference.
    HRESULT wrongGet = S_OK, wrongSet = S_OK;
    std::thread worker([&] {
        ComPtr<IInspectable> foreign;
        wrongGet = element->GetValue(property.Get(), &foreign);
        wrongSet = element->SetValue(property.Get(), nullptr);
    });
    worker.join();
    CHECK(wrongGet == RPC_E_WRONG_THREAD && wrongSet == RPC_E_WRONG_THREAD);
    HStringReference numberName(L"Int32");
    TypeName integer{numberName.Get(), TypeKind_Primitive};
    ComPtr<IDependencyProperty> numeric;
    CHECK(SUCCEEDED(properties->Register(HStringReference(L"Counter").Get(), integer, owner, nullptr, &numeric)));
    value.Reset();
    CHECK(SUCCEEDED(element->GetValue(numeric.Get(), &value)));
    ComPtr<IPropertyValue> zero;
    INT32 integerValue = -1;
    CHECK(value && SUCCEEDED(value.As(&zero)) && SUCCEEDED(zero->GetInt32(&integerValue)) && integerValue == 0);
    CHECK(element->SetValue(numeric.Get(), nullptr) == E_INVALIDARG);
    CHECK(SUCCEEDED(element->SetValue(numeric.Get(), number.Get())));
    number.Reset();
    value.Reset();
    zero.Reset();
    CHECK(SUCCEEDED(element->GetValue(numeric.Get(), &value)) && SUCCEEDED(value.As(&zero)) &&
          SUCCEEDED(zero->GetInt32(&integerValue)) && integerValue == 42);
    // Reentrant mutation reports each transition, preserving the outer arguments.
    ComPtr<IDependencyProperty> reentrant;
    unsigned nested = 0;
    auto reenter = Callback<IPropertyChangedCallback>(
        [&](IDependencyObject* sender, IDependencyPropertyChangedEventArgs* args) -> HRESULT {
            ++nested;
            ComPtr<IInspectable> newValue;
            CHECK(SUCCEEDED(args->get_NewValue(&newValue)));
            if (Same(newValue.Get(), next.Get())) {
                CHECK(SUCCEEDED(sender->ClearValue(reentrant.Get())));
                ComPtr<IInspectable> after;
                CHECK(SUCCEEDED(args->get_NewValue(&after)) && Same(after.Get(), next.Get()));
            }
            return S_OK;
        });
    ComPtr<IPropertyMetadata> reentrantInfo;
    CHECK(SUCCEEDED(metadata->CreateInstanceWithDefaultValueAndCallback(initial.Get(), reenter.Get(), nullptr, nullptr,
                                                                        &reentrantInfo)));
    CHECK(SUCCEEDED(properties->RegisterAttached(HStringReference(L"Reentrant").Get(), type, owner, reentrantInfo.Get(),
                                                 &reentrant)));
    CHECK(SUCCEEDED(element->SetValue(reentrant.Get(), next.Get())) && nested == 2);
    value.Reset();
    CHECK(SUCCEEDED(element->GetValue(reentrant.Get(), &value)) && Same(value.Get(), initial.Get()));
    auto failing = Callback<IPropertyChangedCallback>(
        [](IDependencyObject*, IDependencyPropertyChangedEventArgs*) -> HRESULT { return E_ABORT; });
    ComPtr<IPropertyMetadata> failingInfo;
    CHECK(SUCCEEDED(metadata->CreateInstanceWithDefaultValueAndCallback(initial.Get(), failing.Get(), nullptr, nullptr,
                                                                        &failingInfo)));
    ComPtr<IDependencyProperty> fails;
    CHECK(SUCCEEDED(
        properties->RegisterAttached(HStringReference(L"Failing").Get(), type, owner, failingInfo.Get(), &fails)));
    CHECK(element->SetValue(fails.Get(), next.Get()) == E_ABORT);
    value.Reset();
    CHECK(SUCCEEDED(element->GetValue(fails.Get(), &value)) && Same(value.Get(), next.Get()));
    printf("dependencytest: %d failures\n", failures);
    return failures ? 1 : 0;
}
