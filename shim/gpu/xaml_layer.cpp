#include "xaml_layer.h"
#include "log.h"
#include "host/host.h"
#include "options.h"
#include "entry/hotpatch.h"
#include <cstdio>
#include <atomic>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include <mutex>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace appx {
namespace {
// The newest frame of a managed game's WPF XAML tree: premultiplied BGRA,
// rendered offscreen and blended over the game's swap chain when it presents.
struct Frame {
    std::vector<BYTE> pixels;
    UINT width = 0, height = 0;
    unsigned generation = 0;
    // What changed since the last upload; all of it after a size change.
    RECT dirty{};
    bool shown = false;
};
std::mutex frameMutex;
Frame latest;
// When the game last presented. A XAML-only page (Bejeweled's hub) presents
// nothing, so the shim then shows the XAML frame on the window itself.
std::atomic<ULONGLONG> lastPresent{0};
constexpr ULONGLONG presentQuiet = 250;

const char shaderSource[] = R"(
struct V { float4 position : SV_Position; float2 uv : TEXCOORD0; };
V vs(float4 corner : POSITION) {
    V o;
    o.position = float4(corner.xy, 0, 1);
    o.uv = corner.zw;
    return o;
}
Texture2D layer : register(t0);
SamplerState layerSampler : register(s0);
float4 ps(V i) : SV_Target { return layer.Sample(layerSampler, i.uv); }
)";

// Device objects for the game's device; rebuilt if the game changes devices.
struct Layer {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11InputLayout> input;
    ComPtr<ID3D11Buffer> corners;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    UINT width = 0, height = 0;
    unsigned generation = 0;
    bool failed = false;
};
std::mutex layerMutex;
Layer layer;

using CompileCall = decltype(&D3DCompile);
HRESULT Compile(const char* entry, const char* profile, ComPtr<ID3DBlob>& code) {
    static CompileCall compile = [] {
        auto* module = LoadLibraryW(L"d3dcompiler_47.dll");
        return module ? reinterpret_cast<CompileCall>(GetProcAddress(module, "D3DCompile")) : nullptr;
    }();
    if (!compile) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    ComPtr<ID3DBlob> errors;
    auto hr = compile(shaderSource, sizeof(shaderSource) - 1, "xaml_layer", nullptr, nullptr, entry, profile, 0, 0,
                      &code, &errors);
    if (FAILED(hr) && errors)
        Log("FAIL", "XAML layer shader: %s", static_cast<const char*>(errors->GetBufferPointer()));
    return hr;
}

HRESULT Prepare(ID3D11Device* device) {
    if (layer.device.Get() == device) return layer.failed ? E_FAIL : S_OK;
    layer = Layer{};
    layer.device = device;
    ComPtr<ID3DBlob> vs, ps;
    // Feature level 9_1 profiles and a vertex buffer: Windows 8 games often
    // create 9_x devices, which have no SV_VertexID.
    auto hr = Compile("vs", "vs_4_0_level_9_1", vs);
    if (SUCCEEDED(hr)) hr = Compile("ps", "ps_4_0_level_9_1", ps);
    if (SUCCEEDED(hr))
        hr = device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &layer.vertex);
    const D3D11_INPUT_ELEMENT_DESC element{
        "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
    if (SUCCEEDED(hr))
        hr = device->CreateInputLayout(&element, 1, vs->GetBufferPointer(), vs->GetBufferSize(), &layer.input);
    // One triangle covering the target: clip-space x, y and texture u, v.
    static const float corners[] = {-1, 1, 0, 0, 3, 1, 2, 0, -1, -3, 0, 2};
    D3D11_BUFFER_DESC buffer{sizeof(corners), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER};
    D3D11_SUBRESOURCE_DATA data{corners};
    if (SUCCEEDED(hr)) hr = device->CreateBuffer(&buffer, &data, &layer.corners);
    if (SUCCEEDED(hr))
        hr = device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &layer.pixel);
    D3D11_BLEND_DESC blend{};
    auto& target = blend.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.SrcBlend = target.SrcBlendAlpha = D3D11_BLEND_ONE;
    target.DestBlend = target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOp = target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (SUCCEEDED(hr)) hr = device->CreateBlendState(&blend, &layer.blend);
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    if (SUCCEEDED(hr)) hr = device->CreateRasterizerState(&raster, &layer.raster);
    D3D11_DEPTH_STENCIL_DESC depth{};
    if (SUCCEEDED(hr)) hr = device->CreateDepthStencilState(&depth, &layer.depth);
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (SUCCEEDED(hr)) hr = device->CreateSamplerState(&sampler, &layer.sampler);
    if (FAILED(hr)) {
        layer.failed = true;
        Log("FAIL", "XAML layer setup hr=%08lx; XAML is not drawn", hr);
    }
    return hr;
}

