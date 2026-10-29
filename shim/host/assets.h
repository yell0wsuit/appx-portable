#pragma once
#include <windows.h>
#include <filesystem>
#include <optional>
#include <string>

namespace appx {
// A raw .appx extraction keeps the package's OPC percent-encoded names
// (%21_Name.xml, User%20Controls); installed packages have them decoded, and
// games open the decoded names. Decodes every such name under the game folder,
// except the SaveData folder. Existing decoded names are never overwritten.
// `count`, when given, receives how many names were renamed.
HRESULT DecodePackageNames(const std::filesystem::path& gameDirectory, unsigned* count = nullptr) noexcept;
// One name's decoded form, or nothing when it has no valid escapes or would
// decode to an invalid file name.
std::optional<std::wstring> DecodePackageName(const std::wstring& name);
// Store packages supply shell logos rather than executable icon resources.
// Decodes one (PNG or JPEG) and scales it to a width x height icon, or returns
// null for a missing or undecodable file.
HICON LoadGameIcon(const std::filesystem::path& path, int width, int height) noexcept;
}
