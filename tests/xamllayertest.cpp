// A managed game's WPF XAML layer blends over its swap chain when it presents:
// the shim's present hook draws the newest submitted frame with premultiplied
// alpha and leaves the game's pipeline state as it was.
#include <windows.h>
#include <roapi.h>
#include <windows.ui.xaml.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <vector>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Xaml;
static constexpr UINT width = 64, height = 32;
static bool Check(HRESULT hr, const char* step) {
    if (SUCCEEDED(hr)) return true;
    printf("xamllayertest: %s failed hr=%08lx\n", step, hr);
    return false;
}
static bool Expect(bool value, const char* what) {
    if (!value) printf("xamllayertest: %s\n", what);
    return value;
}
using SubmitCall = HRESULT(__cdecl*)(const void*, UINT, UINT, UINT);
// The shim saves the first presented frame as appx-frame.bmp (bottom-up rows
// written top-down with a negative height): BGRA pixel at (x, y).
static bool CapturedPixel(UINT x, UINT y, DWORD* value) {
    FILE* file{};
    if (fopen_s(&file, "appx-frame.bmp", "rb") != 0) return false;
    BITMAPFILEHEADER header{};
    BITMAPINFOHEADER info{};
    bool ok = fread(&header, sizeof(header), 1, file) == 1 && fread(&info, sizeof(info), 1, file) == 1 &&
              info.biWidth == static_cast<LONG>(width) && info.biHeight == -static_cast<LONG>(height) &&
              fseek(file, static_cast<long>(header.bfOffBits + (y * width + x) * 4), SEEK_SET) == 0 &&
              fread(value, 4, 1, file) == 1;
    fclose(file);
    return ok;
}
static BOOL CALLBACK FindVisible(HWND window, LPARAM found) {
    if (!IsWindowVisible(window)) return TRUE;
    *reinterpret_cast<HWND*>(found) = window;
    return FALSE;
}
// A XAML-only page presents nothing; the shim then shows the XAML frame itself.
static bool WithoutGamePresent(SubmitCall submit) {
    HWND host = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), FindVisible, reinterpret_cast<LPARAM>(&host));
    if (!Expect(host != nullptr, "a visible host window")) return false;
    std::vector<DWORD> red(width * height, 0xffff0000);
    if (!Check(submit(red.data(), width, height, width * 4), "submit frame without a game present")) return false;
    HDC dc = GetDC(host);
    const COLORREF pixel = GetPixel(dc, 5, 5);
    ReleaseDC(host, dc);
    if (!Expect(pixel == RGB(255, 0, 0), "the XAML frame shows on the window while the game does not present")) {
        printf("xamllayertest: window pixel=%08lx\n", pixel);
        return false;
    }
    return true;
}
static bool Run(IDXGISwapChain1* swap, ID3D11Device* device, ID3D11DeviceContext* context, SubmitCall submit) {
    ComPtr<ID3D11Texture2D> back;
    ComPtr<ID3D11RenderTargetView> target;
    if (!Check(swap->GetBuffer(0, IID_PPV_ARGS(&back)), "back buffer") ||
        !Check(device->CreateRenderTargetView(back.Get(), nullptr, &target), "render target"))
        return false;
    // The game's frame: green, with its own target and a viewport of its own.
    const float green[4] = {0, 160 / 255.f, 0, 1};
    context->ClearRenderTargetView(target.Get(), green);
    D3D11_VIEWPORT gameViewport{3, 4, 20, 10, 0, 1};
    context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
    context->RSSetViewports(1, &gameViewport);
    // The XAML layer: half-transparent white, premultiplied, at half the size.
    std::vector<DWORD> layer((width / 2) * (height / 2), 0x80808080);
    if (!Check(submit(layer.data(), width / 2, height / 2, width / 2 * 4), "submit frame") ||
        !Expect(submit(layer.data(), 0, 1, 4) == E_INVALIDARG, "an empty frame size is rejected"))
        return false;
    if (!Check(swap->Present(0, 0), "present")) return false;
    D3D11_VIEWPORT viewport{};
    UINT count = 1;
    context->RSGetViewports(&count, &viewport);
    ComPtr<ID3D11RenderTargetView> bound;
    context->OMGetRenderTargets(1, &bound, nullptr);
    bool ok = Expect(count == 1 && viewport.TopLeftX == 3 && viewport.Width == 20 && bound == target,
                     "the game's viewport and render target survive the blend");
    DWORD corner = 0, middle = 0;
    ok = Expect(CapturedPixel(0, 0, &corner) && CapturedPixel(width / 2, height / 2, &middle), "captured frame") && ok;
    // 50% white over (0, 160, 0): (128, 128 + 80, 128), scaled to cover the target.
    ok = Expect(corner == 0xff80d080 && middle == 0xff80d080, "XAML layer blends over the game frame") && ok;
    if (!ok) printf("xamllayertest: corner=%08lx middle=%08lx\n", corner, middle);
    return Check(submit(nullptr, 0, 0, 0), "withdraw layer") && ok;
}
int main() {
    DeleteFileW(L"appx-frame.bmp");
    SetEnvironmentVariableW(L"APPX_PORTABLE_CAPTURE", L"1");
    RoInitialize(RO_INIT_SINGLETHREADED);
    auto* dll = LoadLibraryW(L"appxportable.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    auto submit = reinterpret_cast<SubmitCall>(GetProcAddress(dll, "ShimSubmitXamlFrame"));
    if (!get || !submit) return 2;
    ComPtr<IActivationFactory> factory;
    if (FAILED(get(HStringReference(L"Windows.UI.Xaml.Application").Get(), &factory))) return 3;
    ComPtr<IApplicationStatics> statics;
    ComPtr<IApplicationFactory> compose;
    if (FAILED(factory.As(&statics)) || FAILED(factory.As(&compose))) return 4;
    bool passed = false;
    auto callback =
        Callback<IApplicationInitializationCallback>([&](IApplicationInitializationCallbackParams*) -> HRESULT {
            ComPtr<IApplication> app;
            compose->CreateInstance(nullptr, nullptr, &app);
            HWND host = nullptr;
            EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM found) -> BOOL {
                *reinterpret_cast<HWND*>(found) = window;
                return FALSE;
            }, reinterpret_cast<LPARAM>(&host));
            if (host) ShowWindow(host, SW_SHOWNA);
            const bool shown = WithoutGamePresent(submit);
            ComPtr<ID3D11Device> device;
            ComPtr<ID3D11DeviceContext> context;
            ComPtr<IDXGIDevice> dxgi;
            ComPtr<IDXGIAdapter> adapter;
            ComPtr<IDXGIFactory2> dxgiFactory;
            if (Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context),
                      "D3D11 device") &&
                Check(device.As(&dxgi), "DXGI device") && Check(dxgi->GetAdapter(&adapter), "adapter") &&
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
                // A XAML game's board swap chain; the shim redirects it to its window.
                ComPtr<IDXGISwapChain1> swap;
                if (Check(dxgiFactory->CreateSwapChainForComposition(device.Get(), &desc, nullptr, &swap),
                          "composition swap chain"))
                    passed = shown && Run(swap.Get(), device.Get(), context.Get(), submit);
            }
            PostQuitMessage(0);
            return S_OK;
        });
    auto hr = statics->Start(callback.Get());
    printf("xamllayertest: host=%08lx passed=%d\n", hr, passed ? 1 : 0);
    return FAILED(hr) || !passed ? 1 : 0;
}