// Uploads the newest frame; false when there is nothing to draw.
bool Upload(ID3D11Device* device, ID3D11DeviceContext* context) {
    Frame pending;
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        if (!latest.width || !latest.height) return false;
        if (latest.generation == layer.generation && layer.view) return true;
        pending = latest;
    }
    // Drivers may wait for the WPF renderer, which must be free to store a frame.
    RECT dirty = pending.dirty;
    if (pending.width != layer.width || pending.height != layer.height || !layer.texture) {
        dirty = RECT{0, 0, static_cast<LONG>(pending.width), static_cast<LONG>(pending.height)};
        D3D11_TEXTURE2D_DESC desc{pending.width,
                                  pending.height,
                                  1,
                                  1,
                                  DXGI_FORMAT_B8G8R8A8_UNORM,
                                  {1, 0},
                                  D3D11_USAGE_DEFAULT,
                                  D3D11_BIND_SHADER_RESOURCE};
        layer.view.Reset();
        layer.texture.Reset();
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &layer.texture)) ||
            FAILED(device->CreateShaderResourceView(layer.texture.Get(), nullptr, &layer.view)))
            return false;
        layer.width = pending.width;
        layer.height = pending.height;
    }
    // Only what changed: an idle tree with a blinking caret uploads a few pixels.
    if (!IsRectEmpty(&dirty)) {
        const D3D11_BOX box{static_cast<UINT>(dirty.left),  static_cast<UINT>(dirty.top),    0,
                            static_cast<UINT>(dirty.right), static_cast<UINT>(dirty.bottom), 1};
        const BYTE* first = pending.pixels.data() + (static_cast<size_t>(dirty.top) * pending.width + dirty.left) * 4;
        context->UpdateSubresource(layer.texture.Get(), 0, &box, first, pending.width * 4, 0);
    }
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        if (latest.generation == pending.generation) latest.dirty = RECT{};
    }
    layer.generation = pending.generation;
    return true;
}

// The pipeline state the blend touches, saved from and restored to the game.
struct SavedState {
    D3D11_PRIMITIVE_TOPOLOGY topology{};
    ComPtr<ID3D11InputLayout> input;
    ComPtr<ID3D11Buffer> buffer;
    UINT stride = 0, offset = 0;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11HullShader> hull;
    ComPtr<ID3D11DomainShader> domain;
    ComPtr<ID3D11GeometryShader> geometry;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11ShaderResourceView> view;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> raster;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ComPtr<ID3D11RenderTargetView> targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
    ComPtr<ID3D11DepthStencilView> depthView;
    ComPtr<ID3D11BlendState> blend;
    FLOAT blendFactor[4]{};
    UINT sampleMask = 0;
    ComPtr<ID3D11DepthStencilState> depth;
    UINT stencilRef = 0;
    void Save(ID3D11DeviceContext* c) {
        c->IAGetPrimitiveTopology(&topology);
        c->IAGetInputLayout(&input);
        c->IAGetVertexBuffers(0, 1, &buffer, &stride, &offset);
        c->VSGetShader(&vertex, nullptr, nullptr);
        c->HSGetShader(&hull, nullptr, nullptr);
        c->DSGetShader(&domain, nullptr, nullptr);
        c->GSGetShader(&geometry, nullptr, nullptr);
        c->PSGetShader(&pixel, nullptr, nullptr);
        c->PSGetShaderResources(0, 1, &view);
        c->PSGetSamplers(0, 1, &sampler);
        c->RSGetState(&raster);
        c->RSGetViewports(&viewportCount, viewports);
        c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets[0].GetAddressOf(), &depthView);
        c->OMGetBlendState(&blend, blendFactor, &sampleMask);
        c->OMGetDepthStencilState(&depth, &stencilRef);
    }
    void Restore(ID3D11DeviceContext* c) {
        c->IASetPrimitiveTopology(topology);
        c->IASetInputLayout(input.Get());
        c->IASetVertexBuffers(0, 1, buffer.GetAddressOf(), &stride, &offset);
        c->VSSetShader(vertex.Get(), nullptr, 0);
        c->HSSetShader(hull.Get(), nullptr, 0);
        c->DSSetShader(domain.Get(), nullptr, 0);
        c->GSSetShader(geometry.Get(), nullptr, 0);
        c->PSSetShader(pixel.Get(), nullptr, 0);
        c->PSSetShaderResources(0, 1, view.GetAddressOf());
        c->PSSetSamplers(0, 1, sampler.GetAddressOf());
        c->RSSetState(raster.Get());
        c->RSSetViewports(viewportCount, viewports);
        ID3D11RenderTargetView* views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
        for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) views[i] = targets[i].Get();
        c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, views, depthView.Get());
        c->OMSetBlendState(blend.Get(), blendFactor, sampleMask);
        c->OMSetDepthStencilState(depth.Get(), stencilRef);
    }
};
}

