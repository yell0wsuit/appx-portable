#include "host/assets.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static int failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// An 88x88 image of one opaque BGR color, as a package logo at scale-200.
static bool WriteImage(const std::filesystem::path& path, REFGUID format, BYTE blue, BYTE green, BYTE red) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    constexpr UINT size = 88;
    std::vector<BYTE> pixels(size * size * 3);
    for (size_t i = 0; i < pixels.size(); i += 3) {
        pixels[i] = blue;
        pixels[i + 1] = green;
        pixels[i + 2] = red;
    }
    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat24bppBGR;
    return SUCCEEDED(
               CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
           SUCCEEDED(factory->CreateStream(&stream)) &&
           SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
           SUCCEEDED(factory->CreateEncoder(format, nullptr, &encoder)) &&
           SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
           SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
           SUCCEEDED(frame->SetSize(size, size)) && SUCCEEDED(frame->SetPixelFormat(&pixelFormat)) &&
           IsEqualGUID(pixelFormat, GUID_WICPixelFormat24bppBGR) &&
           SUCCEEDED(frame->WritePixels(size, size * 3, static_cast<UINT>(pixels.size()), pixels.data())) &&
           SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}
// The icon's size and the BGR color at its centre.
static bool IconColor(HICON icon, int size, BYTE& blue, BYTE& green, BYTE& red) {
    ICONINFO info{};
    if (!icon || !GetIconInfo(icon, &info)) return false;
    BITMAP bitmap{};
    bool ok = GetObjectW(info.hbmColor, sizeof bitmap, &bitmap) && bitmap.bmWidth == size && bitmap.bmHeight == size;
    BITMAPINFO format{};
    format.bmiHeader.biSize = sizeof format.bmiHeader;
    format.bmiHeader.biWidth = size;
    format.bmiHeader.biHeight = -size;
    format.bmiHeader.biPlanes = 1;
    format.bmiHeader.biBitCount = 32;
    format.bmiHeader.biCompression = BI_RGB;
    std::vector<BYTE> pixels(static_cast<size_t>(size) * size * 4);
    HDC screen = GetDC(nullptr);
    ok = ok && GetDIBits(screen, info.hbmColor, 0, size, pixels.data(), &format, DIB_RGB_COLORS) == size;
    ReleaseDC(nullptr, screen);
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    const auto* centre = &pixels[(static_cast<size_t>(size / 2) * size + size / 2) * 4];
    blue = centre[0];
    green = centre[1];
    red = centre[2];
    return ok;
}

int main() {
    CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
    // Single names: OPC escapes are UTF-8; invalid results stay encoded.
    CHECK(appx::DecodePackageName(L"%21_MapSelectController.xml") == std::wstring(L"!_MapSelectController.xml"));
    CHECK(appx::DecodePackageName(L"User%20Controls") == std::wstring(L"User Controls"));
    CHECK(appx::DecodePackageName(L"caf%C3%A9.png") == std::wstring(L"café.png"));
    CHECK(!appx::DecodePackageName(L"plain.xml"));
    CHECK(!appx::DecodePackageName(L"100%.txt")); // not an escape
    CHECK(!appx::DecodePackageName(L"%zz.txt"));
    CHECK(!appx::DecodePackageName(L"a%2Fb.txt")); // would contain a separator
    CHECK(!appx::DecodePackageName(L"%C3.txt"));   // truncated UTF-8
    CHECK(!appx::DecodePackageName(L"%2E%2E"));    // ".."

    const auto root = std::filesystem::current_path() / (L"asset-test-" + std::to_wstring(GetCurrentProcessId()) +
                                                         L"-" + std::to_wstring(GetTickCount64()));
    CHECK(SUCCEEDED(appx::DecodePackageNames(root))); // No game assets beside most test executables.
    CHECK(!std::filesystem::exists(root));
    const auto maps = root / L"Assets" / L"assets" / L"maps";
    std::filesystem::create_directories(maps);
    std::filesystem::create_directories(root / L"User%20Controls");
    std::filesystem::create_directories(root / L"SaveData" / L"Local");
    const auto encoded = maps / L"%21_MasterGameDesignDocument.xml";
    const auto decoded = maps / L"!_MasterGameDesignDocument.xml";
    std::ofstream(encoded) << "design";
    std::ofstream(root / L"User%20Controls" / L"How%20To.xaml") << "control";
    std::ofstream(root / L"SaveData" / L"Local" / L"slot%201.dat") << "save";
    std::ofstream(maps / L"100%.txt") << "literal";

    // Decoding uses the supplied game path; nested folders and files both decode.
    unsigned count = 0;
    CHECK(SUCCEEDED(appx::DecodePackageNames(root, &count)));
    CHECK(count == 3);
    CHECK(!std::filesystem::exists(encoded));
    CHECK(Read(decoded) == "design");
    CHECK(Read(root / L"User Controls" / L"How To.xaml") == "control");
    CHECK(Read(root / L"SaveData" / L"Local" / L"slot%201.dat") == "save"); // saves are never renamed
    CHECK(Read(maps / L"100%.txt") == "literal");
    CHECK(SUCCEEDED(appx::DecodePackageNames(root, &count))); // Repeated launches are harmless.
    CHECK(count == 0);

    std::ofstream(encoded) << "replacement";
    CHECK(SUCCEEDED(appx::DecodePackageNames(root)));
    CHECK(Read(decoded) == "design"); // Preserve a pre-existing decoded file.
    CHECK(Read(encoded) == "replacement");

    std::filesystem::remove(decoded);
    HANDLE locked = CreateFileW(encoded.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(locked != INVALID_HANDLE_VALUE);
    CHECK(FAILED(appx::DecodePackageNames(root))); // Report a blocked rename before game initialization.
    CHECK(Read(encoded) == "replacement");
    CHECK(!std::filesystem::exists(decoded));
    if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
    CHECK(SUCCEEDED(appx::DecodePackageNames(root)));
    CHECK(Read(decoded) == "replacement");

    // Window icons come from the manifest's small logo, a PNG or, as in Human
    // Anatomy Atlas, a JPEG; either is scaled to the requested icon size.
    const auto png = root / L"Square44x44Logo.scale-200.png";
    const auto jpeg = root / L"Square44x44Logo.scale-200.jpg";
    CHECK(WriteImage(png, GUID_ContainerFormatPng, 0, 0, 255));
    CHECK(WriteImage(jpeg, GUID_ContainerFormatJpeg, 255, 0, 0));
    BYTE blue{}, green{}, red{};
    HICON icon = appx::LoadGameIcon(png, 16, 16);
    CHECK(IconColor(icon, 16, blue, green, red) && red > 200 && green < 50 && blue < 50);
    if (icon) DestroyIcon(icon);
    icon = appx::LoadGameIcon(jpeg, 32, 32);
    CHECK(IconColor(icon, 32, blue, green, red) && blue > 200 && green < 50 && red < 50);
    if (icon) DestroyIcon(icon);
    CHECK(!appx::LoadGameIcon(maps / L"100%.txt", 16, 16));
    CHECK(!appx::LoadGameIcon(root / L"missing.png", 16, 16));
    std::filesystem::remove_all(root);
    printf("assettest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
