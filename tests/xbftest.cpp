#include "xaml/xbf.h"
#include "package/resources.h"
#include <shlwapi.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <fstream>
using Microsoft::WRL::ComPtr;
static int failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)
template <class T> void Add(std::vector<BYTE>& bytes, T value) {
    auto start = bytes.size();
    bytes.resize(start + sizeof(value));
    memcpy(bytes.data() + start, &value, sizeof(value));
}
static void String(std::vector<BYTE>& bytes, const std::wstring& value) {
    Add<UINT32>(bytes, static_cast<UINT32>(value.size()));
    for (auto character : value) Add<UINT16>(bytes, character);
}
static std::vector<BYTE> Fixture(bool duplicate = false, bool unknown = false) {
    std::vector<BYTE> metadata(120), nodes;
    metadata[0] = 2;
    metadata[4] = 1;
    for (size_t table = 0; table < 6; ++table) {
        UINT64 offset = metadata.size();
        memcpy(metadata.data() + 8 + table * 8, &offset, 8);
        Add<UINT32>(metadata, table == 0 ? 2 : 0);
        if (table == 0) {
            String(metadata, L"Root");
            Add<UINT16>(metadata, 0);
            String(metadata, L"Toggle");
            Add<UINT16>(metadata, 0);
        }
    }
    Add<BYTE>(nodes, 1); // Root object scope.
    Add<BYTE>(nodes, 23);
    Add<UINT16>(nodes, 0x8000 | 491); // UserControl.
    Add<BYTE>(nodes, 12);
    Add<BYTE>(nodes, 4);
    Add<INT32>(nodes, 1);
    Add<BYTE>(nodes, 13);
    Add<BYTE>(nodes, 5);
    Add<UINT16>(nodes, 0);
    Add<BYTE>(nodes, 20);
    Add<UINT16>(nodes, 0x8000 | 540); // Button.
    Add<BYTE>(nodes, 12);
    Add<BYTE>(nodes, 4);
    Add<INT32>(nodes, duplicate ? 1 : 2);
    Add<BYTE>(nodes, 13);
    Add<BYTE>(nodes, 5);
    Add<UINT16>(nodes, 1);
    Add<BYTE>(nodes, 26);
    Add<UINT16>(nodes, 0x8000 | 312); // Visibility.
    Add<BYTE>(nodes, 11);
    Add<UINT16>(nodes, 688);
    Add<INT32>(nodes, 1); // Collapsed.
    Add<BYTE>(nodes, unknown ? 99 : 33);
    Add<BYTE>(nodes, 33);
    std::vector<BYTE> bytes;
    Add<UINT32>(bytes, 0x00464258);
    Add<UINT32>(bytes, static_cast<UINT32>(metadata.size()));
    Add<UINT32>(bytes, static_cast<UINT32>(12 + nodes.size()));
    bytes.insert(bytes.end(), metadata.begin(), metadata.end());
    Add<UINT32>(bytes, 1);
    Add<UINT32>(bytes, 0);
    Add<UINT32>(bytes, static_cast<UINT32>(nodes.size()));
    bytes.insert(bytes.end(), nodes.begin(), nodes.end());
    return bytes;
}
static HRESULT Parse(const std::vector<BYTE>& bytes, std::vector<appx::XbfConnection>& connections) {
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size())));
    return appx::ReadXbfConnections(stream.Get(), connections);
}
int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (argc == 3 && wcscmp(argv[1], L"--write-fixture") == 0) {
        const auto fixture = Fixture();
        std::ofstream output(argv[2], std::ios::binary);
        output.write(reinterpret_cast<const char*>(fixture.data()), fixture.size());
        std::string encoded;
        constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (size_t i = 0; i < fixture.size(); i += 3) {
            unsigned bits = static_cast<unsigned>(fixture[i]) << 16;
            if (i + 1 < fixture.size()) bits |= static_cast<unsigned>(fixture[i + 1]) << 8;
            if (i + 2 < fixture.size()) bits |= fixture[i + 2];
            encoded += alphabet[(bits >> 18) & 63];
            encoded += alphabet[(bits >> 12) & 63];
            encoded += i + 1 < fixture.size() ? alphabet[(bits >> 6) & 63] : '=';
            encoded += i + 2 < fixture.size() ? alphabet[bits & 63] : '=';
        }
        auto infoPath = std::filesystem::path(argv[2]);
        infoPath.replace_extension(L".pri.xml");
        std::ofstream info(infoPath);
        info << "<PriInfo><ResourceMap name='AppxPortable.XbfFixture' version='1.0' primary='true'>"
                "<ResourceMapSubtree name='Files'><NamedResource name='Example.xbf' "
                "uri='ms-resource://AppxPortable.XbfFixture/Files/Example.xbf'>"
                "<Candidate type='EmbeddedData'><QualifierSet index='0'/><Base64Value>"
             << encoded << "</Base64Value></Candidate></NamedResource></ResourceMapSubtree></ResourceMap></PriInfo>";
        return output && info ? 0 : 1;
    }
    std::vector<appx::XbfConnection> connections;
    if (argc == 3) {
        appx::PackageInfo package;
        package.directory = argv[1];
        ComPtr<IStream> stream;
        auto hr = appx::OpenPackageResource(package, argv[2], &stream);
        if (hr == S_OK) hr = appx::ReadXbfConnections(stream.Get(), connections);
        printf("resource/XBF hr=%08lx\n", hr);
        for (const auto& entry : connections)
            wprintf(L"%u %ls %ls root=%d collapsed=%d\n", entry.id, entry.type.c_str(), entry.name.c_str(), entry.root,
                    entry.collapsed);
        return hr == S_OK ? 0 : 1;
    }
    const auto fixture = Fixture();
    CHECK(Parse(fixture, connections) == S_OK);
    CHECK(connections.size() == 2);
    if (connections.size() == 2) {
        CHECK(connections[0].id == 1 && connections[0].root && connections[0].type == L"UserControl" &&
              connections[0].name == L"Root");
        CHECK(connections[1].id == 2 && !connections[1].root && connections[1].type == L"Button" &&
              connections[1].name == L"Toggle" && connections[1].collapsed);
    }
    for (size_t size = 0; size < fixture.size(); ++size) {
        CHECK(FAILED(Parse({fixture.begin(), fixture.begin() + size}, connections)));
        CHECK(connections.empty());
    }
    CHECK(FAILED(Parse(Fixture(true), connections)) && connections.empty());
    CHECK(Parse(Fixture(false, true), connections) == E_NOTIMPL && connections.empty());
    auto version = fixture;
    version[12] = 1;
    CHECK(Parse(version, connections) == E_NOTIMPL);
    auto strings = fixture;
    strings[132] = 255;
    CHECK(FAILED(Parse(strings, connections)));
    const auto directory = std::filesystem::current_path() / (L"xbf-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto path = directory / L"Example.xbf";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(fixture.data()), fixture.size());
    }
    appx::PackageInfo package;
    package.directory = directory;
    ComPtr<IStream> stream;
    CHECK(appx::OpenPackageResource(package, L"Example.xbf", &stream) == S_OK);
    CHECK(appx::ReadXbfConnections(stream.Get(), connections) == S_OK && connections.size() == 2);
    stream.Reset();
    CHECK(appx::OpenPackageResource(package, L"Absent.xbf", &stream) == S_FALSE && !stream);
    CHECK(appx::OpenPackageResource(package, L"../Example.xbf", &stream) == E_INVALIDARG);
    CHECK(appx::OpenPackageResource(package, L"C:/Example.xbf", &stream) == E_INVALIDARG);
    CHECK(appx::OpenPackageResource(package, L"/Example.xbf", &stream) == E_INVALIDARG);
    CHECK(appx::OpenPackageResource(package, L"Example.xbf", nullptr) == E_POINTER);
    std::filesystem::remove(path);
    std::filesystem::remove(directory);
    printf("xbftest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
