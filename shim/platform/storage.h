#pragma once
#include <windows.storage.h>
#include <string>
namespace appx {
// Windows' StorageFolder for a path, or a path-only one where Windows has
// none (Wine). Blocks until Windows' asynchronous lookup completes.
HRESULT MakeStorageFolder(const std::wstring& path, ABI::Windows::Storage::IStorageFolder** value);
}
