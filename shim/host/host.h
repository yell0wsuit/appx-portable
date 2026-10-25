#pragma once
#include <windows.h>
#include <functional>
#include <string>
namespace appx {
HWND HostWindow() noexcept;
// A child window covering the host's client area, on top of earlier ones,
// for one redirected swap chain. Closing it (WM_CLOSE) destroys it.
HWND CreateSurfaceWindow() noexcept;
DWORD HostThread() noexcept;
void ActivateHostWindow();
bool IsHostFullscreen() noexcept;
// Writes the window's size, position and mode now (normally saved on changes and on exit).
void SaveHostPlacement() noexcept;
HRESULT SetHostFullscreen(bool value);
// The pointer shown over the game's client area; nullptr hides it. The arrow
// until this is first called.
void SetHostCursor(HCURSOR value) noexcept;
// ApplicationView.Title: shown before the app's name in the caption; empty
// shows the name alone. Before the window exists, it names the new window.
void SetHostTitle(const std::wstring& title);
HRESULT PostHostWork(int priority, std::function<void()> work);
HRESULT ShowGameSettings();
HRESULT RunHost(const std::function<HRESULT()>& initialize);
}
