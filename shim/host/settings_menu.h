#pragma once
#include <windows.h>
namespace appx {
inline constexpr UINT SettingsMenuId = 0x1F00;
void InitializeSettingsMenu(HWND window);
void ShutdownSettingsMenu();
}
