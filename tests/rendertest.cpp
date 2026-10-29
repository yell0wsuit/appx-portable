// The XAML renderer's prerequisite: Direct2D text and a WIC-decoded image drawn
// over a game's frame on its redirected composition swap chain, with the game's
// own D3D11 device. Without a hardware device (hosted CI) it uses WARP.
// APPX_RENDER_HOLD_MS keeps the presented frame on screen for a screenshot.
#include <windows.h>
#include <roapi.h>
#undef GetCurrentTime
#include <windows.ui.xaml.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <cstdlib>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Xaml;
static constexpr UINT width = 320, height = 160;
static bool Check(HRESULT hr, const char* step) {
    if (SUCCEEDED(hr)) return true;
    printf("rendertest: %s hr=%08lx\n", step, hr);
    return false;
}
// A 32x32 PNG, red on the left and green on the right, encoded and then decoded
// by WIC as a game's image asset would be.
static HRESULT DecodeImage(IWICImagingFactory* wic, IWICFormatConverter** value) {
    static BYTE pixels[32 * 32 * 4];
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            BYTE* p = pixels + (y * 32 + x) * 4;
            p[0] = 0;
            p[1] = x < 16 ? 0 : 255;
            p[2] = x < 16 ? 255 : 0;
            p[3] = 255;
        }
    ComPtr<IStream> stream;
    auto hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    ComPtr<IWICBitmapEncoder> encoder;
    if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &options);
    if (SUCCEEDED(hr)) hr = frame->Initialize(options.Get());
    if (SUCCEEDED(hr)) hr = frame->SetSize(32, 32);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr) && format != GUID_WICPixelFormat32bppBGRA) hr = E_UNEXPECTED;
    if (SUCCEEDED(hr)) hr = frame->WritePixels(32, 32 * 4, sizeof(pixels), pixels);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    LARGE_INTEGER start{};
    if (SUCCEEDED(hr)) hr = stream->Seek(start, STREAM_SEEK_SET, nullptr);
    ComPtr<IWICBitmapDecoder> decoder;
    if (SUCCEEDED(hr))
        hr = wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    ComPtr<IWICBitmapFrameDecode> decoded;
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &decoded);
    ComPtr<IWICFormatConverter> converter;
    if (SUCCEEDED(hr)) hr = wic->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
        hr = converter->Initialize(decoded.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom);
    return SUCCEEDED(hr) ? converter.CopyTo(value) : hr;
}
static bool Near(const BYTE* pixel, int b, int g, int r) {
    return abs(pixel[0] - b) <= 3 && abs(pixel[1] - g) <= 3 && abs(pixel[2] - r) <= 3;
}
// The game's frame, its opaque blue background, must stay where nothing is drawn;
// the image keeps its colors; the text draws light pixels inside its box only.
static bool CheckPixels(const BYTE* pixels, UINT pitch) {
    auto at = [&](UINT x, UINT y) { return pixels + y * pitch + x * 4; };
    if (!Near(at(4, 150), 153, 77, 26) || !Near(at(150, 140), 153, 77, 26)) {
        printf("rendertest: game frame overwritten\n");
        return false;
    }
    if (!Near(at(254, 112), 0, 0, 255) || !Near(at(290, 140), 0, 255, 0)) {
        printf("rendertest: decoded image colors missing\n");
        return false;
    }
    unsigned text = 0, stray = 0;
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x) {
            const BYTE* p = at(x, y);
            if (p[0] < 200 || p[1] < 200 || p[2] < 200) continue;
            if (x >= 12 && x < 300 && y >= 12 && y < 64)
                ++text;
            else
                ++stray;
        }
    printf("rendertest: %u text pixels, %u light pixels outside the text box\n", text, stray);
    return text >= 100 && stray == 0;
}
static bool Render(IDXGISwapChain1* swap, ID3D11Device* device, ID3D11DeviceContext* context) {
    ComPtr<ID3D11Texture2D> back;
    if (!Check(swap->GetBuffer(0, IID_PPV_ARGS(&back)), "back buffer")) return false;
    ComPtr<ID3D11RenderTargetView> target;
    if (!Check(device->CreateRenderTargetView(back.Get(), nullptr, &target), "render target")) return false;
    // The game's own frame.
    const float game[] = {0.1f, 0.3f, 0.6f, 1};
    context->ClearRenderTargetView(target.Get(), game);
    ComPtr<ID2D1Factory1> factory;
    D2D1_FACTORY_OPTIONS options{};
    if (!Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, &factory),
               "Direct2D factory"))
        return false;
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<ID2D1Device> d2d;
    ComPtr<ID2D1DeviceContext> draw;
    if (!Check(device->QueryInterface(IID_PPV_ARGS(&dxgi)), "DXGI device") ||
        !Check(factory->CreateDevice(dxgi.Get(), &d2d), "Direct2D device") ||
        !Check(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &draw), "Direct2D device context"))
        return false;
    ComPtr<IDXGISurface> surface;
    ComPtr<ID2D1Bitmap1> bitmap;
    const auto properties =
        D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (!Check(back.As(&surface), "back buffer surface") ||
        !Check(draw->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &bitmap), "target bitmap"))
        return false;
    draw->SetTarget(bitmap.Get());
    ComPtr<IDWriteFactory> write;
    ComPtr<IDWriteTextFormat> format;
    // Loaded as games load it; the build tree's DWrite proxy shadows the SDK import library.
    using CreateWriteFactory = HRESULT(WINAPI*)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
    auto* dwrite = LoadLibraryW(L"dwrite.dll");
    auto create =
        dwrite ? reinterpret_cast<CreateWriteFactory>(GetProcAddress(dwrite, "DWriteCreateFactory")) : nullptr;
    if (!create) {
        printf("rendertest: DWriteCreateFactory unavailable\n");
        return false;
    }
    if (!Check(create(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                      reinterpret_cast<IUnknown**>(write.GetAddressOf())),
               "DirectWrite factory") ||
        !Check(write->CreateTextFormat(L"Tahoma", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL, 32, L"en-us", &format),
               "text format"))
        return false;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICFormatConverter> image;
    ComPtr<ID2D1Bitmap> picture;
    ComPtr<ID2D1SolidColorBrush> white;
    if (!Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)),
               "WIC factory") ||
        !Check(DecodeImage(wic.Get(), &image), "PNG decode") ||
        !Check(draw->CreateBitmapFromWicBitmap(image.Get(), nullptr, &picture), "image bitmap") ||
        !Check(draw->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &white), "brush"))
        return false;
    draw->BeginDraw();
    const wchar_t text[] = L"Bejeweled";
    draw->DrawText(text, ARRAYSIZE(text) - 1, format.Get(), D2D1::RectF(12, 12, 300, 64), white.Get());
    draw->DrawBitmap(picture.Get(), D2D1::RectF(240, 96, 304, 160), 1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    if (!Check(draw->EndDraw(), "Direct2D EndDraw")) return false;
    draw->SetTarget(nullptr);
    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> readback;
    if (!Check(device->CreateTexture2D(&desc, nullptr, &readback), "readback texture")) return false;
    context->CopyResource(readback.Get(), back.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (!Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped), "readback map")) return false;
    const bool drawn = CheckPixels(static_cast<const BYTE*>(mapped.pData), mapped.RowPitch);
    context->Unmap(readback.Get(), 0);
    // The shim's capture saves this frame as appx-frame.bmp.
    return drawn && Check(swap->Present(0, 0), "present");
}
int main() {
    DeleteFileW(L"appx-frame.bmp");
    SetEnvironmentVariableW(L"APPX_PORTABLE_CAPTURE", L"1");
    RoInitialize(RO_INIT_SINGLETHREADED);
    auto* dll = LoadLibraryW(L"appxportable.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    if (FAILED(get(HStringReference(L"Windows.UI.Xaml.Application").Get(), &factory))) return 2;
    ComPtr<IApplicationStatics> statics;
    ComPtr<IApplicationFactory> compose;
    if (FAILED(factory.As(&statics)) || FAILED(factory.As(&compose))) return 3;
    bool rendered = false;
    auto callback =
        Callback<IApplicationInitializationCallback>([&](IApplicationInitializationCallbackParams*) -> HRESULT {
            ComPtr<IApplication> app;
            compose->CreateInstance(nullptr, nullptr, &app);
            ComPtr<ID3D11Device> device;
            ComPtr<ID3D11DeviceContext> context;
            // Direct2D needs BGRA support, as games that draw with it request.
            auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
            const bool warp = FAILED(hr);
            if (warp)
                hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                       nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
            printf("rendertest: %s device\n", warp ? "WARP" : "hardware");
            ComPtr<IDXGIDevice> dxgi;
            ComPtr<IDXGIAdapter> adapter;
            ComPtr<IDXGIFactory2> dxgiFactory;
            if (Check(hr, "D3D11 device") && Check(device.As(&dxgi), "DXGI device") &&
                Check(dxgi->GetAdapter(&adapter), "adapter") &&
                Check(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory)), "DXGI factory")) {
                DXGI_SWAP_CHAIN_DESC1 desc{};
                desc.Width = width;
                desc.Height = height;
                desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                desc.BufferCount = 2;
                desc.Scaling = DXGI_SCALING_STRETCH;
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
                // A XAML game's swap chain; the shim redirects it to its host window.
                ComPtr<IDXGISwapChain1> swap;
                if (Check(dxgiFactory->CreateSwapChainForComposition(device.Get(), &desc, nullptr, &swap),
                          "composition swap chain"))
                    rendered = Render(swap.Get(), device.Get(), context.Get());
                char hold[16]{};
                if (rendered && GetEnvironmentVariableA("APPX_RENDER_HOLD_MS", hold, sizeof(hold))) {
                    const ULONGLONG until = GetTickCount64() + strtoul(hold, nullptr, 10);
                    MSG msg{};
                    while (GetTickCount64() < until) {
                        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
                        Sleep(16);
                    }
                }
            }
            PostQuitMessage(0);
            return S_OK;
        });
    auto hr = statics->Start(callback.Get());
    printf("rendertest: host=%08lx rendered=%d\n", hr, rendered ? 1 : 0);
    return FAILED(hr) || !rendered ? 1 : 0;
}
