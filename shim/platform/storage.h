#pragma once
#include <windows.storage.h>
#include <string>
namespace appx {
// Windows' StorageFolder for a path, or a path-only one where Windows has
// none (Wine). Blocks until Windows' asynchronous lookup completes.
HRESULT MakeStorageFolder(const std::wstring& path, ABI::Windows::Storage::IStorageFolder** value);
// A completed StorageFile lookup for a path, for Wine, which has no
// StorageFile: an operation that fails as Windows' does when nothing is there.
HRESULT PathFileAsync(HSTRING path,
                      ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::StorageFile*>** operation);
}