void ComposeXamlLayer(IDXGISwapChain* swap) {
    std::lock_guard<std::mutex> resources(layerMutex);
    lastPresent = GetTickCount64();
    HideXamlFallbackWindow();
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        latest.shown = false;
        if (!latest.width) return; // no managed XAML tree, or nothing submitted yet
    }
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&back)))) return;
    ComPtr<ID3D11Device> device;
    back->GetDevice(&device);
    if (FAILED(Prepare(device.Get()))) return;
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    if (!Upload(device.Get(), context.Get())) return;
    ComPtr<ID3D11RenderTargetView> target;
    if (FAILED(device->CreateRenderTargetView(back.Get(), nullptr, &target))) return;
    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);
    SavedState saved;
    saved.Save(context.Get());
    D3D11_VIEWPORT port{0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0, 1};
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(layer.input.Get());
    const UINT stride = 4 * sizeof(float), offset = 0;
    context->IASetVertexBuffers(0, 1, layer.corners.GetAddressOf(), &stride, &offset);
    context->VSSetShader(layer.vertex.Get(), nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    context->PSSetShader(layer.pixel.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 1, layer.view.GetAddressOf());
    context->PSSetSamplers(0, 1, layer.sampler.GetAddressOf());
    context->RSSetState(layer.raster.Get());
    context->RSSetViewports(1, &port);
    context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
    context->OMSetBlendState(layer.blend.Get(), nullptr, 0xffffffff);
    context->OMSetDepthStencilState(layer.depth.Get(), 0);
    context->Draw(3, 0);
    saved.Restore(context.Get());
}
}

