#include "package.h"
#include "log.h"
#include <bcrypt.h>
#include <shlwapi.h>
#include <xmllite.h>
#include <wrl/client.h>
#include <algorithm>
#include <string_view>
using Microsoft::WRL::ComPtr;
namespace appx {
std::wstring PublisherId(const std::wstring& publisher) {
    // Classic CNG calls: one-shot BCryptHash and pseudo-handles need Windows 10.
    BYTE hash[32]{};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE handle = nullptr;
    bool hashed =
        BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
        BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &handle, nullptr, 0, nullptr, 0, 0)) &&
        BCRYPT_SUCCESS(BCryptHashData(handle, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(publisher.data())),
                                      static_cast<ULONG>(publisher.size() * sizeof(wchar_t)), 0)) &&
        BCRYPT_SUCCESS(BCryptFinishHash(handle, hash, sizeof(hash), 0));
    if (handle) BCryptDestroyHash(handle);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!hashed) return {};
    static constexpr wchar_t alphabet[] = L"0123456789abcdefghjkmnpqrstvwxyz";
    unsigned long long bits = 0;
    for (int i = 0; i < 8; ++i) bits = bits << 8 | hash[i];
    // 64 bits make 12 full groups of five plus four bits, padded with a zero bit.
    std::wstring id;
    for (int i = 0; i < 12; ++i) id += alphabet[(bits >> (59 - 5 * i)) & 31];
    id += alphabet[(bits << 1) & 31];
    return id;
}
static std::wstring Attribute(IXmlReader* reader, const wchar_t* name) {
    if (FAILED(reader->MoveToAttributeByName(name, nullptr))) return {};
    const wchar_t* value = nullptr;
    reader->GetValue(&value, nullptr);
    std::wstring result = value ? value : L"";
    reader->MoveToElement();
    return result;
}
// Element text, for <DisplayName> and <Path>.
static std::wstring Text(IXmlReader* reader) {
    BOOL empty = reader->IsEmptyElement();
    std::wstring text;
    XmlNodeType type;
    while (!empty && reader->Read(&type) == S_OK && type != XmlNodeType_EndElement) {
        if (type != XmlNodeType_Text && type != XmlNodeType_CDATA) continue;
        const wchar_t* value = nullptr;
        reader->GetValue(&value, nullptr);
        if (value) text += value;
    }
    return text;
}
// A resource from the package's resources.pri, read in place as the shell reads
// an installed package's: a string's value, or a file candidate's full path.
// Empty when the package has no index or the index has no such resource.
static std::wstring LoadResource(const PackageInfo& info, const std::wstring& uri) {
    const auto index = info.directory / L"resources.pri";
    if (GetFileAttributesW(index.c_str()) == INVALID_FILE_ATTRIBUTES) return {};
    const auto source = L"@{" + index.wstring() + L"? " + uri + L"}";
    std::wstring value(32768, L'\0');
    if (FAILED(SHLoadIndirectString(source.c_str(), value.data(), static_cast<UINT>(value.size()), nullptr)))
        return {};
    value.resize(wcsnlen(value.c_str(), value.size()));
    return value;
}
// "ms-resource:AppName" names a string in the default Resources.resw map and
// "ms-resource:Other/AppName" may name one in Other.resw; "ms-resource:/path"
// and "ms-resource://package/path" are absolute. Other text is the name itself.
static std::wstring ResolveName(const PackageInfo& info, const std::wstring& name) {
    static constexpr std::wstring_view scheme = L"ms-resource:";
    if (name.rfind(scheme, 0) != 0) return name;
    const auto reference = name.substr(scheme.size());
    if (reference.rfind(L"//", 0) == 0) return LoadResource(info, name);
    const auto package = L"ms-resource://" + info.identityName;
    if (reference.rfind(L'/', 0) == 0) return LoadResource(info, package + reference);
    auto value = LoadResource(info, package + L"/resources/" + reference);
    if (value.empty() && reference.find(L'/') != std::wstring::npos)
        value = LoadResource(info, package + L"/" + reference);
    return value;
}
std::filesystem::path ResolvePackageFile(const PackageInfo& package, const std::wstring& relative) {
    auto name = relative;
    std::replace(name.begin(), name.end(), L'\\', L'/');
    const auto resolved = LoadResource(package, L"ms-resource://" + package.identityName + L"/Files/" + name);
    return resolved.empty() ? package.directory / relative : std::filesystem::path(resolved);
}
std::wstring ResolveDisplayName(const PackageInfo& package) {
    for (const auto* name : {&package.displayName, &package.tileName})
        if (!name->empty())
            if (auto value = ResolveName(package, *name); !value.empty()) return value;
    return {};
}
PackageInfo ReadPackage(const std::filesystem::path& directory) {
    PackageInfo info;
    info.directory = directory;
    ComPtr<IStream> stream;
    ComPtr<IXmlReader> reader;
    const auto manifest = directory / L"AppxManifest.xml";
    if (FAILED(
            SHCreateStreamOnFileEx(manifest.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &stream)) ||
        FAILED(CreateXmlReader(IID_PPV_ARGS(&reader), nullptr)) ||
        FAILED(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit)) ||
        FAILED(reader->SetInput(stream.Get())))
        return info;
    // Elements are matched by local name: Windows 8, 8.1 and 10 manifests use
    // different namespaces for the same elements.
    std::wstring serverModule;
    bool inProperties = false, inServer = false, firstApplication = true;
    XmlNodeType type;
    HRESULT hr;
    while ((hr = reader->Read(&type)) == S_OK) {
        const wchar_t* name = nullptr;
        if (type == XmlNodeType_EndElement) {
            reader->GetLocalName(&name, nullptr);
            if (!wcscmp(name, L"Properties")) inProperties = false;
            if (!wcscmp(name, L"InProcessServer")) inServer = false;
            continue;
        }
        if (type != XmlNodeType_Element) continue;
        reader->GetLocalName(&name, nullptr);
        if (!wcscmp(name, L"Identity")) {
            info.identityName = Attribute(reader.Get(), L"Name");
            info.publisher = Attribute(reader.Get(), L"Publisher");
            info.version = Attribute(reader.Get(), L"Version");
            info.architecture = Attribute(reader.Get(), L"ProcessorArchitecture");
            if (info.architecture.empty()) info.architecture = L"neutral";
        } else if (!wcscmp(name, L"TargetDeviceFamily")) {
            info.universal = true;
        } else if (!wcscmp(name, L"Properties")) {
            inProperties = !reader->IsEmptyElement();
        } else if (inProperties && !wcscmp(name, L"DisplayName")) {
            info.displayName = Text(reader.Get());
        } else if (!wcscmp(name, L"Application") && firstApplication) {
            firstApplication = false;
            info.applicationId = Attribute(reader.Get(), L"Id");
            info.executable = Attribute(reader.Get(), L"Executable");
            info.entryPoint = Attribute(reader.Get(), L"EntryPoint");
        } else if (!wcscmp(name, L"VisualElements") && info.smallLogo.empty()) {
            info.tileName = Attribute(reader.Get(), L"DisplayName");
            // Windows 8: SmallLogo; 8.1: Square30x30Logo; 10: Square44x44Logo.
            for (const auto* logo : {L"SmallLogo", L"Square30x30Logo", L"Square44x44Logo"})
                if (info.smallLogo.empty()) info.smallLogo = Attribute(reader.Get(), logo);
        } else if (!wcscmp(name, L"InProcessServer")) {
            inServer = !reader->IsEmptyElement();
            serverModule.clear();
        } else if (inServer && !wcscmp(name, L"Path")) {
            serverModule = Text(reader.Get());
        } else if (inServer && !wcscmp(name, L"ActivatableClass")) {
            auto id = Attribute(reader.Get(), L"ActivatableClassId");
            if (!id.empty() && !serverModule.empty()) info.classes.push_back({std::move(id), directory / serverModule});
        }
    }
    if (FAILED(hr)) Log("FAIL", "AppxManifest.xml parse error hr=%08lx; using defaults", hr);
    if (!info.publisher.empty()) {
        info.publisherId = PublisherId(info.publisher);
        info.familyName = info.identityName + L"_" + info.publisherId;
        info.fullName = info.identityName + L"_" + info.version + L"_" + info.architecture + L"__" + info.publisherId;
    }
    info.loaded = !info.identityName.empty();
    return info;
}
static std::filesystem::path ExecutablePath() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD length = 0;
    while ((length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()))) == path.size())
        path.resize(path.size() * 2);
    path.resize(length);
    return path;
}
const PackageInfo& CurrentPackage() noexcept {
    static const PackageInfo info = [] {
        PackageInfo result;
        try {
            result = ReadPackage(ExecutablePath().parent_path());
        } catch (...) {
            return result;
        }
        if (result.loaded)
            Log("INFO", "package %ls (%ls) app=%ls entry=%ls, %zu in-package classes", result.fullName.c_str(),
                result.displayName.c_str(), result.applicationId.c_str(), result.entryPoint.c_str(),
                result.classes.size());
        else
            Log("INFO", "no AppxManifest.xml beside the game; using defaults");
        return result;
    }();
    return info;
}
const std::wstring& PackageDisplayName() noexcept {
    static const std::wstring name = [] {
        try {
            if (auto value = ResolveDisplayName(CurrentPackage()); !value.empty()) return value;
            return ExecutablePath().stem().wstring();
        } catch (...) {
            return std::wstring(L"Game");
        }
    }();
    return name;
}
}
