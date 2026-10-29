#include "islands.h"
#include "package/package.h"
#include <fstream>
#include <atomic>
static std::atomic<bool> managedCoreWindow{false};
static std::atomic<bool> nativeXaml{false};
// The managed launcher selects this before invoking an MTA game entry point.
// Keep CLR detection separate: CoreWindow games still need activation hooks.
extern "C" void __cdecl ShimSetManagedCoreWindowMode() noexcept { managedCoreWindow = true; }
namespace appx {
void SetNativeXamlMode() noexcept { nativeXaml = true; }
bool ManagedMode() noexcept {
    static const bool managed = [] {
        try {
            const auto& package = CurrentPackage();
            if (package.executable.empty()) return false;
            std::ifstream file(package.directory / package.executable, std::ios::binary);
            IMAGE_DOS_HEADER dos{};
            IMAGE_NT_HEADERS nt{};
            if (!file.read(reinterpret_cast<char*>(&dos), sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
                dos.e_lfanew < 0)
                return false;
            file.seekg(dos.e_lfanew);
            if (!file.read(reinterpret_cast<char*>(&nt), sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
                nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC)
                return false;
            return nt.OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR &&
                   nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress != 0;
        } catch (...) {
            return false;
        }
    }();
    return managed;
}
bool IslandsMode() noexcept { return nativeXaml.load() || (ManagedMode() && !managedCoreWindow.load()); }
}
