#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
int main() {
    auto* module = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!module) {
        printf("FAIL load %lu\n", GetLastError());
        return 1;
    }
    auto compile = reinterpret_cast<pD3DCompile>(GetProcAddress(module, "D3DCompile"));
    if (!compile) return 2;
    const char* shader = "float4 main() : SV_Target { return float4(1,0,0,1); }";
    ID3DBlob* code = nullptr;
    ID3DBlob* error = nullptr;
    auto hr = compile(shader, strlen(shader), "proxytest", nullptr, nullptr, "main", "ps_4_0", 0, 0, &code, &error);
    if (error) {
        printf("compiler: %s\n", static_cast<const char*>(error->GetBufferPointer()));
        error->Release();
    }
    if (FAILED(hr) || !code || !code->GetBufferSize()) {
        printf("FAIL compile %08lx\n", hr);
        return 3;
    }
    auto disassemble = reinterpret_cast<pD3DDisassemble>(GetProcAddress(module, "D3DDisassemble"));
    if (!disassemble) return 4;
    ID3DBlob* text = nullptr;
    hr = disassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr, &text);
    code->Release();
    if (FAILED(hr) || !text) return 5;
    text->Release();
    puts("proxytest: compile and disassemble passed");

    // Games import XAudio2Create by ordinal 1; the proxy must keep the real ordinals.
    auto* audio = LoadLibraryW(L"XAudio2_8.dll");
    if (!audio) return 6;
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(audio, path, MAX_PATH);
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    if (_wcsnicmp(path, system, wcslen(system)) == 0) {
        printf("FAIL loaded the system XAudio2_8.dll, not the proxy: %ls\n", path);
        return 7;
    }
    using CreateCall = HRESULT(WINAPI*)(IUnknown**, UINT32, UINT32);
    auto create = reinterpret_cast<CreateCall>(GetProcAddress(audio, MAKEINTRESOURCEA(1)));
    if (!create || create != reinterpret_cast<CreateCall>(GetProcAddress(audio, "XAudio2Create"))) return 8;
    IUnknown* engine = nullptr;
    hr = create(&engine, 0, 0x00000001 /* XAUDIO2_DEFAULT_PROCESSOR on Windows 8+ */);
    if (FAILED(hr) || !engine) {
        printf("FAIL XAudio2Create %08lx\n", hr);
        return 9;
    }
    engine->Release();
    puts("proxytest: XAudio2Create by ordinal passed");
    return 0;
}
