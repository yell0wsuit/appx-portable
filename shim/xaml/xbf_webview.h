#pragma once
#include <windows.h>
#include <vector>
namespace appx {
// Preserve the file size and node stream while substituting the metadata-only
// seven-character type name. S_FALSE means no native WebView needs adaptation.
HRESULT RemapXbfWebViews(const BYTE* bytes, size_t size, std::vector<BYTE>& adapted);
}
