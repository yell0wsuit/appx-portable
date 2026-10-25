#include "package/package.h"
#include <cstdio>
#include <fstream>

namespace appx {
void Log(const char*, const char*, ...) noexcept {} // the reader's log lines are not under test
}

static int failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

int main() {
    // Publisher IDs from real package folder names (Name_Version_Arch__PublisherId).
    CHECK(appx::PublisherId(L"CN=83A35945-428F-4517-8AC5-EAB8403CD594") == L"dw446q17t8sdy");
    CHECK(appx::PublisherId(L"CN=06EB8428-B850-4B4D-BEDC-C2767C45EB6E") == L"w77bc8x1h5kya");

    const auto root = std::filesystem::current_path() / (L"package-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    CHECK(!appx::ReadPackage(root).loaded); // no manifest

    // A Windows 8.0 manifest: default namespace, SmallLogo, in-package servers.
    std::ofstream(root / L"AppxManifest.xml") << R"(<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/2010/manifest">
  <Identity Name="Example.Game" Publisher="CN=06EB8428-B850-4B4D-BEDC-C2767C45EB6E" Version="1.8.1.66" ProcessorArchitecture="x86" />
  <Properties><DisplayName>Example Game</DisplayName><Logo>Assets\StoreLogo.png</Logo></Properties>
  <Applications>
    <Application Id="App" Executable="Game_Win8.exe" EntryPoint="Game.App">
      <VisualElements DisplayName="Tile Name" Logo="Assets\Logo.png" SmallLogo="Assets\SmallLogo.png" />
    </Application>
  </Applications>
  <Extensions>
    <Extension Category="windows.activatableClass.inProcessServer">
      <InProcessServer>
        <Path>Component.dll</Path>
        <ActivatableClass ActivatableClassId="Component.First" ThreadingModel="both" />
        <ActivatableClass ActivatableClassId="Component.Second" ThreadingModel="both" />
      </InProcessServer>
    </Extension>
  </Extensions>
</Package>)";
    auto info = appx::ReadPackage(root);
    CHECK(info.loaded);
    CHECK(!info.universal);
    CHECK(info.identityName == L"Example.Game");
    CHECK(info.displayName == L"Example Game");
    CHECK(info.applicationId == L"App");
    CHECK(info.executable == L"Game_Win8.exe");
    CHECK(info.entryPoint == L"Game.App");
    CHECK(info.smallLogo == L"Assets\\SmallLogo.png");
    CHECK(info.fullName == L"Example.Game_1.8.1.66_x86__w77bc8x1h5kya");
    CHECK(info.familyName == L"Example.Game_w77bc8x1h5kya");
    CHECK(info.classes.size() == 2);
    if (info.classes.size() == 2) {
        CHECK(info.classes[1].id == L"Component.Second");
        CHECK(info.classes[1].module == root / L"Component.dll");
    }

    // Windows 8.1: a prefixed namespace, a localized display name, Square30x30Logo.
    std::ofstream(root / L"AppxManifest.xml") << R"(<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/2010/manifest" xmlns:m2="http://schemas.microsoft.com/appx/2013/manifest">
  <Identity Name="Example.Other" Publisher="CN=Example" Version="2.0.0.0" />
  <Properties><DisplayName>ms-resource:AppName</DisplayName></Properties>
  <Applications>
    <Application Id="game" Executable="Game.exe" EntryPoint="Other.App">
      <m2:VisualElements DisplayName="Other Game" Square150x150Logo="Assets\Logo.png" Square30x30Logo="Assets\SmallLogo.png" />
    </Application>
  </Applications>
</Package>)";
    info = appx::ReadPackage(root);
    CHECK(info.displayName == L"Other Game"); // the literal tile name, not the resource reference
    CHECK(info.smallLogo == L"Assets\\SmallLogo.png");
    CHECK(info.fullName.find(L"_neutral__") != std::wstring::npos);
    CHECK(info.classes.empty());
    CHECK(!info.universal);

    std::ofstream(root / L"AppxManifest.xml")
        << R"(<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10">
  <Identity Name="Example.Universal" Publisher="CN=Example" Version="1.0.0.0" ProcessorArchitecture="x64" />
  <Dependencies><TargetDeviceFamily Name="Windows.Universal" MinVersion="10.0.10240.0" /></Dependencies>
  <Applications><Application Id="App" Executable="Game.exe" EntryPoint="Example.App" /></Applications>
</Package>)";
    info = appx::ReadPackage(root);
    CHECK(info.loaded && info.universal && info.architecture == L"x64");

    std::filesystem::remove_all(root);
    printf("packagetest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
