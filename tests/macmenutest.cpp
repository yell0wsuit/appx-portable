#include "host/mac_menu.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

static bool ready;
namespace appx {
HRESULT ShowGameSettings() { return S_OK; }
void Log(const char*, const char* format, ...) {
    if (!strcmp(format, "macOS Game Menu ready")) ready = true;
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    puts("");
    va_end(args);
}
}
static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_TIMER && wp == 1) PostQuitMessage(0);
    return DefWindowProcW(window, message, wp, lp);
}
int main(int argc, char**) {
    WNDCLASSW cls{};
    cls.lpfnWndProc = WindowProc;
    cls.lpszClassName = L"AppxMenuTest";
    cls.hInstance = GetModuleHandleW(nullptr);
    if (!RegisterClassW(&cls)) return 1;
    auto window = CreateWindowW(cls.lpszClassName, L"Game Menu bridge probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 400, 200, nullptr, nullptr, cls.hInstance, nullptr);
    if (!window) return 2;
    appx::InitializeMacGameMenu(window);
    SetTimer(window, 1, 3000, nullptr);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    appx::ShutdownMacGameMenu(window);
    DestroyWindow(window);
    if (argc > 1 && !ready) return 3;
    printf("PASS: macOS bridge %s\n", ready ? "loaded and installed" : "optional fallback");
    return 0;
}
