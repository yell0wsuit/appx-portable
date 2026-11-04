#pragma once
#include <windows.h>
namespace appx {
// Sends every caller of a system DLL export to `hook`, returning the address
// that runs the original, or null when the export cannot be patched. x86
// system DLLs start each export with `mov edi, edi` after five padding bytes:
// jump from the padding and call the original past the two-byte prologue.
// Wine's DLLs use `xchg ax, ax` instead, accepted only when `wineNop` is set.
inline void* Hotpatch(HMODULE module, const char* name, void* hook, bool wineNop = false) noexcept {
#ifdef _WIN64
    // The x86 hotpatch layout and rel32 range do not apply to x64.
    (void)module;
    (void)name;
    (void)hook;
    (void)wineNop;
    return nullptr;
#else
    auto* entry = module ? reinterpret_cast<BYTE*>(GetProcAddress(module, name)) : nullptr;
    if (!entry || !((entry[0] == 0x8B && entry[1] == 0xFF) || (wineNop && entry[0] == 0x66 && entry[1] == 0x90)))
        return nullptr;
    for (int i = 1; i <= 5; ++i)
        if (entry[-i] != 0xCC && entry[-i] != 0x90) return nullptr;
    DWORD old;
    if (!VirtualProtect(entry - 5, 7, PAGE_EXECUTE_READWRITE, &old)) return nullptr;
    entry[-5] = 0xE9; // jmp rel32 to the hook
    *reinterpret_cast<INT32*>(entry - 4) = static_cast<INT32>(static_cast<BYTE*>(hook) - entry);
    InterlockedExchange16(reinterpret_cast<SHORT*>(entry), static_cast<SHORT>(0xF9EB)); // jmp short -7
    DWORD ignored;
    VirtualProtect(entry - 5, 7, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), entry - 5, 7);
    return entry + 2;
#endif
}
}
