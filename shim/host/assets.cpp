#include "assets.h"
#include <wincodec.h>
#include <wrl/client.h>
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
// WIC decodes whichever image format the package used; GDI then needs the
// pixels as a 32-bit top-down DIB with straight alpha.
HICON LoadGameIcon(const std::filesystem::path& path, int width, int height) noexcept {
    using Microsoft::WRL::ComPtr;
    if (width <= 0 || height <= 0) return nullptr;
    // Callers' threads usually have an apartment already; keep it.
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HICON icon = nullptr;
    try {
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICBitmapScaler> scaler;
        ComPtr<IWICFormatConverter> converter;
        std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
        if (SUCCEEDED(
                CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                         WICDecodeMetadataCacheOnDemand, &decoder)) &&
            SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(frame.Get(), width, height, WICBitmapInterpolationModeFant)) &&
            SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                            nullptr, 0, WICBitmapPaletteTypeCustom)) &&
            SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) {
            BITMAPV5HEADER header{};
            header.bV5Size = sizeof header;
            header.bV5Width = width;
            header.bV5Height = -height;
            header.bV5Planes = 1;
            header.bV5BitCount = 32;
            header.bV5Compression = BI_BITFIELDS;
            header.bV5RedMask = 0x00FF0000;
            header.bV5GreenMask = 0x0000FF00;
            header.bV5BlueMask = 0x000000FF;
            header.bV5AlphaMask = 0xFF000000;
            void* bits = nullptr;
            HDC screen = GetDC(nullptr);
            HBITMAP color =
                CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
            ReleaseDC(nullptr, screen);
            // The alpha channel decides transparency; the mask only has to exist.
            // Monochrome rows are WORD-aligned.
            std::vector<BYTE> maskBits(static_cast<size_t>((width + 15) / 16 * 2) * height);
            HBITMAP mask = CreateBitmap(width, height, 1, 1, maskBits.data());
            if (color && mask && bits) {
                memcpy(bits, pixels.data(), pixels.size());
                ICONINFO info{TRUE, 0, 0, mask, color};
                icon = CreateIconIndirect(&info);
            }
            if (color) DeleteObject(color);
            if (mask) DeleteObject(mask);
        }
    } catch (...) {
        icon = nullptr;
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return icon;
}
}
