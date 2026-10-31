#include "options.h"
#include <windows.h>
#include <cwchar>
#include <string>
namespace appx {
namespace {
// First use can be inside DllMain, so this splits the command line itself
// rather than loading shell32 under the loader lock. Switches never contain
// spaces or quotes; a quoted program path is skipped as one word.
template <typename Visit> void ForEachWord(const wchar_t* text, Visit visit) {
    while (*text) {
        while (*text == L' ' || *text == L'\t') ++text;
        if (!*text) break;
        std::wstring word;
        bool quoted = false;
        for (; *text && (quoted || (*text != L' ' && *text != L'\t')); ++text) {
            if (*text == L'"')
                quoted = !quoted;
            else
                word += *text;
        }
        visit(word);
    }
}
bool Environment(const wchar_t* name, const wchar_t* expected) {
    wchar_t value[16]{};
    const auto length = GetEnvironmentVariableW(name, value, 16);
    return length > 0 && length < 16 && wcscmp(value, expected) == 0;
}
// A Store override set in the environment decides; otherwise the [options]
// key in appx-portable.ini beside the game executable does.
bool Setting(const wchar_t* variable, const wchar_t* key, const std::wstring& ini) {
    if (GetEnvironmentVariableW(variable, nullptr, 0) > 0) return Environment(variable, L"1");
    return GetPrivateProfileIntW(L"options", key, 0, ini.c_str()) != 0;
}
// The frame cap from the environment, else the ini; missing means 60.
unsigned FrameRate(const std::wstring& ini) {
    wchar_t value[16]{};
    const auto length = GetEnvironmentVariableW(L"APPX_PORTABLE_FRAME_RATE", value, 16);
    if (length > 0 && length < 16) return static_cast<unsigned>(wcstoul(value, nullptr, 10));
    return GetPrivateProfileIntW(L"options", L"FrameRate", 60, ini.c_str());
}
// Product IDs for the stub IAP listing, from the environment, else the ini.
std::wstring ProductSetting(const std::wstring& ini, const wchar_t* variable, const wchar_t* key) {
    std::wstring value(4096, L'\0');
    auto length = GetEnvironmentVariableW(variable, value.data(), static_cast<DWORD>(value.size()));
    if (length >= value.size()) length = 0;
    if (!length && GetLastError() == ERROR_ENVVAR_NOT_FOUND)
        length =
            GetPrivateProfileStringW(L"options", key, L"", value.data(), static_cast<DWORD>(value.size()), ini.c_str());
    value.resize(length);
    return value;
}
// kernel32 only: first use can be inside DllMain.
std::wstring SettingsFile() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD length = 0;
    while ((length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()))) == path.size())
        path.resize(path.size() * 2);
    path.resize(length);
    return path.substr(0, path.find_last_of(L"\\/") + 1) + L"appx-portable.ini";
}
Options Parse() {
    Options options;
    options.log = Environment(L"APPX_PORTABLE_LOG", L"1");
    options.trace = Environment(L"APPX_PORTABLE_TRACE", L"1");
    options.capture = Environment(L"APPX_PORTABLE_CAPTURE", L"1");
    options.diagnostics = Environment(L"APPX_PORTABLE_DIAGNOSTICS", L"1");
    options.passthrough = Environment(L"APPX_PORTABLE_MODE", L"trace");
    options.hostedXaml = Environment(L"APPX_PORTABLE_HOSTED", L"xaml");
    options.hosted = options.hostedXaml || Environment(L"APPX_PORTABLE_HOSTED", L"1");
    const auto ini = SettingsFile();
    options.unlockTrial = Setting(L"APPX_PORTABLE_UNLOCK_TRIAL", L"UnlockTrial", ini);
    options.stubIap = Setting(L"APPX_PORTABLE_STUB_IAP", L"StubIap", ini);
    options.frameRate = FrameRate(ini);
    options.stubIapProducts = ProductSetting(ini, L"APPX_PORTABLE_STUB_IAP_PRODUCTS", L"StubIapProducts");
    options.stubXboxProducts = ProductSetting(ini, L"APPX_PORTABLE_STUB_XBOX_PRODUCTS", L"StubXboxProducts");
    bool program = true;
    ForEachWord(GetCommandLineW(), [&](const std::wstring& word) {
        if (program) {
            program = false;
            return;
        }
        if (_wcsicmp(word.c_str(), L"--log") == 0) options.log = true;
        if (_wcsicmp(word.c_str(), L"--trace") == 0) options.trace = true;
        if (_wcsicmp(word.c_str(), L"--capture") == 0) options.capture = true;
        if (_wcsicmp(word.c_str(), L"--diagnostics") == 0) options.diagnostics = true;
        if (_wcsicmp(word.c_str(), L"--passthrough") == 0) options.passthrough = true;
        if (_wcsicmp(word.c_str(), L"--stub-iap") == 0) options.stubIap = true;
        if (_wcsicmp(word.c_str(), L"--unlock-trial") == 0) options.unlockTrial = true;
    });
    // Tracing and passthrough exist to produce a log.
    if (options.trace || options.passthrough || options.diagnostics) options.log = true;
    if (options.passthrough) options.trace = true;
    return options;
}
}
const Options& GetOptions() noexcept {
    static const Options options = [] {
        try {
            return Parse();
        } catch (...) {
            return Options{}; // Out of memory at startup: everything stays off.
        }
    }();
    return options;
}
}
