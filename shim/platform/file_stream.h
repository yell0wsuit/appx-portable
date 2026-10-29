#pragma once
#include <windows.storage.streams.h>
#include <string>
namespace appx {
// A read-only random-access stream over path, from its start, for the
// path-only StorageFile; writing and resizing are refused.
HRESULT OpenFileStream(const std::wstring& path, const std::wstring& contentType,
                       ABI::Windows::Storage::Streams::IRandomAccessStreamWithContentType** value);
}
