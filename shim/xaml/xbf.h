#pragma once
#include <windows.h>
#include <objidl.h>
#include <string>
#include <vector>
namespace appx {
struct XbfConnection {
    UINT32 id = 0;
    std::wstring type, name;
    bool root = false, collapsed = false;
};
// XBF 2.0/2.1 connection metadata for the portable native XAML backend.
// This does not render a visual tree. Deferred/custom-runtime-data opcodes are
// rejected, and no connections are returned unless the entire stream validates.
HRESULT ReadXbfConnections(IStream* stream, std::vector<XbfConnection>& connections);
}
