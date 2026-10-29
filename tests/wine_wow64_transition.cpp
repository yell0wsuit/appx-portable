// Rewriting an executed x86 call thunk exposes Wine/Rosetta mode transitions.
// The first function reads TLS; the second crosses ntdll's Unix-call bridge.
#include <windows.h>
#include <cstdio>
#include <cstring>

int main() {
    using GetId = DWORD(WINAPI*)();
    using GetTime = LONGLONG(WINAPI*)();
    GetId getId = &GetCurrentThreadId;
    GetTime getTime =
        reinterpret_cast<GetTime>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetSystemTimePrecise"));
    if (!getTime) return 1;
    auto code =
        static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!code) return 2;
    // mov eax, target; call eax; ret
    const unsigned char thunk[] = {0xb8, 0, 0, 0, 0, 0xff, 0xd0, 0xc3};
    memcpy(code, thunk, sizeof(thunk));
    memcpy(code + 1, &getId, sizeof(getId));
    if (!FlushInstructionCache(GetCurrentProcess(), code, sizeof(thunk))) return 3;
    if (reinterpret_cast<GetId>(code)() != GetCurrentThreadId()) return 4;
    puts("generated TLS call passed");
    fflush(stdout);
    memcpy(code + 1, &getTime, sizeof(getTime));
    if (!FlushInstructionCache(GetCurrentProcess(), code, sizeof(thunk))) return 5;
    if (reinterpret_cast<GetTime>(code)() <= 0) return 6;
    for (int i = 0; i < 10000; ++i) {
        LARGE_INTEGER counter;
        if (!QueryPerformanceCounter(&counter) || getTime() <= 0) return 7;
    }
    VirtualFree(code, 0, MEM_RELEASE);
    puts("generated call rewrite and WoW64 transitions passed");
    return 0;
}
