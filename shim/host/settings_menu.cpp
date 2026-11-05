#include "settings_menu.h"
#include "host.h"
namespace appx {
static HWND owner;
static HHOOK hook;
static LRESULT CALLBACK SettingsMessage(int code, WPARAM removed, LPARAM message) {
    if (code == HC_ACTION && removed == PM_REMOVE) {
        auto& msg = *reinterpret_cast<MSG*>(message);
        if ((msg.hwnd == owner || IsChild(owner, msg.hwnd) || GetAncestor(msg.hwnd, GA_ROOTOWNER) == owner) &&
            msg.message == WM_KEYDOWN && msg.wParam == VK_OEM_COMMA && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (!(msg.lParam & (1L << 30))) ShowGameSettings();
            msg.message = WM_NULL;
        }
    }
    return CallNextHookEx(hook, code, removed, message);
}
void InitializeSettingsMenu(HWND window) {
    owner = window;
    auto menu = GetSystemMenu(window, FALSE);
    InsertMenuW(menu, SC_CLOSE, MF_BYCOMMAND | MF_STRING, SettingsMenuId, L"Game settings\tCtrl+,");
    InsertMenuW(menu, SC_CLOSE, MF_BYCOMMAND | MF_SEPARATOR, 0, nullptr);
    // Covers the host loop, CoreDispatcher.ProcessEvents and nested XAML
    // loops. ShowGameSettings queues the command; no menu runs inside a hook.
    hook = SetWindowsHookExW(WH_GETMESSAGE, SettingsMessage, nullptr, GetCurrentThreadId());
}
void ShutdownSettingsMenu() {
    if (hook) UnhookWindowsHookEx(hook);
    hook = nullptr;
    owner = nullptr;
}
}
