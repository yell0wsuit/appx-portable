#include "mac_menu.h"
#include "host.h"
#include "log.h"
#include <winternl.h>
#include <cstdint>

namespace appx {
using UnixCall = LONG(WINAPI*)(uint64_t, unsigned int, void*);
static UnixCall call;
static uint64_t functions;
static UINT_PTR timer;
static bool ready;
static void CALLBACK PollMenu(HWND, UINT, UINT_PTR, DWORD) {
    uint32_t state = 0;
    if (call(functions, 1, &state) < 0) return;
    if ((state & 1) && !ready) {
        ready = true;
        Log("INFO", "macOS Game Menu ready");
    }
    if (state & 2) ShowGameSettings();
}

void InitializeMacGameMenu(HWND window) {
    // Wine's private Unix-call ABI is optional. Windows and older/unbundled
    // Wine retain the system-menu entry and Ctrl+, without this bridge.
    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!GetProcAddress(ntdll, "wine_get_version")) return;
    Log("TRACE", "initializing macOS Game Menu bridge");
    auto dispatcher = reinterpret_cast<UnixCall*>(GetProcAddress(ntdll, "__wine_unix_call_dispatcher"));
    using Query = LONG(NTAPI*)(HANDLE, const void*, int, void*, SIZE_T, SIZE_T*);
    auto query = reinterpret_cast<Query>(GetProcAddress(ntdll, "NtQueryVirtualMemory"));
    if (!dispatcher || !*dispatcher || !query) {
        Log("TRACE", "macOS Game Menu Unix-call API unavailable dispatcher=%p query=%p", dispatcher, query);
        return;
    }
    wchar_t name[] = L"appxmenu.dll";
    UNICODE_STRING library{sizeof(name) - sizeof(wchar_t), sizeof(name), name};
    uint64_t handles[2]{};
    // MemoryWineLoadUnixLibByName (Wine 11.18); WoW64 converts the string
    // and selects __wine_unix_call_wow64_funcs for a 32-bit host.
    const auto status = query(GetCurrentProcess(), &library, 1002, handles, sizeof(handles), nullptr);
    if (status < 0) {
        Log("TRACE", "macOS Game Menu bridge unavailable status=%08lx", status);
        return;
    }
    call = *dispatcher;
    functions = handles[1];
    uint32_t version = 1;
    if (call(functions, 0, &version) < 0) return;
    timer = SetTimer(window, 0, 100, PollMenu);
    if (!timer) call(functions, 2, nullptr);
    // Keep the Unix library loaded: Cocoa blocks and action targets may still
    // be queued when the Windows UI loop stops.
}

void ShutdownMacGameMenu(HWND window) {
    if (timer) {
        KillTimer(window, timer);
        timer = 0;
        call(functions, 2, nullptr);
    }
    ready = false;
}
}
