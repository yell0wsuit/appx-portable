// A system DLL the game imports from its own folder, so Windows loads this
// first: it loads appxportable.dll and forwards each export to the real DLL.
// proxy_names.h (from scripts/gen_proxy.py) names the DLL being stood in for.
#include "proxy_names.h"
#include <windows.h>
#include <cstdio>
extern "C" __declspec(dllimport) void __cdecl ShimBootstrap();
extern "C" void* appx_targets[];
extern "C" const char* appx_export_names[];
extern "C" const unsigned appx_export_count;
static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
static HMODULE self;
static BOOL CALLBACK Initialize(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[32768]{};
    UINT length = GetSystemDirectoryW(path, 32768);
    if (!length || length + 40 >= 32768) return FALSE;
    wcscat_s(path, L"\\" APPX_PROXY_FILE_W);
    HMODULE module = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module || module == self) return FALSE;
    for (unsigned i = 0; i != appx_export_count; ++i)
        appx_targets[i] = reinterpret_cast<void*>(GetProcAddress(module, appx_export_names[i]));
    // Keep the module loaded: tail thunks hold addresses for process lifetime.
    return TRUE;
}
extern "C" void __cdecl appx_resolve(unsigned index) {
    if (!InitOnceExecuteOnce(&once, Initialize, nullptr, nullptr) || !appx_targets[index]) {
        char message[256];
        snprintf(message, sizeof(message), "appx-portable proxy: missing real %s export %s (error %lu)\n",
                 APPX_PROXY_FILE, appx_export_names[index], GetLastError());
        OutputDebugStringA(message);
        // Never return through an ABI-unknown signature with a fabricated result.
        TerminateProcess(GetCurrentProcess(), ERROR_PROC_NOT_FOUND);
    }
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        self = module;
        ShimBootstrap();
    }
    return TRUE;
}
