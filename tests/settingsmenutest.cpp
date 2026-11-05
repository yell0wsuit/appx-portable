#include "host/settings_menu.h"
#include <windows.h>
#include <cstdio>
static unsigned requested;
namespace appx {
HRESULT ShowGameSettings() { ++requested; return S_OK; }
}
#define CHECK(condition) do { if (!(condition)) { printf("FAIL: %s line %d\n", #condition, __LINE__); return 1; } } while (0)
int main() {
    auto owner = CreateWindowW(L"STATIC", L"settings test", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100,
                               nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    auto child = CreateWindowW(L"STATIC", L"child", WS_CHILD, 0, 0, 10, 10, owner, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    CHECK(owner && child);
    appx::InitializeSettingsMenu(owner);
    CHECK(GetMenuState(GetSystemMenu(owner, FALSE), appx::SettingsMenuId, MF_BYCOMMAND) != UINT(-1));
    BYTE keys[256]{};
    keys[VK_CONTROL] = 0x80;
    CHECK(SetKeyboardState(keys));
    MSG msg{};
    CHECK(PostMessageW(child, WM_KEYDOWN, VK_OEM_COMMA, 1));
    // This separate message pump models a game's own CoreDispatcher/XAML loop.
    CHECK(PeekMessageW(&msg, child, WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE));
    CHECK(requested == 0);
    CHECK(PeekMessageW(&msg, child, WM_KEYDOWN, WM_KEYDOWN, PM_REMOVE));
    CHECK(msg.message == WM_NULL && requested == 1);
    CHECK(PostMessageW(child, WM_KEYDOWN, VK_OEM_COMMA, 1 | (1L << 30)));
    CHECK(PeekMessageW(&msg, child, WM_KEYDOWN, WM_KEYDOWN, PM_REMOVE));
    CHECK(msg.message == WM_NULL && requested == 1);
    keys[VK_CONTROL] = 0;
    CHECK(SetKeyboardState(keys));
    CHECK(PostMessageW(child, WM_KEYDOWN, VK_OEM_COMMA, 1));
    CHECK(PeekMessageW(&msg, child, WM_KEYDOWN, WM_KEYDOWN, PM_REMOVE));
    CHECK(msg.message == WM_KEYDOWN && requested == 1);
    appx::ShutdownSettingsMenu();
    keys[VK_CONTROL] = 0x80;
    CHECK(SetKeyboardState(keys));
    CHECK(PostMessageW(child, WM_KEYDOWN, VK_OEM_COMMA, 1));
    CHECK(PeekMessageW(&msg, child, WM_KEYDOWN, WM_KEYDOWN, PM_REMOVE));
    CHECK(msg.message == WM_KEYDOWN && requested == 1);
    keys[VK_CONTROL] = 0;
    SetKeyboardState(keys);
    DestroyWindow(owner);
    puts("PASS: settings shortcut in nested pump, repeat suppression, and cleanup");
    return 0;
}
