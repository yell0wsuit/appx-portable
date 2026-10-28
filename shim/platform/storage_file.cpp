// A StorageFile for a path, for Wine, whose Windows.Storage has none. Windows'
// own StorageFile serves games wherever it exists (see StorageFileFactory).
#include "generated/storage.h"
#include "storage.h"
#include "file_stream.h"
#include "com/completed_operation.h"
#include "com/failed_operation.h"
#include "log.h"
#include <filesystem>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Storage;
using ABI::Windows::Foundation::IAsyncOperation;
using namespace ABI::Windows::Storage::Streams;
namespace appx {
namespace {
class File final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIStorageFile, DefaultIStorageItem,
                                       IRandomAccessStreamReference, IInputStreamReference, FtmBase> {
    InspectableClass(L"Windows.Storage.StorageFile", BaseTrust);
    const std::wstring path_;
    static HRESULT Text(const std::wstring& text, HSTRING* value) {
        if (!value) return E_POINTER;
        return WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), value);
    }

public:
    explicit File(std::wstring path) : path_(std::move(path)) {}
    // WRL's hook before QueryInterface: logs what callers expect of a
    // StorageFile that this one lacks.
    HRESULT CustomQueryInterface(REFIID iid, void** value, bool* handled) {
        *handled = true;
        auto hr = RuntimeClassT::QueryInterface(iid, value);
        if (FAILED(hr))
            Log("FAIL", "StorageFile %ls QI missing %08lx-%04x-%04x", path_.c_str(), iid.Data1, iid.Data2, iid.Data3);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_Path(HSTRING* value) override { return Text(path_, value); }
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* value) override {
        return Text(std::filesystem::path(path_).filename().wstring(), value);
    }
    HRESULT STDMETHODCALLTYPE get_FileType(HSTRING* value) override {
        return Text(std::filesystem::path(path_).extension().wstring(), value);
    }
    HRESULT STDMETHODCALLTYPE get_ContentType(HSTRING* value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        // Windows maps the extension through the registry; none is an empty type.
        wchar_t type[256]{};
        DWORD size = sizeof(type);
        const auto extension = std::filesystem::path(path_).extension().wstring();
        if (extension.empty() || RegGetValueW(HKEY_CLASSES_ROOT, extension.c_str(), L"Content Type", RRF_RT_REG_SZ,
                                              nullptr, type, &size) != ERROR_SUCCESS)
            return S_OK;
        return Text(type, value);
    }
    HRESULT STDMETHODCALLTYPE get_Attributes(FileAttributes* value) override {
        if (!value) return E_POINTER;
        const DWORD attributes = GetFileAttributesW(path_.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return HRESULT_FROM_WIN32(GetLastError());
        *value = static_cast<FileAttributes>(attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE |
                                                           FILE_ATTRIBUTE_TEMPORARY));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_DateCreated(ABI::Windows::Foundation::DateTime* value) override {
        if (!value) return E_POINTER;
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &data))
            return HRESULT_FROM_WIN32(GetLastError());
        value->UniversalTime =
            (static_cast<INT64>(data.ftCreationTime.dwHighDateTime) << 32) | data.ftCreationTime.dwLowDateTime;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsOfType(StorageItemTypes type, boolean* value) override {
        if (!value) return E_POINTER;
        *value = (type & StorageItemTypes_File) != 0;
        return S_OK;
    }
    // Reading only: the stream refuses writes, so ReadWrite fails here instead.
    HRESULT STDMETHODCALLTYPE OpenReadAsync(
        IAsyncOperation<IRandomAccessStreamWithContentType*>** operation) override {
        return Open<IRandomAccessStreamWithContentType*, IRandomAccessStreamWithContentType>(operation);
    }
    HRESULT STDMETHODCALLTYPE OpenSequentialReadAsync(IAsyncOperation<IInputStream*>** operation) override {
        return Open<IInputStream*, IInputStream>(operation);
    }
    HRESULT STDMETHODCALLTYPE OpenAsync(FileAccessMode mode, IAsyncOperation<IRandomAccessStream*>** operation) override {
        if (mode != FileAccessMode_Read) {
            if (operation) *operation = nullptr;
            Log("UNIMPL", "StorageFile %ls OpenAsync for writing", path_.c_str());
            return E_NOTIMPL;
        }
        return Open<IRandomAccessStream*, IRandomAccessStream>(operation);
    }

private:
    template <class T, class Stream> HRESULT Open(IAsyncOperation<T>** operation) {
        if (!operation) return E_POINTER;
        *operation = nullptr;
        Wrappers::HString type;
        auto hr = get_ContentType(type.GetAddressOf());
        if (FAILED(hr)) return hr;
        ComPtr<IRandomAccessStreamWithContentType> opened;
        hr = OpenFileStream(path_, type.GetRawBuffer(nullptr), &opened);
        ComPtr<Stream> stream;
        if (SUCCEEDED(hr)) hr = opened.As(&stream);
        if (FAILED(hr)) {
            ComPtr<IInspectable> failed;
            hr = MakeFailedOperation(__uuidof(IAsyncOperation<T>), hr, sizeof(void*), &failed);
            return SUCCEEDED(hr) ? failed.CopyTo(operation) : hr;
        }
        auto completed = Make<CompletedOperation<T, Stream*>>([stream](Stream** value) { return stream.CopyTo(value); });
        return completed ? completed.CopyTo(operation) : E_OUTOFMEMORY;
    }
};
}
HRESULT PathFileAsync(HSTRING path, IAsyncOperation<StorageFile*>** operation) {
    if (!operation) return E_POINTER;
    *operation = nullptr;
    std::wstring file(WindowsGetStringRawBuffer(path, nullptr));
    const DWORD attributes = GetFileAttributesW(file.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        // Windows completes the operation with the error rather than failing the call.
        const HRESULT error = attributes == INVALID_FILE_ATTRIBUTES ? HRESULT_FROM_WIN32(GetLastError())
                                                                    : HRESULT_FROM_WIN32(ERROR_DIRECTORY);
        Log("TRACE", "StorageFile %ls hr=%08lx", file.c_str(), error);
        ComPtr<IInspectable> failed;
        auto hr = MakeFailedOperation(__uuidof(IAsyncOperation<StorageFile*>), error, sizeof(void*), &failed);
        return SUCCEEDED(hr) ? failed.CopyTo(operation) : hr;
    }
    auto item = Make<File>(file);
    if (!item) return E_OUTOFMEMORY;
    ComPtr<IStorageFile> result;
    auto hr = item.As(&result);
    if (FAILED(hr)) return hr;
    Log("TRACE", "StorageFile %ls: using the path-only file", file.c_str());
    auto completed = Make<CompletedOperation<StorageFile*, IStorageFile*>>([result](IStorageFile** value) {
        return result.CopyTo(value);
    });
    return completed ? completed.CopyTo(operation) : E_OUTOFMEMORY;
}
}
