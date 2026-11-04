#include "log.h"
#include "options.h"
#include <winstring.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

namespace appx {
bool TraceMode() noexcept { return GetOptions().passthrough; }
// appx-portable.log beside the game executable.
static const std::wstring& LogPath() noexcept {
    static const std::wstring path = []() noexcept -> std::wstring {
        try {
            std::wstring module(32768, L'\0');
            auto size = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
            module.resize(size < module.size() ? size : 0);
            auto slash = module.rfind(L'\\');
            return slash == std::wstring::npos ? std::wstring() : module.substr(0, slash + 1) + L"appx-portable.log";
        } catch (...) {
            return {}; // No log rather than a crash.
        }
    }();
    return path;
}
// Each logging run starts a new appx-portable.log. The two before it are kept as
// appx-portable.1.log and appx-portable.2.log; older ones are replaced. Runs without
// logging leave them alone. A game started by appx-native-host.exe continues
// the host's log.
static void RotateLogs(const std::wstring& path) noexcept {
    static constexpr int kept = 2;
    std::wstring older[kept + 1];
    try {
        const auto stem = path.substr(0, path.size() - 4); // Without ".log".
        for (int n = 1; n <= kept; ++n) older[n] = stem + L"." + std::to_wstring(n) + L".log";
    } catch (...) {
        return; // Out of memory: keep appending to the current log.
    }
    for (int n = kept; n > 1; --n) MoveFileExW(older[n - 1].c_str(), older[n].c_str(), MOVEFILE_REPLACE_EXISTING);
    MoveFileExW(path.c_str(), older[1].c_str(), MOVEFILE_REPLACE_EXISTING);
}
void Log(const char* level, const char* format, ...) noexcept {
    // Off unless --log or --trace (or their environment variables) is given.
    const auto& options = GetOptions();
    if (!options.log || (!options.trace && strcmp(level, "TRACE") == 0)) return;
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    char message[1600]{};
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    char line[1800]{};
    snprintf(line, sizeof(line), "[%llu][%lu] %s %s\r\n", GetTickCount64(), GetCurrentThreadId(), level, message);
    OutputDebugStringA(line);
    const auto& path = LogPath();
    if (path.empty()) return;
    static bool rotated = false;
    if (!rotated) {
        rotated = true;
        if (!options.hosted) RotateLogs(path);
    }
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    CloseHandle(file);
}
void LogMissingInterface(const char* type, REFIID iid) noexcept {
    Log("FAIL", "%s QI missing %08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", type, iid.Data1, iid.Data2,
        iid.Data3, iid.Data4[0], iid.Data4[1], iid.Data4[2], iid.Data4[3], iid.Data4[4], iid.Data4[5], iid.Data4[6],
        iid.Data4[7]);
}
void LogActivation(HSTRING name, REFIID iid, HRESULT result) noexcept {
    LogThreadStorage();
    const wchar_t* text = WindowsGetStringRawBuffer(name, nullptr);
    if (GetOptions().diagnostics) {
        static wchar_t previous[128] = L"startup";
        char after[300];
        snprintf(after, sizeof(after), "activation of %ls (the previous one: %ls)", text, previous);
        CheckHeaps(after);
        wcsncpy_s(previous, text, _TRUNCATE);
    }
    Log(FAILED(result) ? "FAIL" : "TRACE",
        "activation %ls iid=%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x hr=%08lx", text, iid.Data1, iid.Data2,
        iid.Data3, iid.Data4[0], iid.Data4[1], iid.Data4[2], iid.Data4[3], iid.Data4[4], iid.Data4[5], iid.Data4[6],
        iid.Data4[7], result);
}
// Managed reports repeat: a game timer can hit the same completion stub, or the
// same handler can fail the same way, every second. Each distinct report is
// logged once.
static bool FirstReport(const wchar_t* category, const wchar_t* text) noexcept {
    static std::mutex mutex;
    static std::set<std::wstring> seen;
    try {
        std::lock_guard<std::mutex> lock(mutex);
        return seen.insert(std::wstring(category) + L'|' + text).second;
    } catch (...) {
        return true; // Out of memory: log rather than lose it.
    }
}
}
// Completion stubs in the managed projection name the member they stand in for,
// so a call the game swallows (a discarded Task) still leaves a trace.
extern "C" void __cdecl ShimReportUnimplemented(const wchar_t* member) noexcept {
    if (!member) member = L"(unnamed)";
    if (appx::FirstReport(L"UNIMPL", member)) appx::Log("UNIMPL", "managed %ls", member);
}
// A managed callback's exception that no managed caller will rethrow, such as an
// event handler's: native event raisers discard handler failures.
extern "C" void __cdecl ShimReportException(const wchar_t* text) noexcept {
    if (!text) text = L"(no description)";
    if (appx::FirstReport(L"FAIL", text)) appx::Log("FAIL", "managed callback threw %ls", text);
}
