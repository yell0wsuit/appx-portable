#pragma once
#include <windows.h>
#include <array>
#include <cstring>
#include <string>
#include <vector>
namespace fixture {
template <class T> inline void Add(std::vector<BYTE>& bytes, T value) {
    const auto offset = bytes.size();
    bytes.resize(offset + sizeof(value));
    memcpy(bytes.data() + offset, &value, sizeof(value));
}
inline void String(std::vector<BYTE>& bytes, const std::wstring& value) {
    Add<UINT32>(bytes, static_cast<UINT32>(value.size()));
    for (auto character : value) Add<UINT16>(bytes, character);
}
inline std::vector<BYTE> WebView(bool sharedLiteral = false, bool native = true) {
    const std::vector<std::wstring> strings{L"System.Windows",
                                            L"Windows.UI.Xaml.Controls",
                                            L"Windows.UI.Xaml",
                                            L"UserControl",
                                            L"WebView",
                                            L"FrameworkElement",
                                            L"Content",
                                            L"Name",
                                            L"ConnectionId",
                                            L"DefaultBackgroundColor",
                                            L"Width",
                                            L"Height",
                                            L"Transparent",
                                            L"articleView",
                                            L"http://schemas.microsoft.com/winfx/2006/xaml/presentation",
                                            L"http://schemas.microsoft.com/winfx/2006/xaml"};
    const std::vector<std::array<UINT32, 3>> tables[6] = {
        {},
        {{1, 0, 0}},
        {{0, 1, 0}, {0, 2, 0}},
        {{0, 0, 3}, {0, static_cast<UINT32>(native ? 0 : 1), 4}, {0, 1, 5}},
        {{2, 0, 8}, {2, 0, 7}, {0, 0, 6}, {0, 1, 9}, {0, 2, 10}, {0, 2, 11}},
        {{14, 0, 0}, {15, 0, 0}}};
    std::vector<BYTE> metadata(120), nodes;
    metadata[0] = 1;
    for (size_t table = 0; table < 6; ++table) {
        const UINT64 offset = metadata.size();
        memcpy(metadata.data() + 8 + table * 8, &offset, sizeof(offset));
        if (table == 0) {
            Add<UINT32>(metadata, static_cast<UINT32>(strings.size()));
            for (const auto& string : strings) String(metadata, string);
        } else {
            Add<UINT32>(metadata, static_cast<UINT32>(tables[table].size()));
            const auto width = table == 3 || table == 4 ? 3 : table == 5 ? 1 : 2;
            for (const auto& row : tables[table])
                for (int column = 0; column < width; ++column) Add<UINT32>(metadata, row[column]);
        }
    }
    auto node = [&](BYTE kind, UINT32 id) {
        Add<BYTE>(nodes, kind);
        Add<UINT32>(nodes, id);
        Add<UINT32>(nodes, 0);
    };
    auto integer = [&](UINT32 property, INT32 value) {
        node(3, property);
        Add<BYTE>(nodes, 6);
        Add<BYTE>(nodes, 4);
        Add<INT32>(nodes, value);
        Add<BYTE>(nodes, 4);
    };
    auto text = [&](UINT32 property, UINT32 string) {
        node(3, property);
        node(5, string);
        Add<BYTE>(nodes, 4);
    };
    auto number = [&](UINT32 property, float value) {
        node(3, property);
        Add<BYTE>(nodes, 6);
        Add<BYTE>(nodes, 3);
        Add<float>(nodes, value);
        Add<BYTE>(nodes, 4);
    };
    node(7, 0);
    String(nodes, L"");
    node(7, 1);
    String(nodes, L"x");
    node(1, 0);
    integer(0, 1); // Existing UserControl root, connector ID 1.
    node(3, 2);
    node(1, 1);
    integer(0, 2);                   // Content WebView, connector ID 2.
    text(1, sharedLiteral ? 4 : 13); // Same string used as literal must prevent remapping.
    number(4, 500);
    number(5, 300);
    text(3, 12);
    Add<BYTE>(nodes, 2);
    Add<BYTE>(nodes, 4);
    Add<BYTE>(nodes, 2);
    std::vector<BYTE> result;
    Add<UINT32>(result, 0x00464258);
    Add<UINT32>(result, static_cast<UINT32>(metadata.size()));
    Add<UINT32>(result, static_cast<UINT32>(nodes.size()));
    result.insert(result.end(), metadata.begin(), metadata.end());
    result.insert(result.end(), nodes.begin(), nodes.end());
    return result;
}
}
