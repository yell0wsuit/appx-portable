// .NET Native's forwarding EXE imports the game DLL before a proxy can load
// the shim. This launcher supplies the same entry point with portable startup.
#include <windows.h>
extern "C" __declspec(dllimport) int __cdecl ShimRunNativeApplication() noexcept;
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return ShimRunNativeApplication(); }
