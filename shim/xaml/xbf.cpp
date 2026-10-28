#include "xbf.h"
#include <cstring>
#include <limits>
#include <set>
#include <new>
namespace appx {
namespace {
constexpr HRESULT Invalid = HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
struct Reader {
    const std::vector<BYTE>& bytes;
    size_t pos, end;
    template <class T> T Get() {
        if (pos > end || sizeof(T) > end - pos) throw Invalid;
        T value;
        memcpy(&value, bytes.data() + pos, sizeof(value));
        pos += sizeof(value);
        return value;
    }
    void Skip(size_t count) {
        if (pos > end || count > end - pos) throw Invalid;
        pos += count;
    }
    std::wstring String() {
        const auto count = Get<UINT32>();
        if (count > (end - pos) / sizeof(wchar_t)) throw Invalid;
        std::wstring value(count, L'\0');
        if (count) memcpy(value.data(), bytes.data() + pos, count * sizeof(wchar_t));
        Skip(count * sizeof(wchar_t));
        return value;
    }
};
struct Constant {
    BYTE kind;
    INT32 number = 0;
    std::wstring text;
};
struct Metadata {
    std::vector<std::wstring> strings, types, properties;
    UINT32 namespaces = 0;
    const std::wstring& String(UINT32 id) const {
        if (id >= strings.size()) throw Invalid;
        return strings[id];
    }
    UINT16 Ref(Reader& reader, size_t count) const {
        const auto id = reader.Get<UINT16>();
        if (!(id & 0x8000) && id >= count) throw Invalid;
        return id;
    }
    std::wstring Type(Reader& reader) const {
        const auto id = Ref(reader, types.size());
        if (!(id & 0x8000)) return types[id];
        // Stable XBF indices from Microsoft's published XBF metadata. Only
        // types that can participate in our native connection backend belong
        // here; unconnected layout/decorative objects need no COM stand-in.
        switch (id & 0x7fff) {
        case 442:
            return L"Grid";
        case 470:
            return L"Rectangle";
        case 482:
            return L"SymbolIcon";
        case 491:
            return L"UserControl";
        case 540:
            return L"Button";
        default:
            return {};
        }
    }
    Constant Value(Reader& reader) const {
        Constant value{reader.Get<BYTE>()};
        switch (value.kind) {
        case 1:
        case 2:
        case 10:
            break;
        case 3:
        case 8:
            reader.Skip(4);
            break;
        case 4:
            value.number = reader.Get<INT32>();
            break;
        case 5: {
            const auto id = reader.Get<UINT16>();
            if (id & 0x8000) throw Invalid;
            value.text = String(id);
            break;
        }
        case 6:
            reader.Skip(16);
            break;
        case 7:
            reader.Skip(8);
            break;
        case 9:
            value.text = reader.String();
            break;
        case 11:
            reader.Get<UINT16>();
            value.number = reader.Get<INT32>();
            break;
        default:
            throw Invalid;
        }
        return value;
    }
};
Metadata ReadMetadata(const std::vector<BYTE>& bytes, size_t end) {
    Reader header{bytes, 12, end};
    if (header.Get<UINT32>() != 2) throw E_NOTIMPL;
    const auto minor = header.Get<UINT32>();
    if (minor > 1) throw E_NOTIMPL;
    UINT64 offsets[6];
    for (auto& offset : offsets) offset = header.Get<UINT64>();
    header.Skip(64); // Compiler hash; not an integrity signature.
    for (size_t i = 0; i < 6; ++i)
        if (offsets[i] < header.pos - 12 || offsets[i] > end - 12 || (i && offsets[i] < offsets[i - 1])) throw Invalid;
    Metadata metadata;
    for (size_t table = 0; table < 6; ++table) {
        Reader reader{bytes, 12 + static_cast<size_t>(offsets[table]),
                      table == 5 ? end : 12 + static_cast<size_t>(offsets[table + 1])};
        const auto count = reader.Get<UINT32>();
        const size_t entrySize = table == 0 ? 4 : table == 3 || table == 4 ? 12 : table == 5 ? 4 : 8;
        if (count > (reader.end - reader.pos) / entrySize) throw Invalid;
        for (UINT32 i = 0; i < count; ++i) {
            if (table == 0) {
                metadata.strings.push_back(reader.String());
                if (minor == 1 && reader.Get<UINT16>() != 0) throw Invalid;
            } else if (table == 3 || table == 4) {
                reader.Get<UINT32>(); // Type/property flags.
                reader.Get<UINT32>(); // Namespace/declaring type.
                const auto& name = metadata.String(reader.Get<UINT32>());
                (table == 3 ? metadata.types : metadata.properties).push_back(name);
            } else if (table == 5) {
                metadata.String(reader.Get<UINT32>());
            } else {
                reader.Get<UINT32>();
                metadata.String(reader.Get<UINT32>());
            }
        }
        if (table == 5) metadata.namespaces = count;
        if (reader.pos != reader.end) throw Invalid;
    }
    return metadata;
}
void Parse(const std::vector<BYTE>& bytes, std::vector<XbfConnection>& connections) {
    Reader header{bytes, 0, bytes.size()};
    if (header.Get<UINT32>() != 0x00464258) throw Invalid;
    const auto metadataSize = header.Get<UINT32>(), nodesSize = header.Get<UINT32>();
    if (metadataSize > bytes.size() - 12 || nodesSize != bytes.size() - 12 - metadataSize) throw Invalid;
    const auto metadata = ReadMetadata(bytes, 12 + metadataSize);
    Reader table{bytes, 12 + metadataSize, bytes.size()};
    // Deferred node streams require an object writer, beyond this binding
    // reader. Reject them instead of guessing their object ownership.
    if (table.Get<UINT32>() != 1) throw E_NOTIMPL;
    const auto start = table.Get<UINT32>(), lines = table.Get<UINT32>();
    if (start != 0 || lines > table.end - table.pos) throw Invalid;
    Reader reader{bytes, table.pos, table.pos + lines};
    std::vector<XbfConnection> objects;
    std::vector<size_t> scopes;
    constexpr size_t NoObject = std::numeric_limits<size_t>::max();
    size_t current = NoObject;
    auto object = [&]() -> XbfConnection& {
        if (current >= objects.size()) throw Invalid;
        return objects[current];
    };
    auto pop = [&] {
        if (scopes.empty()) throw Invalid;
        current = scopes.back();
        scopes.pop_back();
    };
    auto create = [&] {
        auto type = metadata.Type(reader);
        objects.push_back({0, std::move(type), {}, objects.empty(), false});
        current = objects.size() - 1;
    };
    while (reader.pos < reader.end) {
        const auto op = reader.Get<BYTE>();
        switch (op) {
        case 1:
            scopes.push_back(current);
            break;
        case 2:
        case 33:
            pop();
            break;
        case 18:
            scopes.push_back(current);
            [[fallthrough]];
        case 3: {
            const auto id = reader.Get<UINT16>();
            if (id >= metadata.namespaces) throw Invalid;
            reader.String();
            break;
        }
        case 4:
        case 10:
        case 14:
        case 34:
        case 35:
            metadata.Value(reader);
            break;
        case 8:
        case 9:
            break;
        case 11:
            reader.String();
            break;
        case 12: {
            auto value = metadata.Value(reader);
            if (value.kind != 4 || value.number <= 0 || object().id) throw Invalid;
            object().id = static_cast<UINT32>(value.number);
            break;
        }
        case 13: {
            auto value = metadata.Value(reader);
            if (value.kind != 5 && value.kind != 9) throw Invalid;
            object().name = std::move(value.text);
            break;
        }
        case 19:
            scopes.push_back(current);
            [[fallthrough]];
        case 7:
        case 32:
            metadata.Ref(reader, metadata.properties.size());
            break;
        case 20:
            scopes.push_back(current);
            [[fallthrough]];
        case 23:
            create();
            break;
        case 21:
        case 22:
            scopes.push_back(current);
            [[fallthrough]];
        case 24:
        case 25:
            create();
            metadata.Value(reader);
            break;
        case 26:
        case 27:
        case 30:
        case 36: {
            const auto property = metadata.Ref(reader, metadata.properties.size());
            auto value = metadata.Value(reader);
            if (property == (0x8000 | 312)) {
                if (value.kind == 11 && (value.number == 0 || value.number == 1))
                    object().collapsed = value.number == 1;
                else if ((value.kind == 5 || value.kind == 9) &&
                         (value.text == L"Collapsed" || value.text == L"Visible"))
                    object().collapsed = value.text == L"Collapsed";
                else
                    throw E_NOTIMPL;
            }
            break;
        }
        case 28:
        case 31:
            metadata.Ref(reader, metadata.properties.size());
            metadata.Ref(reader, metadata.properties.size());
            break;
        case 29:
            metadata.Ref(reader, metadata.properties.size());
            metadata.Type(reader);
            break;
        default:
            throw E_NOTIMPL;
        }
    }
    if (!scopes.empty() || objects.empty()) throw Invalid;
    std::set<UINT32> ids;
    for (auto& entry : objects) {
        if (!entry.id) continue;
        if (entry.type.empty()) throw E_NOTIMPL;
        if (!ids.insert(entry.id).second) throw Invalid;
        connections.push_back(std::move(entry));
    }
}
}
HRESULT ReadXbfConnections(IStream* stream, std::vector<XbfConnection>& connections) {
    connections.clear();
    if (!stream) return E_POINTER;
    STATSTG stat{};
    auto hr = stream->Stat(&stat, STATFLAG_NONAME);
    if (FAILED(hr)) return hr;
    if (stat.cbSize.QuadPart < 12 || stat.cbSize.QuadPart > 64 * 1024 * 1024) return Invalid;
    try {
        std::vector<BYTE> bytes(static_cast<size_t>(stat.cbSize.QuadPart));
        ULONG read = 0;
        hr = stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read);
        if (FAILED(hr)) return hr;
        if (read != bytes.size()) return Invalid;
        std::vector<XbfConnection> parsed;
        Parse(bytes, parsed);
        connections = std::move(parsed);
        return S_OK;
    } catch (HRESULT failure) {
        return failure;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
}
}
