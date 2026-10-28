#include "xbf_webview.h"
#include <array>
#include <cstring>
#include <new>
#include <set>
#include <string>
namespace appx {
namespace {
constexpr HRESULT Invalid = HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
struct Input {
    const BYTE* data;
    size_t pos, end;
    template <class T> T Get() {
        if (pos > end || sizeof(T) > end - pos) throw Invalid;
        T value;
        memcpy(&value, data + pos, sizeof(value));
        pos += sizeof(value);
        return value;
    }
    void Skip(size_t count) {
        if (pos > end || count > end - pos) throw Invalid;
        pos += count;
    }
    void SkipString() {
        const auto count = Get<UINT32>();
        if (count > (end - pos) / 2) throw Invalid;
        Skip(count * 2);
    }
};
struct StringEntry {
    std::wstring value;
    size_t offset;
};
std::vector<size_t> Plan(const BYTE* bytes, size_t size) {
    Input header{bytes, 0, size};
    if (header.Get<UINT32>() != 0x00464258) return {};
    const auto metadataSize = header.Get<UINT32>(), nodeSize = header.Get<UINT32>();
    if (header.Get<UINT32>() != 1 || header.Get<UINT32>() != 0) return {};
    if (metadataSize > size - 12 || nodeSize != size - 12 - metadataSize) throw Invalid;
    UINT64 offsets[6];
    for (auto& offset : offsets) offset = header.Get<UINT64>();
    header.Skip(64);
    if (offsets[0] != 120) throw Invalid;
    for (size_t i = 0; i < 6; ++i)
        if (offsets[i] > metadataSize || (i && offsets[i] < offsets[i - 1])) throw Invalid;
    Input strings{bytes, 12 + static_cast<size_t>(offsets[0]), 12 + static_cast<size_t>(offsets[1])};
    const auto stringCount = strings.Get<UINT32>();
    if (stringCount > (strings.end - strings.pos) / 4) throw Invalid;
    std::vector<StringEntry> names;
    for (UINT32 i = 0; i < stringCount; ++i) {
        const auto count = strings.Get<UINT32>();
        if (count > (strings.end - strings.pos) / 2) throw Invalid;
        StringEntry entry{std::wstring(count, L'\0'), strings.pos};
        if (count) memcpy(entry.value.data(), bytes + strings.pos, count * 2);
        strings.Skip(count * 2);
        names.push_back(std::move(entry));
    }
    if (strings.pos != strings.end) throw Invalid;
    auto name = [&](UINT32 id) -> const std::wstring& {
        if (id >= names.size()) throw Invalid;
        return names[id].value;
    };
    std::vector<std::array<UINT32, 3>> tables[6];
    for (size_t index = 1; index < 6; ++index) {
        Input table{bytes, 12 + static_cast<size_t>(offsets[index]),
                    12 + (index == 5 ? metadataSize : static_cast<size_t>(offsets[index + 1]))};
        const auto count = table.Get<UINT32>();
        const auto width = index == 3 || index == 4 ? 3 : index == 5 ? 1 : 2;
        if (count > (table.end - table.pos) / (width * 4)) throw Invalid;
        for (UINT32 i = 0; i < count; ++i) {
            std::array<UINT32, 3> row{};
            for (int column = 0; column < width; ++column) row[column] = table.Get<UINT32>();
            name(row[width - 1]);
            tables[index].push_back(row);
        }
        if (table.pos != table.end) throw Invalid;
    }
    std::set<UINT32> targets;
    for (const auto& type : tables[3]) {
        if (type[0] || name(type[2]) != L"WebView") continue;
        if (type[1] >= tables[2].size()) throw Invalid;
        const auto& space = tables[2][type[1]];
        if (space[0] >= tables[1].size()) throw Invalid;
        if (tables[1][space[0]][0] == 1 && name(space[1]) == L"Windows.UI.Xaml.Controls") targets.insert(type[2]);
    }
    if (targets.empty()) return {};
    // A shared metadata string may also be a literal, a property, or the name
    // of an unrelated custom type. Never change those uses as a side effect.
    auto exclusive = [&](UINT32 id) {
        if (targets.count(id)) throw E_NOTIMPL;
    };
    for (size_t table : {1u, 2u, 4u, 5u})
        for (const auto& row : tables[table]) exclusive(row[table == 4 ? 2 : table == 5 ? 0 : 1]);
    for (const auto& type : tables[3]) {
        if (!targets.count(type[2])) continue;
        if (type[0] || type[1] >= tables[2].size()) throw E_NOTIMPL;
        const auto& space = tables[2][type[1]];
        if (space[0] >= tables[1].size() || tables[1][space[0]][0] != 1 ||
            name(space[1]) != L"Windows.UI.Xaml.Controls")
            throw E_NOTIMPL;
    }
    Input nodes{bytes, 12 + metadataSize, size};
    while (nodes.pos < nodes.end) {
        const auto kind = nodes.Get<BYTE>();
        switch (kind) {
        case 1:
        case 3:
        case 5:
        case 7: {
            const auto id = nodes.Get<UINT32>();
            nodes.Get<UINT32>(); // Persisted node flags.
            const auto count = kind == 1   ? tables[3].size()
                               : kind == 3 ? tables[4].size()
                               : kind == 5 ? names.size()
                                           : tables[5].size();
            if (id >= count) throw Invalid;
            if (kind == 5) exclusive(id);
            if (kind == 7) nodes.SkipString();
            break;
        }
        case 6: {
            const auto value = nodes.Get<BYTE>();
            switch (value) {
            case 1:
            case 2:
                break;
            case 3:
            case 4:
            case 6:
            case 8:
            case 10:
            case 11:
                nodes.Skip(4);
                break;
            case 5:
                nodes.SkipString();
                break;
            case 7:
                nodes.Skip(16);
                break;
            case 9:
                nodes.Skip(8);
                break;
            default:
                throw E_NOTIMPL;
            }
            break;
        }
        case 2:
        case 4:
        case 8:
        case 9:
            break;
        case 10:
            nodes.Skip(4);
            break;
        case 11:
            nodes.Skip(8);
            break;
        default:
            throw E_NOTIMPL;
        }
    }
    std::vector<size_t> result;
    for (auto id : targets) result.push_back(names[id].offset);
    return result;
}
}
HRESULT RemapXbfWebViews(const BYTE* bytes, size_t size, std::vector<BYTE>& adapted) {
    adapted.clear();
    if (!bytes) return E_POINTER;
    if (size < 20) return S_FALSE;
    try {
        const auto offsets = Plan(bytes, size);
        if (offsets.empty()) return S_FALSE;
        adapted.assign(bytes, bytes + size);
        // Same UTF-16 length as WebView, so every stream size, table offset,
        // node position, connection ID and source location remains unchanged.
        constexpr wchar_t alias[] = L"AppxWeb";
        static_assert(sizeof(alias) == sizeof(L"WebView"));
        for (auto offset : offsets) memcpy(adapted.data() + offset, alias, sizeof(alias) - sizeof(wchar_t));
        return S_OK;
    } catch (HRESULT failure) {
        adapted.clear();
        return failure;
    } catch (const std::bad_alloc&) {
        adapted.clear();
        return E_OUTOFMEMORY;
    }
}
}
