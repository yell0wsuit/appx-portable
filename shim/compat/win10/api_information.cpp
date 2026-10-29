#include "registry.h"
#include "generated/win10_metadata.h"
#include "com/factory.h"
#include <wrl/wrappers/corewrappers.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Foundation::Metadata;
namespace appx {
namespace {
// Windows answers from its metadata; Wine's wintypes stubs these queries with
// E_NOTIMPL, which C++/CX games throw as NotImplementedException. Only then
// does the shim answer: a type is present when it can be activated, and
// members and contracts are reported absent so games keep their basic path.
class ApiInformationFactory final : public Factory<DefaultIApiInformationStatics> {
    ComPtr<IApiInformationStatics> os_;

    static bool Fallback(HRESULT hr) noexcept { return hr == E_NOTIMPL; }
    static HRESULT Absent(const char* query, HSTRING name, boolean* result) noexcept {
        Log("TRACE", "ApiInformation.%s %ls: not implemented by Windows; reporting absent", query,
            WindowsGetStringRawBuffer(name, nullptr));
        *result = false;
        return S_OK;
    }

public:
    ApiInformationFactory() : Factory(L"Windows.Foundation.Metadata.ApiInformation") {
        OsGetActivationFactory(Wrappers::HStringReference(L"Windows.Foundation.Metadata.ApiInformation").Get(),
                               IID_PPV_ARGS(&os_));
    }
    HRESULT STDMETHODCALLTYPE IsTypePresent(HSTRING typeName, boolean* result) override {
        if (!result) return E_POINTER;
        HRESULT hr = os_ ? os_->IsTypePresent(typeName, result) : E_NOTIMPL;
        if (!Fallback(hr)) return hr;
        const wchar_t* name = WindowsGetStringRawBuffer(typeName, nullptr);
        ComPtr<IActivationFactory> factory;
        *result = FindShimClass(name) || SUCCEEDED(OsGetActivationFactory(typeName, IID_PPV_ARGS(&factory)));
        Log("TRACE", "ApiInformation.IsTypePresent %ls: not implemented by Windows; activatable=%d", name, *result);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsMethodPresent(HSTRING typeName, HSTRING methodName, boolean* result) override {
        if (!result) return E_POINTER;
        HRESULT hr = os_ ? os_->IsMethodPresent(typeName, methodName, result) : E_NOTIMPL;
        return Fallback(hr) ? Absent("IsMethodPresent", typeName, result) : hr;
    }
    HRESULT STDMETHODCALLTYPE IsMethodPresentWithArity(HSTRING typeName, HSTRING methodName, UINT32 inputParameterCount,
                                                       boolean* result) override {
        if (!result) return E_POINTER;
        HRESULT hr = os_ ? os_->IsMethodPresentWithArity(typeName, methodName, inputParameterCount, result) : E_NOTIMPL;
        return Fallback(hr) ? Absent("IsMethodPresentWithArity", typeName, result) : hr;
    }
    HRESULT STDMETHODCALLTYPE IsEventPresent(HSTRING typeName, HSTRING eventName, boolean* result) override {
        if (!result) return E_POINTER;
        HRESULT hr = os_ ? os_->IsEventPresent(typeName, eventName, result) : E_NOTIMPL;
        return Fallback(hr) ? Absent("IsEventPresent", typeName, result) : hr;
    }
    HRESULT STDMETHODCALLTYPE IsPropertyPresent(HSTRING typeName, HSTRING propertyName, boolean* result) override {
        if (!result) return E_POINTER;
        HRESULT hr = os_ ? os_->IsPropertyPresent(typeName, propertyName, result) : E_NOTIMPL;
        return Fallback(hr) ? Absent("IsPropertyPresent", typeName, result) : hr;
    }
    HRESULT STDMETHODCALLTYPE IsReadOnlyPropertyPresent(HSTRING typeName, HSTRING propertyName,
                                                        boolean* result) override {
        if (!result) return E_POINTER;
        HRESULT hr = os_ ? os_->IsReadOnlyPropertyPresent(typeName, propertyName, result) : E_NOTIMPL;
        return Fallback(hr) ? Absent("IsReadOnlyPropertyPresent", typeName, result) : hr;
    }
    HRESULT STDMETHODCALLTYPE IsWriteablePropertyPresent(HSTRING typeName, HSTRING propertyName,
                                                         boolean* result) override {
        if (!result) return E_POINTER;
        HRESULT hr = os_ ? os_->IsWriteablePropertyPresent(typeName, propertyName, result) : E_NOTIMPL;
        return Fallback(hr) ? Absent("IsWriteablePropertyPresent", typeName, result) : hr;
    }
    HRESULT STDMETHODCALLTYPE IsEnumNamedValuePresent(HSTRING enumTypeName, HSTRING valueName,
                                                      boolean* result) override {
        if (!result) return E_POINTER;
        HRESULT hr = os_ ? os_->IsEnumNamedValuePresent(enumTypeName, valueName, result) : E_NOTIMPL;
        return Fallback(hr) ? Absent("IsEnumNamedValuePresent", enumTypeName, result) : hr;
    }
    HRESULT STDMETHODCALLTYPE IsApiContractPresentByMajor(HSTRING contractName, UINT16 majorVersion,
                                                          boolean* result) override {
        if (!result) return E_POINTER;
        HRESULT hr = os_ ? os_->IsApiContractPresentByMajor(contractName, majorVersion, result) : E_NOTIMPL;
        return Fallback(hr) ? Absent("IsApiContractPresentByMajor", contractName, result) : hr;
    }
    HRESULT STDMETHODCALLTYPE IsApiContractPresentByMajorAndMinor(HSTRING contractName, UINT16 majorVersion,
                                                                  UINT16 minorVersion, boolean* result) override {
        if (!result) return E_POINTER;
        HRESULT hr = os_ ? os_->IsApiContractPresentByMajorAndMinor(contractName, majorVersion, minorVersion, result)
                         : E_NOTIMPL;
        return Fallback(hr) ? Absent("IsApiContractPresentByMajorAndMinor", contractName, result) : hr;
    }
};
HRESULT MakeApiInformationFactory(IActivationFactory** value) {
    static auto factory = Make<ApiInformationFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
// Pure metadata queries: managed games in XAML Islands mode ask them too.
const ShimClass apiInformationClass{L"Windows.Foundation.Metadata.ApiInformation", MakeApiInformationFactory,
                                    ShimPolicy::Replace, InIslands};
}
