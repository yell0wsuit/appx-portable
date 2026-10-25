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
    std::wstring displayName;                       // never empty: falls back to the executable name
    std::wstring applicationId, executable, entryPoint;
    std::wstring smallLogo; // relative to directory, as written in the manifest
    std::vector<PackageClass> classes;
    bool loaded = false;
    bool universal = false; // Windows 10 TargetDeviceFamily manifest
};
const PackageInfo& CurrentPackage() noexcept;
// Parses a manifest; exposed for tests.
PackageInfo ReadPackage(const std::filesystem::path& directory);
// Windows' publisher ID: the first 8 bytes of SHA-256 over the UTF-16LE
// publisher, in Crockford base32 (13 characters).
std::wstring PublisherId(const std::wstring& publisher);
}
