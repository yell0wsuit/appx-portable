#include "islands.h"
#include "package/package.h"
#include <fstream>
namespace appx {
bool IslandsMode() noexcept {
    static const bool managed = [] {
        try {
            const auto& package = CurrentPackage();
            if (package.executable.empty()) return false;
            std::ifstream file(package.directory / package.executable, std::ios::binary);
            IMAGE_DOS_HEADER dos{};
            IMAGE_NT_HEADERS32 nt{};
            if (!file.read(reinterpret_cast<char*>(&dos), sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
                dos.e_lfanew < 0)
                return false;
            file.seekg(dos.e_lfanew);
            if (!file.read(reinterpret_cast<char*>(&nt), sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
                nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
                return false;
            return nt.OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR &&
                   nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress != 0;
        } catch (...) {
            return false;
        }
    }();
    return managed;
}
}
