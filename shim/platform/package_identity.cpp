// Windows.ApplicationModel.Package.Current, which fails in a process without
// package identity: answered from the game's AppxManifest.xml.
#include "generated/package.h"
#include "com/factory.h"
#include "package/package.h"
#include "platform/storage.h"
#include "registry.h"
using namespace Microsoft::WRL;
using namespace ABI::Windows::ApplicationModel;
namespace appx {
namespace {
HRESULT Text(const std::wstring& text, HSTRING* value) {
    if (!value) return E_POINTER;
    return WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), value);
}
class Id final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIPackageId, FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.PackageId", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* value) override { return Text(CurrentPackage().identityName, value); }
    HRESULT STDMETHODCALLTYPE get_Version(PackageVersion* value) override {
        if (!value) return E_POINTER;
        *value = {};
        swscanf_s(CurrentPackage().version.c_str(), L"%hu.%hu.%hu.%hu", &value->Major, &value->Minor, &value->Build,
                  &value->Revision);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Architecture(ABI::Windows::System::ProcessorArchitecture* value) override {
        if (!value) return E_POINTER;
        const auto& architecture = CurrentPackage().architecture;
        *value = architecture == L"x86"   ? ABI::Windows::System::ProcessorArchitecture_X86
                 : architecture == L"x64" ? ABI::Windows::System::ProcessorArchitecture_X64
                 : architecture == L"arm" ? ABI::Windows::System::ProcessorArchitecture_Arm
                                          : ABI::Windows::System::ProcessorArchitecture_Neutral;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ResourceId(HSTRING* value) override { return Text(L"", value); }
    HRESULT STDMETHODCALLTYPE get_Publisher(HSTRING* value) override { return Text(CurrentPackage().publisher, value); }
    HRESULT STDMETHODCALLTYPE get_PublisherId(HSTRING* value) override {
        return Text(CurrentPackage().publisherId, value);
    }
    HRESULT STDMETHODCALLTYPE get_FullName(HSTRING* value) override { return Text(CurrentPackage().fullName, value); }
    HRESULT STDMETHODCALLTYPE get_FamilyName(HSTRING* value) override {
        return Text(CurrentPackage().familyName, value);
    }
};
class Package final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIPackage, FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.Package", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE get_Id(IPackageId** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto id = Make<Id>();
        return id ? id.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_InstalledLocation(ABI::Windows::Storage::IStorageFolder** value) override {
        return MakeStorageFolder(CurrentPackage().directory.wstring(), value);
    }
    HRESULT STDMETHODCALLTYPE get_IsFramework(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
};
class PackageFactory final : public Factory<IPackageStatics> {
public:
    PackageFactory() : Factory(L"Windows.ApplicationModel.Package") {}
    HRESULT STDMETHODCALLTYPE get_Current(IPackage** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        static auto package = Make<Package>();
        return package ? package.CopyTo(value) : E_OUTOFMEMORY;
    }
};
}
HRESULT MakePackageFactory(IActivationFactory** value) {
    static auto factory = Make<PackageFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
const ShimClass packageClass{L"Windows.ApplicationModel.Package", MakePackageFactory, ShimPolicy::Replace, InIslands};
}
