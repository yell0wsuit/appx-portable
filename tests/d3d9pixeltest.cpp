// D3D9 pixel centers are integers. A rectangle from (-0.5, -0.5) to
// (width - 0.5, height - 0.5) must fill every pixel, including its far edges.
// Wine's Vulkan backend must apply this without shifting D3D10/11 viewports.
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
int main() {
    HWND window = CreateWindowW(L"STATIC", L"pixel center", WS_POPUP, 0, 0, 32, 32, nullptr, nullptr, nullptr, nullptr);
    ComPtr<IDirect3D9> d3d;
    d3d.Attach(Direct3DCreate9(D3D_SDK_VERSION));
    if (!d3d) return 1;
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.BackBufferWidth = 32;
    pp.BackBufferHeight = 32;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    ComPtr<IDirect3DDevice9> device;
    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
    if (FAILED(hr)) {
        printf("create %08lx\n", hr);
        return 2;
    }
    ComPtr<IDirect3DSurface9> target, readback;
    hr = device->CreateRenderTarget(32, 32, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr);
    if (SUCCEEDED(hr))
        hr = device->CreateOffscreenPlainSurface(32, 32, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr);
    if (FAILED(hr)) {
        printf("surfaces %08lx\n", hr);
        return 3;
    }
    device->SetRenderTarget(0, target.Get());
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    struct Vertex {
        float x, y, z, w;
        DWORD color;
    };
    const Vertex vertices[] = {{-.5f, -.5f, 0, 1, 0xffff0000},
                               {31.5f, -.5f, 0, 1, 0xffff0000},
                               {-.5f, 31.5f, 0, 1, 0xffff0000},
                               {31.5f, 31.5f, 0, 1, 0xffff0000}};
    device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff000000, 1, 0);
    device->BeginScene();
    hr = device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(Vertex));
    device->EndScene();
    if (FAILED(hr)) {
        printf("draw %08lx\n", hr);
        return 3;
    }
    hr = device->GetRenderTargetData(target.Get(), readback.Get());
    if (FAILED(hr)) {
        printf("read %08lx\n", hr);
        return 4;
    }
    D3DLOCKED_RECT lock{};
    hr = readback->LockRect(&lock, nullptr, D3DLOCK_READONLY);
    if (FAILED(hr)) {
        printf("lock %08lx\n", hr);
        return 4;
    }
    int errors = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if (reinterpret_cast<DWORD*>(static_cast<BYTE*>(lock.pBits) + y * lock.Pitch)[x] != 0xffff0000) ++errors;
    printf("d3d9 pixels: %d wrong (last=%08lx)\n", errors,
           reinterpret_cast<DWORD*>(static_cast<BYTE*>(lock.pBits) + 31 * lock.Pitch)[31]);
    readback->UnlockRect();
    return errors ? 5 : 0;
}
