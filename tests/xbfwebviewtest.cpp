#include "xaml/xbf_webview.h"
#include "xbf_webview_fixture.h"
#include <cstdio>
#include <fstream>
static int failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)
int wmain(int argc, wchar_t** argv) {
    auto original = fixture::WebView();
    if (argc == 3 && wcscmp(argv[1], L"--write-fixture") == 0) {
        std::ofstream output(argv[2], std::ios::binary);
        output.write(reinterpret_cast<const char*>(original.data()), original.size());
        return output ? 0 : 1;
    }
    if (argc == 3 && wcscmp(argv[1], L"--check") == 0) {
        std::ifstream input(argv[2], std::ios::binary);
        if (!input) return 1;
        original.assign(std::istreambuf_iterator<char>(input), {});
        if (original.size() < 132) return 1;
    }
    std::vector<BYTE> adapted;
    CHECK(appx::RemapXbfWebViews(original.data(), original.size(), adapted) == S_OK);
    CHECK(adapted.size() == original.size());
    UINT32 metadataSize = 0;
    memcpy(&metadataSize, original.data() + 4, 4);
    if (adapted.size() == original.size()) {
        CHECK(memcmp(original.data(), adapted.data(), 132) == 0);
        CHECK(memcmp(original.data() + 12 + metadataSize, adapted.data() + 12 + metadataSize,
                     original.size() - 12 - metadataSize) == 0);
        std::vector<BYTE> twice;
        CHECK(appx::RemapXbfWebViews(adapted.data(), adapted.size(), twice) == S_FALSE && twice.empty());
    }
    if (argc == 3) return failures ? 1 : 0;
    const auto saved = original;
    for (size_t size = 0; size < original.size(); ++size) {
        CHECK(appx::RemapXbfWebViews(original.data(), size, adapted) != S_OK);
        CHECK(adapted.empty());
    }
    CHECK(original == saved);
    auto literal = fixture::WebView(true);
    CHECK(appx::RemapXbfWebViews(literal.data(), literal.size(), adapted) == E_NOTIMPL && adapted.empty());
    auto custom = fixture::WebView(false, false);
    CHECK(appx::RemapXbfWebViews(custom.data(), custom.size(), adapted) == S_FALSE && adapted.empty());
    auto reused = original;
    UINT64 typesOffset = 0;
    memcpy(&typesOffset, reused.data() + 44, 8);
    const UINT32 webViewString = 4;
    memcpy(reused.data() + 12 + typesOffset + 4 + 2 * 12 + 8, &webViewString, 4);
    CHECK(appx::RemapXbfWebViews(reused.data(), reused.size(), adapted) == E_NOTIMPL && adapted.empty());
    auto unsupported = original;
    unsupported.back() = 0xff;
    CHECK(appx::RemapXbfWebViews(unsupported.data(), unsupported.size(), adapted) == E_NOTIMPL && adapted.empty());
    auto invalid = original;
    invalid[20] = 0xff;
    CHECK(FAILED(appx::RemapXbfWebViews(invalid.data(), invalid.size(), adapted)) && adapted.empty());
    auto unknown = original;
    unknown[12] = 2;
    CHECK(appx::RemapXbfWebViews(unknown.data(), unknown.size(), adapted) == S_FALSE && adapted.empty());
    CHECK(appx::RemapXbfWebViews(nullptr, 0, adapted) == E_POINTER);
    printf("xbfwebviewtest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
