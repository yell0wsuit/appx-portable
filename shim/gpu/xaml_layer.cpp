#include "xaml_layer.h"
#include "log.h"
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
// rendered offscreen on the UI thread and blended over the game's swap chain
// when it presents.
struct Frame {
    std::vector<BYTE> pixels;
    UINT width = 0, height = 0;
    unsigned generation = 0;
};
std::mutex frameMutex;
Frame latest;

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
    std::lock_guard<std::mutex> lock(frameMutex);
    if (!latest.width || !latest.height) return false;
    if (latest.generation == layer.generation && layer.view) return true;
    if (latest.width != layer.width || latest.height != layer.height || !layer.texture) {
        D3D11_TEXTURE2D_DESC desc{latest.width,
                                  latest.height,
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
        layer.width = latest.width;
        layer.height = latest.height;
    }
    context->UpdateSubresource(layer.texture.Get(), 0, nullptr, latest.pixels.data(), latest.width * 4, 0);
    layer.generation = latest.generation;
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
    {
        std::lock_guard<std::mutex> lock(frameMutex);
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

// A managed game's newest XAML frame, premultiplied BGRA rows of `stride`
// bytes; null or an empty size withdraws the layer.
extern "C" HRESULT __cdecl ShimSubmitXamlFrame(const void* bgra, UINT width, UINT height, UINT stride) noexcept {
    if (bgra && (!width || !height || stride < width * 4)) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(appx::frameMutex);
    auto& frame = appx::latest;
    ++frame.generation;
    if (!bgra) {
        frame.width = frame.height = 0;
        return S_OK;
    }
    frame.pixels.resize(static_cast<size_t>(width) * height * 4);
    for (UINT y = 0; y < height; ++y)
        memcpy(frame.pixels.data() + static_cast<size_t>(y) * width * 4,
               static_cast<const BYTE*>(bgra) + static_cast<size_t>(y) * stride, static_cast<size_t>(width) * 4);
    frame.width = width;
    frame.height = height;
    return S_OK;
}
