// A managed game's WPF XAML layer blends over its swap chain when it presents:
// the shim's present hook draws the newest submitted frame with premultiplied
// alpha and leaves the game's pipeline state as it was.
#include <windows.h>
#include <roapi.h>
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.media.h>
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
using CaptureCall = HRESULT(__cdecl*)(HWND);
using CountCall = UINT(__cdecl*)();
// The managed host's WPF source: a layered window whose presents the shim
// keeps as the XAML frame.
struct LayeredSource {
    HWND window = nullptr;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    DWORD* bits = nullptr;
    bool bottomUp = false;
    bool Create(bool rowsBottomUp) {
        bottomUp = rowsBottomUp;
        window =
            CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", nullptr,
                            WS_POPUP | WS_VISIBLE, -32000, -32000, width, height, nullptr, nullptr, nullptr, nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = bottomUp ? static_cast<LONG>(height) : -static_cast<LONG>(height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&bits), nullptr, 0);
        dc = CreateCompatibleDC(nullptr);
        if (dc && bitmap) previous = SelectObject(dc, bitmap);
        return window && bitmap && dc && bits;
    }
    // Pixel (x, y) counted from the top, whatever the row order.
    void Fill(LONG left, LONG top, LONG right, LONG bottom, DWORD color) {
        for (LONG y = top; y < bottom; ++y)
            for (LONG x = left; x < right; ++x) bits[(bottomUp ? height - 1 - y : y) * width + x] = color;
    }
    BOOL Present(const RECT* dirty, const POINT* destination = nullptr) {
        SIZE size{width, height};
        POINT origin{};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UPDATELAYEREDWINDOWINFO info{sizeof(info), nullptr, destination, &size,     dc,
                                     &origin,      0,       &blend,      ULW_ALPHA, dirty};
        return UpdateLayeredWindowIndirect(window, &info);
    }
    ~LayeredSource() {
        if (dc) {
            SelectObject(dc, previous);
            DeleteDC(dc);
        }
        if (bitmap) DeleteObject(bitmap);
        if (window) DestroyWindow(window);
    }
};
static COLORREF HostPixel(HWND host, bool right, bool bottom) {
    HWND fallback = FindWindowExW(host, nullptr, L"AppxPortableXamlFrame", nullptr);
    if (fallback && IsWindowVisible(fallback)) host = fallback;
    RECT client{};
    GetClientRect(host, &client);
    HDC dc = GetDC(host);
    const COLORREF pixel = GetPixel(dc, right ? client.right - 5 : 5, bottom ? client.bottom - 5 : 5);
    ReleaseDC(host, dc);
    return pixel;
}
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
// WPF renders the layered source only where something changed; the shim takes
// what it presents, only the dirty part, and never shows the window itself.
static bool LayeredPresents(HWND host, CaptureCall capture, CountCall count) {
    LayeredSource source;
    if (!Expect(source.Create(false), "a layered source window") || !Check(capture(source.window), "capture"))
        return false;
    const UINT before = count();
    source.Fill(0, 0, width, height, 0xff0000ff);
    SetWindowPos(source.window, nullptr, 0, 0, width / 2, height / 2, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    const POINT destination{-31000, -31000};
    bool ok =
        Expect(source.Present(nullptr, &destination) && count() == before + 1, "a layered present is a XAML frame");
    RECT nativeBounds{};
    GetWindowRect(source.window, &nativeBounds);
    ok = Expect(nativeBounds.left == destination.x && nativeBounds.top == destination.y &&
                    nativeBounds.right - nativeBounds.left == width && nativeBounds.bottom - nativeBounds.top == height,
                "capturing a present preserves native source position and size") &&
         ok;
    const POINT moved{-30000, -30000};
    UPDATELAYEREDWINDOWINFO positionOnly{};
    positionOnly.cbSize = sizeof(positionOnly);
    positionOnly.pptDst = &moved;
    ok = Expect(UpdateLayeredWindowIndirect(source.window, &positionOnly) && count() == before + 1,
                "a position-only update remains native and does not submit pixels") &&
         ok;
    GetWindowRect(source.window, &nativeBounds);
    ok = Expect(nativeBounds.left == moved.x && nativeBounds.top == moved.y,
                "the source follows a position-only update") &&
         ok;
    ok = Expect(HostPixel(host, false, false) == RGB(0, 0, 255) && HostPixel(host, true, false) == RGB(0, 0, 255),
                "the layered frame shows on the window") &&
         ok;
    // Only the dirty quarter is taken: the rest keeps its earlier pixels.
    source.Fill(0, 0, width / 2, height / 2, 0xffff0000);
    source.Fill(width / 2, 0, width, height, 0xff00ff00);
    source.Fill(0, height / 2, width / 2, height, 0xff00ff00);
    const RECT dirty{0, 0, width / 2, height / 2};
    ok = Expect(source.Present(&dirty) && count() == before + 2, "a dirty present is a XAML frame") && ok;
    ok = Expect(HostPixel(host, false, false) == RGB(255, 0, 0) && HostPixel(host, true, false) == RGB(0, 0, 255),
                "a dirty present takes only its dirty rectangle") &&
         ok;
    // Bottom-up sections are read top row first.
    LayeredSource upward;
    ok = Expect(upward.Create(true), "a bottom-up layered source") && Check(capture(upward.window), "capture") && ok;
    upward.Fill(0, 0, width, height / 2, 0xffffff00);
    upward.Fill(0, height / 2, width, height, 0xff0000ff);
    ok = Expect(upward.Present(nullptr) && HostPixel(host, false, false) == RGB(255, 255, 0) &&
                    HostPixel(host, false, true) == RGB(0, 0, 255),
                "a bottom-up section keeps its rows in order") &&
         ok;
    // Replacing the source starts a complete frame even when WPF reports a
    // small dirty rectangle and the new window has the same dimensions.
    source.Fill(0, 0, width, height, 0xff00ffff);
    ok = Check(capture(source.window), "replace captured source") &&
         Expect(source.Present(&dirty) && HostPixel(host, true, false) == RGB(0, 255, 255),
                "a replacement source starts with all its pixels") &&
         ok;
    // Other layered windows are shown as usual.
    LayeredSource other;
    ok = Expect(other.Create(false), "another layered window") &&
         Expect(other.Present(nullptr) && count() == before + 4, "another window's present is not a XAML frame") && ok;
    SIZE fullSize{width, height};
    POINT origin{};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    ok = Expect(UpdateLayeredWindow(source.window, nullptr, nullptr, &fullSize, source.dc, &origin, 0, &blend,
                                    ULW_ALPHA) &&
                    count() == before + 5,
                "the plain layered API captures once even if it forwards through the indirect API") &&
         ok;
    if (!ok)
        printf("xamllayertest: host pixels %08lx %08lx\n", HostPixel(host, false, false), HostPixel(host, true, false));
    return ok;
}
// A XAML-only page presents nothing; the shim then shows the XAML frame itself.
static bool WithoutGamePresent(SubmitCall submit) {
    HWND host = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), FindVisible, reinterpret_cast<LPARAM>(&host));
    if (!Expect(host != nullptr, "a visible host window")) return false;
    std::vector<DWORD> red(width * height, 0xffff0000);
    if (!Check(submit(red.data(), width, height, width * 4), "submit frame without a game present")) return false;
    const COLORREF pixel = HostPixel(host, false, false);
    if (!Expect(pixel == RGB(255, 0, 0), "the XAML frame shows on the window while the game does not present")) {
        printf("xamllayertest: window pixel=%08lx\n", pixel);
        return false;
    }
    RECT bounds{};
    GetClientRect(host, &bounds);
    const POINT point{bounds.right / 2, bounds.bottom / 2};
    return Expect(ChildWindowFromPointEx(host, point, CWP_SKIPDISABLED | CWP_SKIPINVISIBLE) == host,
                  "the display-only XAML frame does not take pointer hit testing");
}
static DWORD WINAPI PresentOnWorker(void* swap) {
    auto* chain = static_cast<IDXGISwapChain*>(swap);
    const auto hr = chain->Present(0, 0);
    chain->Release();
    return hr;
}
static bool Run(IDXGISwapChain1* swap, ID3D11Device* device, ID3D11DeviceContext* context, SubmitCall submit,
                CaptureCall capture, HWND host) {
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
    // A dirty layered present updates only its part of the blended layer. The
    // shim captures every 120th frame.
    if (!capture) return Check(submit(nullptr, 0, 0, 0), "withdraw layer") && ok;
    LayeredSource source;
    ok = Expect(source.Create(false), "a layered source") && Check(capture(source.window), "capture") && ok;
    source.Fill(0, 0, width, height, 0xff0000ff);
    ok =
        Expect(source.Present(nullptr) != FALSE, "full layered present") && Check(swap->Present(0, 0), "present") && ok;
    source.Fill(0, 0, 8, 8, 0xffff0000);
    source.Fill(40, 20, 48, 28, 0xffffff00);
    const RECT dirty{0, 0, 8, 8};
    ok = Expect(source.Present(&dirty) != FALSE, "dirty layered present") && ok;
    source.Fill(16, 8, 24, 16, 0xff00ff00);
    const RECT secondDirty{16, 8, 24, 16};
    ok = Expect(source.Present(&secondDirty) != FALSE, "another dirty present before upload") && ok;
    for (int frame = 3; frame <= 120 && ok; ++frame) ok = Check(swap->Present(0, 0), "present");
    DWORD changed = 0, second = 0, kept = 0;
    ok = Expect(CapturedPixel(2, 2, &changed) && CapturedPixel(20, 12, &second) && CapturedPixel(44, 24, &kept),
                "captured frame 120") &&
         ok;
    ok = Expect(changed == 0xffff0000 && second == 0xff00ff00 && kept == 0xff0000ff,
                "a dirty present uploads only its rectangle") &&
         ok;
    if (!ok) printf("xamllayertest: changed=%08lx kept=%08lx\n", changed, kept);
    // The final XAML change may precede the game's final present by less than
    // the quiet period. The composition clock must eventually show that retained
    // frame, even if WPF submits nothing else.
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(
        GetProcAddress(GetModuleHandleW(L"appxportable.dll"), "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    ComPtr<ABI::Windows::UI::Xaml::Media::ICompositionTargetStatics> clock;
    if (!Check(get(HStringReference(L"Windows.UI.Xaml.Media.CompositionTarget").Get(), &factory), "clock factory") ||
        !Check(factory.As(&clock), "clock statics"))
        return false;
    bool delayHide = false, delayed = false;
    auto tick =
        Callback<ABI::Windows::Foundation::IEventHandler<IInspectable*>>([&](IInspectable*, IInspectable*) -> HRESULT {
            if (delayHide && !delayed) {
                delayed = true;
                // A worker present posts an overlay hide while this UI callback
                // is busy. The clock shows the retained frame before returning
                // to the queue; the older hide must not hide that newer frame.
                swap->AddRef(); // the worker owns this even if its wait times out
                HANDLE worker = CreateThread(nullptr, 0, PresentOnWorker, swap, 0, nullptr);
                if (!worker) {
                    swap->Release();
                    ok = false;
                    return E_FAIL;
                }
                // DXGI may send synchronous window messages during Present.
                // Service those without removing the posted hide being tested.
                const auto deadline = GetTickCount64() + 5000;
                for (;;) {
                    const auto now = GetTickCount64();
                    if (now >= deadline) break;
                    const auto wait = MsgWaitForMultipleObjects(1, &worker, FALSE, static_cast<DWORD>(deadline - now),
                                                                QS_SENDMESSAGE);
                    if (wait != WAIT_OBJECT_0 + 1) break;
                    MSG message{};
                    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
                }
                DWORD result = E_FAIL;
                if (WaitForSingleObject(worker, 0) == WAIT_OBJECT_0) GetExitCodeThread(worker, &result);
                CloseHandle(worker);
                ok = Check(static_cast<HRESULT>(result), "worker present") && ok;
                Sleep(300);
            }
            return S_OK;
        });
    EventRegistrationToken token{};
    if (!Check(clock->add_Rendering(tick.Get(), &token), "start composition clock")) return false;
    source.Fill(0, 0, width, height, 0xff00ffff);
    ok = Expect(source.Present(nullptr) != FALSE, "final XAML change while game present is recent") && ok;
    const auto until = GetTickCount64() + 400;
    while (GetTickCount64() < until) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(message.wParam));
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    }
    ok = Expect(HostPixel(host, false, false) == RGB(0, 255, 255),
                "a retained XAML frame shows after the game's last present") &&
         ok;
    HWND paintWindow = FindWindowExW(host, nullptr, L"AppxPortableXamlFrame", nullptr);
    if (!paintWindow) paintWindow = host;
    HDC dc = GetDC(paintWindow);
    RECT client{};
    GetClientRect(paintWindow, &client);
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    ReleaseDC(paintWindow, dc);
    InvalidateRect(paintWindow, nullptr, TRUE);
    UpdateWindow(paintWindow);
    ok = Expect(HostPixel(host, false, false) == RGB(0, 255, 255), "paint restores an unchanged XAML frame") && ok;
    delayHide = true;
    bool hiddenByDelayedWork = false;
    const auto delayedUntil = GetTickCount64() + 500;
    while (GetTickCount64() < delayedUntil) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (delayed && !IsWindowVisible(paintWindow)) hiddenByDelayedWork = true;
        }
        Sleep(1);
    }
    ok = Expect(delayed && !hiddenByDelayedWork && IsWindowVisible(paintWindow) &&
                    HostPixel(host, false, false) == RGB(0, 255, 255),
                "a delayed hide cannot hide a newer retained frame") &&
         ok;
    ok = Check(clock->remove_Rendering(token), "stop composition clock") && ok;
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
    auto capture = reinterpret_cast<CaptureCall>(GetProcAddress(dll, "ShimCaptureXamlWindow"));
    auto count = reinterpret_cast<CountCall>(GetProcAddress(dll, "ShimXamlFrameCount"));
    if (!get || !submit || !capture || !count) return 2;
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
            EnumThreadWindows(
                GetCurrentThreadId(),
                [](HWND window, LPARAM found) -> BOOL {
                    *reinterpret_cast<HWND*>(found) = window;
                    return FALSE;
                },
                reinterpret_cast<LPARAM>(&host));
            if (host) ShowWindow(host, SW_SHOWNA);
            // x64 has no hotpatchable user32; the managed host renders frames itself there.
            const HRESULT captured = capture(host);
            const bool layered =
                captured == E_NOTIMPL || (Check(captured, "capture host") && LayeredPresents(host, capture, count));
            const bool shown = layered && WithoutGamePresent(submit);
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
                          "composition swap chain")) {
                    RECT bounds{};
                    GetClientRect(host, &bounds);
                    const POINT center{bounds.right / 2, bounds.bottom / 2};
                    const bool hostInput =
                        Expect(ChildWindowFromPointEx(host, center, CWP_SKIPDISABLED | CWP_SKIPINVISIBLE) == host,
                               "display swap-chain surfaces leave pointer hit testing to the host");
                    passed = shown && hostInput &&
                             Run(swap.Get(), device.Get(), context.Get(), submit,
                                 captured == E_NOTIMPL ? nullptr : capture, host);
                }
            }
            PostQuitMessage(0);
            return S_OK;
        });
    auto hr = statics->Start(callback.Get());
    printf("xamllayertest: host=%08lx passed=%d\n", hr, passed ? 1 : 0);
    return FAILED(hr) || !passed ? 1 : 0;
}
