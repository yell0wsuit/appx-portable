#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
namespace appx {
// A class the package registers for itself (windows.activatableClass.inProcessServer).
struct PackageClass {
    std::wstring id;
    std::filesystem::path module;
};
// What the game's AppxManifest.xml says about it. Read once, from the folder
// holding the game executable; fields stay empty when the manifest is missing.
struct PackageInfo {
    std::filesystem::path directory;
    std::wstring identityName, publisher, version, architecture;
    std::wstring publisherId, fullName, familyName; // derived as Windows does
    // As written: the <Properties> and <VisualElements> names, either of which
    // may be an "ms-resource:" reference. PackageDisplayName() is the app's name.
    std::wstring displayName, tileName;
    std::wstring applicationId, executable, entryPoint;
    std::wstring smallLogo; // relative to directory, as written in the manifest
    std::vector<PackageClass> classes;
    bool loaded = false;
    bool universal = false; // Windows 10 TargetDeviceFamily manifest
};
const PackageInfo& CurrentPackage() noexcept;
// Parses a manifest; exposed for tests.
PackageInfo ReadPackage(const std::filesystem::path& directory);
// The first of the manifest's names that resolves, "ms-resource:" ones from
// resources.pri; empty when none does. Exposed for tests.
std::wstring ResolveDisplayName(const PackageInfo& package);
// The app's name, as resolved above or else the executable's; never empty.
// Resolved on first use, not with the manifest: reading resources.pri activates
// Windows classes, and the activation hook reads the package.
const std::wstring& PackageDisplayName() noexcept;
// A package file as Windows resolves it: the resources.pri candidate for this
// display (Assets\scale-100\SmallLogo.png for Assets\SmallLogo.png), or the
// path as written when the package indexes no such file.
std::filesystem::path ResolvePackageFile(const PackageInfo& package, const std::wstring& relative);
// Windows' publisher ID: the first 8 bytes of SHA-256 over the UTF-16LE
// publisher, in Crockford base32 (13 characters).
std::wstring PublisherId(const std::wstring& publisher);
}
