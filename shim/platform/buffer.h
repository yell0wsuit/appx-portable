#pragma once
#include <windows.storage.streams.h>
namespace appx {
// A new IBuffer holding a copy of length bytes, its Length and Capacity both length.
HRESULT CopyToBuffer(const void* data, UINT32 length, ABI::Windows::Storage::Streams::IBuffer** value);
}
