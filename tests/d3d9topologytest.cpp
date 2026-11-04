// WPF switches between strips and fans. A dynamic fan on MoltenVK used to
// draw only one triangle of a quad; a full strip alone does not catch this.
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT size = 32;
struct Vertex {
    float x, y, z, w;
    DWORD color;
};
bool Check(HRESULT hr, const char* operation) {
    if (SUCCEEDED(hr)) return true;
    printf("d3d9 topology: %s hr=%08lx\n", operation, hr);
    return false;
}
bool Pixels(IDirect3DDevice9* device, IDirect3DSurface9* target, IDirect3DSurface9* readback, DWORD color,
            const char* shape) {
    if (!Check(device->GetRenderTargetData(target, readback), "readback")) return false;
    D3DLOCKED_RECT locked{};
    if (!Check(readback->LockRect(&locked, nullptr, D3DLOCK_READONLY), "lock readback")) return false;
    unsigned errors = 0;
    for (UINT y = 0; y < size; ++y)
        for (UINT x = 0; x < size; ++x)
            if (reinterpret_cast<DWORD*>(static_cast<BYTE*>(locked.pBits) + y * locked.Pitch)[x] != color) ++errors;
    readback->UnlockRect();
    printf("d3d9 topology: %s %u wrong pixels\n", shape, errors);
    return !errors;
}
}
int main() {
    HWND window = CreateWindowW(L"STATIC", L"topology", WS_POPUP, 0, 0, size, size, nullptr, nullptr, nullptr, nullptr);
    ComPtr<IDirect3D9> d3d;
    d3d.Attach(Direct3DCreate9(D3D_SDK_VERSION));
    if (!window || !d3d) return 1;
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.BackBufferWidth = pp.BackBufferHeight = size;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    ComPtr<IDirect3DDevice9> device;
    if (!Check(d3d->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device),
               "device"))
        return 1;
    ComPtr<IDirect3DSurface9> target, readback;
    if (!Check(device->CreateRenderTarget(size, size, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr),
               "target") ||
        !Check(device->CreateOffscreenPlainSurface(size, size, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr),
               "readback surface"))
        return 1;
    const Vertex vertices[] = {
        {-.5f, -.5f, 0, 1, 0xff0000ff},   {31.5f, -.5f, 0, 1, 0xff0000ff}, {-.5f, 31.5f, 0, 1, 0xff0000ff},
        {31.5f, 31.5f, 0, 1, 0xff0000ff}, {-.5f, -.5f, 0, 1, 0xffff0000},  {31.5f, -.5f, 0, 1, 0xffff0000},
        {31.5f, 31.5f, 0, 1, 0xffff0000}, {-.5f, 31.5f, 0, 1, 0xffff0000},
    };
    constexpr DWORD fvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;
    ComPtr<IDirect3DVertexBuffer9> buffer;
    if (!Check(device->CreateVertexBuffer(sizeof(vertices), D3DUSAGE_WRITEONLY, fvf, D3DPOOL_DEFAULT, &buffer, nullptr),
               "vertices"))
        return 1;
    void* data = nullptr;
    if (!Check(buffer->Lock(0, sizeof(vertices), &data, 0), "lock vertices")) return 1;
    memcpy(data, vertices, sizeof(vertices));
    buffer->Unlock();
    device->SetRenderTarget(0, target.Get());
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetFVF(fvf);
    device->SetStreamSource(0, buffer.Get(), 0, sizeof(Vertex));
    bool passed = true;
    for (unsigned draw = 0; draw < 4; ++draw) {
        const bool fan = draw % 2;
        device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff000000, 1, 0);
        if (!Check(device->BeginScene(), "begin")) return 1;
        const HRESULT hr = device->DrawPrimitive(fan ? D3DPT_TRIANGLEFAN : D3DPT_TRIANGLESTRIP, fan ? 4 : 0, 2);
        device->EndScene();
        passed =
            Check(hr, "draw") &&
            Pixels(device.Get(), target.Get(), readback.Get(), fan ? 0xffff0000 : 0xff0000ff, fan ? "fan" : "strip") &&
            passed;
    }
    return passed ? 0 : 1;
}
