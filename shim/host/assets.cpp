#include "assets.h"
#include <algorithm>
#include <new>
#include <vector>

namespace appx {
static int Hex(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}
std::optional<std::wstring> DecodePackageName(const std::wstring& name) {
    // Escapes are UTF-8 bytes; everything else is copied through as UTF-8 too.
    std::string bytes;
    bool escaped = false;
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == L'%' && i + 2 < name.size()) {
            const int high = Hex(name[i + 1]), low = Hex(name[i + 2]);
            if (high >= 0 && low >= 0) {
                bytes += static_cast<char>(high << 4 | low);
                escaped = true;
                i += 2;
                continue;
            }
        }
        char utf8[4];
        const int length = WideCharToMultiByte(CP_UTF8, 0, &name[i], 1, utf8, sizeof(utf8), nullptr, nullptr);
        if (length <= 0) return std::nullopt;
        bytes.append(utf8, length);
    }
    if (!escaped) return std::nullopt;
    const int length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (length <= 0) return std::nullopt;
    std::wstring decoded(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), decoded.data(),
                        length);
    if (decoded == L"." || decoded == L".." || decoded.find_first_of(L"<>:\"/\\|?*") != std::wstring::npos ||
        std::any_of(decoded.begin(), decoded.end(), [](wchar_t c) { return c < 32; }))
        return std::nullopt;
    return decoded;
}
HRESULT DecodePackageNames(const std::filesystem::path& gameDirectory, unsigned* count) noexcept {
    if (count) *count = 0;
    try {
        std::vector<std::filesystem::path> encoded;
        std::error_code error;
        if (!std::filesystem::is_directory(gameDirectory, error)) return S_OK; // nothing to decode
        std::filesystem::recursive_directory_iterator it(gameDirectory, error), end;
        for (; !error && it != end; it.increment(error)) {
            const auto& path = it->path();
            if (it.depth() == 0 && it->is_directory(error) && _wcsicmp(path.filename().c_str(), L"SaveData") == 0) {
                it.disable_recursion_pending();
                continue;
            }
            if (path.filename().wstring().find(L'%') != std::wstring::npos) encoded.push_back(path);
        }
        if (error) return HRESULT_FROM_WIN32(error.value());
        // Deepest first, so renaming a folder never invalidates a pending child.
        std::sort(encoded.begin(), encoded.end(), [](const auto& a, const auto& b) {
            return std::distance(a.begin(), a.end()) > std::distance(b.begin(), b.end());
        });
        for (const auto& path : encoded) {
            const auto decoded = DecodePackageName(path.filename().wstring());
            if (!decoded) continue;
            const auto target = path.parent_path() / *decoded;
            // MoveFile never overwrites a user's existing decoded file.
            if (!MoveFileW(path.c_str(), target.c_str())) {
                const DWORD failure = GetLastError();
                if (failure == ERROR_ALREADY_EXISTS || failure == ERROR_FILE_EXISTS) continue;
                return HRESULT_FROM_WIN32(failure);
            }
            if (count) ++*count;
        }
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}
}