namespace appx {
// [options] Capture: one settled XAML frame as appx-xaml.bmp, beside the
// swap-chain capture, to see what the managed tree draws.
static void CaptureXamlFrame(const Frame& frame) {
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = static_cast<LONG>(frame.width);
    info.biHeight = -static_cast<LONG>(frame.height);
    info.biPlanes = 1;
    info.biBitCount = 32;
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(frame.pixels.size());
    FILE* file{};
    if (fopen_s(&file, "appx-xaml.bmp", "wb") != 0) return;
    fwrite(&header, sizeof(header), 1, file);
    fwrite(&info, sizeof(info), 1, file);
    fwrite(frame.pixels.data(), frame.pixels.size(), 1, file);
    fclose(file);
}
// The frame on the host window by GDI, scaled to its client area; transparent
// (premultiplied) pixels show black, where no game frame draws.
static bool ShowXamlFrame(const Frame& frame) {
    HWND window = HostWindow();
    RECT client{};
    if (!window || !IsWindowVisible(window) || IsIconic(window) || !GetClientRect(window, &client)) return false;
    window = ShowXamlFallbackWindow();
    if (!window) return false;
    HDC dc = GetDC(window);
    if (!dc) return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = static_cast<LONG>(frame.width);
    info.bmiHeader.biHeight = -static_cast<LONG>(frame.height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    SetStretchBltMode(dc, HALFTONE);
    StretchDIBits(dc, 0, 0, client.right, client.bottom, 0, 0, frame.width, frame.height, frame.pixels.data(), &info,
                  DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(window, dc);
    return true;
}
void PresentIdleXamlFrame(bool repaint) {
    if (GetCurrentThreadId() != HostThread()) return;
    static bool painting = false; // positioning/drawing can synchronously send WM_PAINT
    if (painting) return;
    Frame pending;
    const auto present = lastPresent.load();
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        if (!latest.width || GetTickCount64() - present <= presentQuiet || (!repaint && latest.shown)) return;
        pending = latest;
    }
    painting = true;
    const bool shown = ShowXamlFrame(pending);
    painting = false;
    bool gamePresented;
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        gamePresented = lastPresent != present;
        if (latest.generation == pending.generation) latest.shown = shown && !gamePresented;
    }
    if (gamePresented) HideXamlFallbackWindow();
}
}
namespace appx {
namespace {
// Stores `dirty` of a width x height premultiplied BGRA image whose row y starts
// at rows + y * stride; a new size takes the whole image.
void StoreFrame(const BYTE* rows, ptrdiff_t stride, UINT width, UINT height, RECT dirty) {
    Frame capturedFrame;
    unsigned generation;
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        auto& frame = latest;
        const RECT all{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        if (frame.width != width || frame.height != height) {
            frame.pixels.assign(static_cast<size_t>(width) * height * 4, 0);
            frame.width = width;
            frame.height = height;
            dirty = all;
            frame.dirty = all;
        }
        IntersectRect(&dirty, &dirty, &all);
        for (LONG y = dirty.top; y < dirty.bottom; ++y)
            memcpy(frame.pixels.data() + (static_cast<size_t>(y) * width + dirty.left) * 4,
                   rows + y * stride + dirty.left * 4, static_cast<size_t>(dirty.right - dirty.left) * 4);
        UnionRect(&frame.dirty, &frame.dirty, &dirty);
        ++frame.generation;
        generation = frame.generation;
        // Frames now come only when something changes, so the capture keeps the
        // newest one, at most every two seconds.
        static ULONGLONG captured = 0;
        if (GetOptions().capture && GetTickCount64() - captured > 2000) {
            captured = GetTickCount64();
            capturedFrame = frame;
        }
        frame.shown = false;
    }
    if (generation <= 3 || generation % 600 == 0) Log("TRACE", "XAML frame %u %ux%u", generation, width, height);
    if (capturedFrame.width) CaptureXamlFrame(capturedFrame);
}

// The managed host's WPF source is a layered window: WPF renders it with its
// own compositor, only where something changed, and hands each result to
// UpdateLayeredWindow(Indirect). The shim keeps its pixels as the XAML frame;
// the native update still maintains the source window's state off-screen.
std::atomic<HWND> xamlWindow{nullptr};
using IndirectCall = BOOL(WINAPI*)(HWND, const UPDATELAYEREDWINDOWINFO*);
using LayeredCall = BOOL(WINAPI*)(HWND, HDC, POINT*, SIZE*, HDC, POINT*, COLORREF, BLENDFUNCTION*, DWORD);
IndirectCall originalIndirect;
LayeredCall originalLayered;
// The presented pixels, copied by GDI into a top-down section of the shim's own,
// which settles row order and works for any source DC.
struct Copy {
    std::mutex mutex;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    BYTE* bits = nullptr;
    LONG width = 0, height = 0;
    void Reset() {
        if (dc) {
            SelectObject(dc, previous);
            DeleteDC(dc);
        }
        if (bitmap) DeleteObject(bitmap);
        dc = nullptr;
        bitmap = nullptr;
        previous = nullptr;
        bits = nullptr;
        width = height = 0;
    }
    bool Resize(LONG w, LONG h) {
        if (bits && w == width && h == height) return true;
        if (!dc) dc = CreateCompatibleDC(nullptr);
        if (!dc) return false;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* memory = nullptr;
        HBITMAP made = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &memory, nullptr, 0);
        if (!made) return false;
        HGDIOBJ old = SelectObject(dc, made);
        if (bitmap)
            DeleteObject(bitmap);
        else
            previous = old;
        bitmap = made;
        bits = static_cast<BYTE*>(memory);
        width = w;
        height = h;
        return true;
    }
};
Copy copy;
BOOL TakeLayered(HWND window, HDC source, const POINT* origin, const SIZE* size, const RECT* dirty) {
    SIZE currentSize{};
    if (!size) {
        RECT bounds{};
        if (!GetWindowRect(window, &bounds)) return FALSE;
        currentSize = SIZE{bounds.right - bounds.left, bounds.bottom - bounds.top};
        size = &currentSize;
    }
    if (!source || !size || size->cx <= 0 || size->cy <= 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const POINT at = origin ? *origin : POINT{};
    RECT changed{0, 0, size->cx, size->cy};
    std::unique_lock<std::mutex> lock(copy.mutex);
    if (window != xamlWindow) return TRUE; // the source was withdrawn while this present waited
    if (dirty && copy.bits && copy.width == size->cx && copy.height == size->cy)
        IntersectRect(&changed, &changed, dirty);
    if (!copy.Resize(size->cx, size->cy) ||
        (!IsRectEmpty(&changed) &&
         !BitBlt(copy.dc, changed.left, changed.top, changed.right - changed.left, changed.bottom - changed.top, source,
                 at.x + changed.left, at.y + changed.top, SRCCOPY))) {
        static bool logged = false;
        if (!logged) Log("FAIL", "XAML layered present not copied (error %lu); frame dropped", GetLastError());
        logged = true;
        return FALSE;
    }
    GdiFlush();
    StoreFrame(copy.bits, static_cast<ptrdiff_t>(size->cx) * 4, static_cast<UINT>(size->cx),
               static_cast<UINT>(size->cy), changed);
    lock.unlock();
    PresentIdleXamlFrame();
    return TRUE;
}
thread_local bool forwardingLayered = false;
BOOL WINAPI XamlIndirect(HWND window, const UPDATELAYEREDWINDOWINFO* info) {
    if (!window || window != xamlWindow || forwardingLayered) return originalIndirect(window, info);
    forwardingLayered = true;
    const BOOL result = originalIndirect(window, info);
    forwardingLayered = false;
    if (result && info && info->hdcSrc) TakeLayered(window, info->hdcSrc, info->pptSrc, info->psize, info->prcDirty);
    return result;
}
BOOL WINAPI XamlLayered(HWND window, HDC target, POINT* position, SIZE* size, HDC source, POINT* origin, COLORREF key,
                        BLENDFUNCTION* blend, DWORD flags) {
    if (!window || window != xamlWindow || forwardingLayered)
        return originalLayered(window, target, position, size, source, origin, key, blend, flags);
    forwardingLayered = true;
    const BOOL result = originalLayered(window, target, position, size, source, origin, key, blend, flags);
    forwardingLayered = false;
    if (result && source) TakeLayered(window, source, origin, size, nullptr);
    return result;
}
}
}
namespace appx {
void ShutdownXamlLayer() {
    xamlWindow = nullptr;
    {
        std::lock_guard<std::mutex> lock(copy.mutex);
        copy.Reset();
    }
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        latest = Frame{};
    }
    // Releasing D3D objects during DLL detach can wait on Wine's graphics
    // worker while holding the loader lock. The view's loop has ended here.
    std::lock_guard<std::mutex> resources(layerMutex);
    layer = Layer{};
    lastPresent = 0;
}
}
// A managed game's newest XAML frame, premultiplied BGRA rows of `stride`
// bytes; null or an empty size withdraws the layer.
extern "C" HRESULT __cdecl ShimSubmitXamlFrame(const void* bgra, UINT width, UINT height, UINT stride) noexcept {
    if (bgra && (!width || !height || stride < width * 4)) return E_INVALIDARG;
    if (!bgra) {
        {
            std::lock_guard<std::mutex> lock(appx::frameMutex);
            ++appx::latest.generation;
            appx::latest.width = appx::latest.height = 0;
        }
        appx::HideXamlFallbackWindow();
        return S_OK;
    }
    appx::StoreFrame(static_cast<const BYTE*>(bgra), stride, width, height,
                     RECT{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)});
    appx::PresentIdleXamlFrame();
    return S_OK;
}
// Takes a layered window's presented pixels as the XAML frame instead of
// showing them. E_NOTIMPL when UpdateLayeredWindow cannot be intercepted here
// (x64): the caller submits frames itself.
extern "C" HRESULT __cdecl ShimCaptureXamlWindow(HWND window) noexcept {
    if (!window || !IsWindow(window)) return E_INVALIDARG;
    static const bool hooked = [] {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        appx::originalIndirect = reinterpret_cast<appx::IndirectCall>(
            appx::Hotpatch(user32, "UpdateLayeredWindowIndirect", reinterpret_cast<void*>(&appx::XamlIndirect), true));
        appx::originalLayered = reinterpret_cast<appx::LayeredCall>(
            appx::Hotpatch(user32, "UpdateLayeredWindow", reinterpret_cast<void*>(&appx::XamlLayered), true));
        if (!appx::originalIndirect || !appx::originalLayered)
            appx::Log("UNIMPL", "UpdateLayeredWindow hooks for the XAML source (indirect=%p plain=%p)",
                      reinterpret_cast<void*>(appx::originalIndirect), reinterpret_cast<void*>(appx::originalLayered));
        return appx::originalIndirect && appx::originalLayered;
    }();
    if (!hooked) return E_NOTIMPL;
    {
        std::lock_guard<std::mutex> lock(appx::copy.mutex);
        appx::copy.Reset(); // a replacement source's first present must copy the whole image
        appx::xamlWindow = window;
    }
    return S_OK;
}
// How many XAML frames the shim has received, for diagnostics and probes.
extern "C" UINT __cdecl ShimXamlFrameCount() noexcept {
    std::lock_guard<std::mutex> lock(appx::frameMutex);
    return appx::latest.generation;
}
