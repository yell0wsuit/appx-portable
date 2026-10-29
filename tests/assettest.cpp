#include "host/assets.h"
#include <cstdio>
#include <fstream>
#include <string>

static int failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int main() {
    // Single names: OPC escapes are UTF-8; invalid results stay encoded.
    CHECK(appx::DecodePackageName(L"%21_MapSelectController.xml") == std::wstring(L"!_MapSelectController.xml"));
    CHECK(appx::DecodePackageName(L"User%20Controls") == std::wstring(L"User Controls"));
    CHECK(appx::DecodePackageName(L"caf%C3%A9.png") == std::wstring(L"café.png"));
    CHECK(!appx::DecodePackageName(L"plain.xml"));
    CHECK(!appx::DecodePackageName(L"100%.txt")); // not an escape
    CHECK(!appx::DecodePackageName(L"%zz.txt"));
    CHECK(!appx::DecodePackageName(L"a%2Fb.txt")); // would contain a separator
    CHECK(!appx::DecodePackageName(L"%C3.txt"));   // truncated UTF-8
    CHECK(!appx::DecodePackageName(L"%2E%2E"));    // ".."

    const auto root = std::filesystem::current_path() / (L"asset-test-" + std::to_wstring(GetCurrentProcessId()) +
                                                         L"-" + std::to_wstring(GetTickCount64()));
    CHECK(SUCCEEDED(appx::DecodePackageNames(root))); // No game assets beside most test executables.
    CHECK(!std::filesystem::exists(root));
    const auto maps = root / L"Assets" / L"assets" / L"maps";
    std::filesystem::create_directories(maps);
    std::filesystem::create_directories(root / L"User%20Controls");
    std::filesystem::create_directories(root / L"SaveData" / L"Local");
    const auto encoded = maps / L"%21_MasterGameDesignDocument.xml";
    const auto decoded = maps / L"!_MasterGameDesignDocument.xml";
    std::ofstream(encoded) << "design";
    std::ofstream(root / L"User%20Controls" / L"How%20To.xaml") << "control";
    std::ofstream(root / L"SaveData" / L"Local" / L"slot%201.dat") << "save";
    std::ofstream(maps / L"100%.txt") << "literal";

    // Decoding uses the supplied game path; nested folders and files both decode.
    unsigned count = 0;
    CHECK(SUCCEEDED(appx::DecodePackageNames(root, &count)));
    CHECK(count == 3);
    CHECK(!std::filesystem::exists(encoded));
    CHECK(Read(decoded) == "design");
    CHECK(Read(root / L"User Controls" / L"How To.xaml") == "control");
    CHECK(Read(root / L"SaveData" / L"Local" / L"slot%201.dat") == "save"); // saves are never renamed
    CHECK(Read(maps / L"100%.txt") == "literal");
    CHECK(SUCCEEDED(appx::DecodePackageNames(root, &count))); // Repeated launches are harmless.
    CHECK(count == 0);

    std::ofstream(encoded) << "replacement";
    CHECK(SUCCEEDED(appx::DecodePackageNames(root)));
    CHECK(Read(decoded) == "design"); // Preserve a pre-existing decoded file.
    CHECK(Read(encoded) == "replacement");

    std::filesystem::remove(decoded);
    HANDLE locked = CreateFileW(encoded.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(locked != INVALID_HANDLE_VALUE);
    CHECK(FAILED(appx::DecodePackageNames(root))); // Report a blocked rename before game initialization.
    CHECK(Read(encoded) == "replacement");
    CHECK(!std::filesystem::exists(decoded));
    if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
    CHECK(SUCCEEDED(appx::DecodePackageNames(root)));
    CHECK(Read(decoded) == "replacement");
    std::filesystem::remove_all(root);
    printf("assettest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
