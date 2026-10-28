#include "xbf_loader.h"
#include "package/package.h"
#include "xaml/xbf_webview.h"
#include "log.h"
#include <psapi.h>
#include <cstring>
#include <string>
namespace appx {
namespace {
using MapCall = LPVOID(WINAPI*)(HANDLE, DWORD, DWORD, DWORD, SIZE_T);
MapCall realMap = &MapViewOfFile;
std::wstring PackageNtDirectory() {
    const auto& directory = CurrentPackage().directory;
    HANDLE handle =
        CreateFileW(directory.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    std::wstring path(32768, L'\0');
    const auto length = GetFinalPathNameByHandleW(handle, path.data(), static_cast<DWORD>(path.size()), VOLUME_NAME_NT);
    CloseHandle(handle);
    if (!length || length >= path.size()) return {};
    path.resize(length);
    while (!path.empty() && path.back() == L'\\') path.pop_back();
    return path;
}
bool PackageView(const void* view, size_t size) {
    static const auto directory = PackageNtDirectory();
    if (directory.empty()) return false;
    std::wstring path(32768, L'\0');
    const auto length =
        GetMappedFileNameW(GetCurrentProcess(), const_cast<void*>(view), path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return false;
    path.resize(length);
    if (path.size() <= directory.size() || _wcsnicmp(path.c_str(), directory.c_str(), directory.size()) != 0 ||
        path[directory.size()] != L'\\')
        return false;
    if (path.size() < 4 || _wcsicmp(path.c_str() + path.size() - 4, L".xbf") != 0) return false;
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    const auto absolute = L"\\\\?\\GLOBALROOT" + path;
    if (!GetFileAttributesExW(absolute.c_str(), GetFileExInfoStandard, &attributes)) return false;
    return attributes.nFileSizeHigh == 0 && attributes.nFileSizeLow == size;
}
LPVOID WINAPI Map(HANDLE mapping, DWORD access, DWORD high, DWORD low, SIZE_T count) noexcept {
    void* original = realMap(mapping, access, high, low, count);
    const DWORD error = GetLastError();
    if (!original || high || low || access != FILE_MAP_READ) {
        SetLastError(error);
        return original;
    }
    try {
        MEMORY_BASIC_INFORMATION region{};
        UINT32 header[3]{};
        if (!VirtualQuery(original, &region, sizeof(region)) || region.RegionSize < sizeof(header) ||
            region.State != MEM_COMMIT || (region.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
            SetLastError(error);
            return original;
        }
        memcpy(header, original, sizeof(header));
        const UINT64 size = 12ull + header[1] + header[2];
        if (header[0] != 0x00464258 || size > region.RegionSize || size > 64 * 1024 * 1024 ||
            (count && size != count) || !PackageView(original, static_cast<size_t>(size))) {
            SetLastError(error);
            return original;
        }
        std::vector<BYTE> adapted;
        const auto hr = RemapXbfWebViews(static_cast<const BYTE*>(original), static_cast<size_t>(size), adapted);
        if (hr != S_OK) {
            if (FAILED(hr)) Log("FAIL", "XBF WebView adaptation failed hr=%08lx", hr);
            SetLastError(error);
            return original;
        }
        HANDLE shadow = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                           static_cast<DWORD>(adapted.size()), nullptr);
        void* view = shadow ? realMap(shadow, FILE_MAP_WRITE, 0, 0, adapted.size()) : nullptr;
        if (shadow) CloseHandle(shadow);
        if (!view) {
            Log("FAIL", "XBF WebView memory view failed error=%lu", GetLastError());
            SetLastError(error);
            return original;
        }
        memcpy(view, adapted.data(), adapted.size());
        DWORD protection;
        if (!VirtualProtect(view, adapted.size(), PAGE_READONLY, &protection)) {
            UnmapViewOfFile(view);
            SetLastError(error);
            return original;
        }
        UnmapViewOfFile(original);
        Log("TRACE", "XBF WebView metadata adapted in memory (%zu bytes)", adapted.size());
        SetLastError(error);
        return view;
    } catch (...) {
        Log("FAIL", "XBF WebView memory adaptation allocation failed");
        SetLastError(error);
        return original;
    }
}
}
HRESULT InstallIslandXbfLoader() {
    static bool installed = false;
    if (installed) return S_OK;
    HMODULE module = GetModuleHandleW(L"Windows.UI.Xaml.dll");
    if (!module) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    auto* base = reinterpret_cast<BYTE*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) return E_NOTIMPL;
    auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; imports->Name; ++imports) {
        if (!imports->OriginalFirstThunk) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* symbol = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (strcmp(reinterpret_cast<char*>(symbol->Name), "MapViewOfFile") != 0) continue;
            DWORD old;
            if (!VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old))
                return HRESULT_FROM_WIN32(GetLastError());
            realMap = reinterpret_cast<MapCall>(slots->u1.Function);
            InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&slots->u1.Function),
                                       reinterpret_cast<void*>(&Map));
            VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
            installed = true;
            return S_OK;
        }
    }
    return E_NOTIMPL;
}
}
