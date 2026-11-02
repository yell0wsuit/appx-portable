#pragma once
#include <objbase.h>
namespace appx {
// CLSID_FreeThreadedXMLHTTP60, which Wine's msxml lacks.
constexpr GUID freeThreadedHttpClass = {0x88d96a09, 0xf192, 0x11d4, {0xa6, 0x5f, 0x00, 0x40, 0x96, 0x32, 0x51, 0xe5}};
// CoCreateInstanceFromApp's results for an offline FreeThreadedXMLHTTP60.
HRESULT CreateOfflineHttpRequest(DWORD count, MULTI_QI* results);
}
