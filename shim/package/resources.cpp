#include "resources.h"
#include <shlwapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <new>
using Microsoft::WRL::ComPtr;
namespace appx {
namespace {
// Native MRT COM contracts. IIDs and method order were verified against the
// matching Microsoft MrmCoreR public symbols; these are not WinRT interfaces.
// Unsupported systems fail through CoCreateInstance/QueryInterface.
MIDL_INTERFACE("130A2F65-2BE7-4309-9A58-A9052FF2B61C") PriManager : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Initialize() = 0;
    virtual HRESULT STDMETHODCALLTYPE InitializeForCurrentApplication() = 0;
    virtual HRESULT STDMETHODCALLTYPE InitializeForPackage(PCWSTR name) = 0;
    virtual HRESULT STDMETHODCALLTYPE InitializeForFile(PCWSTR path) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMainResourceMap(REFIID iid, void** value) = 0;
};
MIDL_INTERFACE("6E21E72B-B9B0-42AE-A686-983CF784EDCD") PriMap : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetUri(PWSTR * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSubtree(PCWSTR name, PriMap * *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetString(PCWSTR name, PWSTR * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStringForContext(IUnknown * context, PCWSTR name, PWSTR * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFilePath(PCWSTR name, PWSTR * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFilePathForContext(IUnknown * context, PCWSTR name, PWSTR * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNamedResourceCount(UINT32 * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNamedResourceUri(UINT32 index, PWSTR * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNamedResource(PCWSTR name, REFIID iid, void** value) = 0;
};
MIDL_INTERFACE("C2AC6F97-54F2-445A-8F62-D87B9537F9B2") PriNamedResource : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetUri(PWSTR * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCandidateCount(UINT32 * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCandidate(UINT32 index, IUnknown * *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Resolve(IUnknown * *value) = 0;
};
MIDL_INTERFACE("8C09FA9C-1846-4E32-B36C-93E2B94395CF") PriCandidateStream : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE ToStream(IStream * *value) = 0;
};
constexpr CLSID ResourceManagerClass{0xdbce7e40, 0x7345, 0x439d, {0xb1, 0x2c, 0x11, 0x4a, 0x11, 0x81, 0x9a, 0x09}};
HRESULT Missing(HRESULT hr) {
    return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND) ||
                   hr == HRESULT_FROM_WIN32(ERROR_MRM_NAMED_RESOURCE_NOT_FOUND)
               ? S_FALSE
               : hr;
}
}
HRESULT OpenPackageResource(const PackageInfo& package, const std::wstring& relative, IStream** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    auto name = relative;
    std::replace(name.begin(), name.end(), L'\\', L'/');
    if (name.empty() || name.front() == L'/' || name.find(L':') != std::wstring::npos) return E_INVALIDARG;
    for (size_t start = 0; start < name.size();) {
        auto end = name.find(L'/', start);
        auto part = name.substr(start, end == std::wstring::npos ? end : end - start);
        if (part.empty() || part == L"." || part == L"..") return E_INVALIDARG;
        if (end == std::wstring::npos) break;
        start = end + 1;
        if (start == name.size()) return E_INVALIDARG;
    }
    const auto path = package.directory / relative;
    auto hr = SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, value);
    if (Missing(hr) != S_FALSE) return hr;
    auto pri = package.directory / L"resources.pri";
    // Unlike Win32 file opens, MRT does not resolve slash-separated paths.
    pri.make_preferred();
    if (GetFileAttributesW(pri.c_str()) == INVALID_FILE_ATTRIBUTES) return S_FALSE;
    ComPtr<PriManager> manager;
    ComPtr<PriMap> map;
    ComPtr<PriNamedResource> resource;
    ComPtr<IUnknown> candidate;
    ComPtr<PriCandidateStream> stream;
    hr = CoCreateInstance(ResourceManagerClass, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager));
    if (SUCCEEDED(hr)) hr = manager->InitializeForFile(pri.c_str());
    if (SUCCEEDED(hr)) hr = manager->GetMainResourceMap(IID_PPV_ARGS(&map));
    const auto key = L"Files/" + name;
    if (SUCCEEDED(hr)) hr = map->GetNamedResource(key.c_str(), IID_PPV_ARGS(&resource));
    if (SUCCEEDED(hr)) hr = resource->Resolve(&candidate);
    if (SUCCEEDED(hr)) hr = candidate.As(&stream);
    ComPtr<IStream> borrowed;
    if (SUCCEEDED(hr)) hr = stream->ToStream(&borrowed);
    if (SUCCEEDED(hr)) {
        // MRT's stream borrows the mapped PRI bytes. Copy them before the
        // manager/candidate are released so callers receive an owning stream.
        STATSTG stat{};
        hr = borrowed->Stat(&stat, STATFLAG_NONAME);
        if (SUCCEEDED(hr) && stat.cbSize.QuadPart > 64 * 1024 * 1024) hr = E_NOTIMPL;
        if (SUCCEEDED(hr)) {
            try {
                std::vector<BYTE> bytes(static_cast<size_t>(stat.cbSize.QuadPart));
                ULONG read = 0;
                hr = borrowed->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read);
                if (SUCCEEDED(hr) && read != bytes.size()) hr = HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
                if (SUCCEEDED(hr)) {
                    *value = SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size()));
                    if (!*value) hr = E_OUTOFMEMORY;
                }
            } catch (const std::bad_alloc&) {
                hr = E_OUTOFMEMORY;
            }
        }
    }
    return Missing(hr);
}
}
