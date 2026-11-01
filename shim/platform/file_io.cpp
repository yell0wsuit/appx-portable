// Windows.Storage.FileIO for Wine, whose Windows.Storage has none. Text reads
// follow windows.storage.dll: with at least 3 bytes, a byte-order mark selects
// UTF-8 or UTF-16 and is skipped; otherwise UTF-8. UTF-16 reads bytes / 2 code
// units, dropping an odd trailing byte. Bytes without a Unicode mapping fail.
#include "com/completed_operation.h"
#include "com/factory.h"
#include "com/failed_operation.h"
#include "log.h"
#include "registry.h"
#include <windows.storage.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <string>
#include <vector>
using namespace Microsoft::WRL;
using Microsoft::WRL::Wrappers::HString;
using namespace ABI::Windows::Storage;
using namespace ABI::Windows::Storage::Streams;
using ABI::Windows::Foundation::IAsyncAction;
namespace appx {
namespace {
using TextOperation = __FIAsyncOperation_1_HSTRING;
HRESULT Unsupported(const char* member) {
    Log("UNIMPL", "FileIO.%s hr=%08lx", member, E_NOTIMPL);
    return E_NOTIMPL;
}
HRESULT ReadBytes(IStorageFile* file, std::wstring& path, std::vector<BYTE>& bytes) {
    ComPtr<IStorageItem> item;
    auto hr = file->QueryInterface(IID_PPV_ARGS(&item));
    HString text;
    if (SUCCEEDED(hr)) hr = item->get_Path(text.GetAddressOf());
    if (FAILED(hr)) return hr;
    path = text.GetRawBuffer(nullptr);
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER size{};
    hr = GetFileSizeEx(handle, &size) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    // An HSTRING holds at most UINT32 characters; larger text cannot be returned.
    if (SUCCEEDED(hr) && size.QuadPart > 0x7fffffff) hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!bytes.empty() &&
            (!ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()))
            hr = HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_HANDLE_EOF);
    }
    CloseHandle(handle);
    return hr;
}
HRESULT Decode(const std::vector<BYTE>& bytes, UnicodeEncoding encoding, bool detect, std::wstring& text) {
    size_t start = 0;
    detect = detect && bytes.size() >= 3;
    if (detect && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) {
        encoding = UnicodeEncoding_Utf8;
        start = 3;
    } else if (detect && bytes[0] == 0xff && bytes[1] == 0xfe) {
        encoding = UnicodeEncoding_Utf16LE;
        start = 2;
    } else if (detect && bytes[0] == 0xfe && bytes[1] == 0xff) {
        encoding = UnicodeEncoding_Utf16BE;
        start = 2;
    }
    const auto count = bytes.size() - start;
    text.clear();
    if (!count) return S_OK;
    if (encoding == UnicodeEncoding_Utf8) {
        const auto* source = reinterpret_cast<const char*>(bytes.data() + start);
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, static_cast<int>(count), nullptr, 0);
        if (!length) return HRESULT_FROM_WIN32(GetLastError());
        text.resize(static_cast<size_t>(length));
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, static_cast<int>(count), text.data(), length))
            return HRESULT_FROM_WIN32(GetLastError());
        return S_OK;
    }
    if (encoding != UnicodeEncoding_Utf16LE && encoding != UnicodeEncoding_Utf16BE) return E_INVALIDARG;
    text.resize(count / 2);
    for (size_t i = 0; i < text.size(); ++i) {
        const BYTE first = bytes[start + i * 2], second = bytes[start + i * 2 + 1];
        text[i] = static_cast<wchar_t>(encoding == UnicodeEncoding_Utf16LE ? first | second << 8 : first << 8 | second);
    }
    return S_OK;
}
// Read failures complete the operation with the error, as Windows does.
HRESULT ReadText(IStorageFile* file, UnicodeEncoding encoding, bool detect, TextOperation** operation) {
    if (!operation) return E_POINTER;
    *operation = nullptr;
    if (!file) return E_POINTER;
    std::wstring path, text;
    std::vector<BYTE> bytes;
    HRESULT error = S_OK;
    try {
        error = ReadBytes(file, path, bytes);
        if (SUCCEEDED(error)) error = Decode(bytes, encoding, detect, text);
    } catch (const std::bad_alloc&) {
        error = E_OUTOFMEMORY;
    }
    Log("TRACE", "FileIO ReadTextAsync %ls bytes=%zu hr=%08lx", path.c_str(), bytes.size(), error);
    if (FAILED(error)) {
        ComPtr<IInspectable> failed;
        auto hr = MakeFailedOperation(__uuidof(TextOperation), error, sizeof(HSTRING), &failed);
        return SUCCEEDED(hr) ? failed.CopyTo(operation) : hr;
    }
    auto completed = Make<CompletedOperation<HSTRING>>(
        [text](HSTRING* value) { return WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), value); });
    return completed ? completed.CopyTo(operation) : E_OUTOFMEMORY;
}
class FileIOFactory final : public Factory<IFileIOStatics> {
public:
    FileIOFactory() : Factory(L"Windows.Storage.FileIO") {}
    HRESULT STDMETHODCALLTYPE ReadTextAsync(IStorageFile* file, TextOperation** operation) override {
        return ReadText(file, UnicodeEncoding_Utf8, true, operation);
    }
    HRESULT STDMETHODCALLTYPE ReadTextWithEncodingAsync(IStorageFile* file, UnicodeEncoding encoding,
                                                        TextOperation** operation) override {
        return ReadText(file, encoding, false, operation);
    }
    HRESULT STDMETHODCALLTYPE WriteTextAsync(IStorageFile*, HSTRING, IAsyncAction**) override {
        return Unsupported("WriteTextAsync");
    }
    HRESULT STDMETHODCALLTYPE WriteTextWithEncodingAsync(IStorageFile*, HSTRING, UnicodeEncoding,
                                                         IAsyncAction**) override {
        return Unsupported("WriteTextWithEncodingAsync");
    }
    HRESULT STDMETHODCALLTYPE AppendTextAsync(IStorageFile*, HSTRING, IAsyncAction**) override {
        return Unsupported("AppendTextAsync");
    }
    HRESULT STDMETHODCALLTYPE AppendTextWithEncodingAsync(IStorageFile*, HSTRING, UnicodeEncoding,
                                                          IAsyncAction**) override {
        return Unsupported("AppendTextWithEncodingAsync");
    }
    HRESULT STDMETHODCALLTYPE ReadLinesAsync(IStorageFile*, __FIAsyncOperation_1___FIVector_1_HSTRING**) override {
        return Unsupported("ReadLinesAsync");
    }
    HRESULT STDMETHODCALLTYPE ReadLinesWithEncodingAsync(IStorageFile*, UnicodeEncoding,
                                                         __FIAsyncOperation_1___FIVector_1_HSTRING**) override {
        return Unsupported("ReadLinesWithEncodingAsync");
    }
    HRESULT STDMETHODCALLTYPE WriteLinesAsync(IStorageFile*, __FIIterable_1_HSTRING*, IAsyncAction**) override {
        return Unsupported("WriteLinesAsync");
    }
    HRESULT STDMETHODCALLTYPE WriteLinesWithEncodingAsync(IStorageFile*, __FIIterable_1_HSTRING*, UnicodeEncoding,
                                                          IAsyncAction**) override {
        return Unsupported("WriteLinesWithEncodingAsync");
    }
    HRESULT STDMETHODCALLTYPE AppendLinesAsync(IStorageFile*, __FIIterable_1_HSTRING*, IAsyncAction**) override {
        return Unsupported("AppendLinesAsync");
    }
    HRESULT STDMETHODCALLTYPE AppendLinesWithEncodingAsync(IStorageFile*, __FIIterable_1_HSTRING*, UnicodeEncoding,
                                                           IAsyncAction**) override {
        return Unsupported("AppendLinesWithEncodingAsync");
    }
    HRESULT STDMETHODCALLTYPE ReadBufferAsync(IStorageFile*,
                                              __FIAsyncOperation_1_Windows__CStorage__CStreams__CIBuffer**) override {
        return Unsupported("ReadBufferAsync");
    }
    HRESULT STDMETHODCALLTYPE WriteBufferAsync(IStorageFile*, IBuffer*, IAsyncAction**) override {
        return Unsupported("WriteBufferAsync");
    }
    HRESULT STDMETHODCALLTYPE WriteBytesAsync(IStorageFile*, UINT32, BYTE*, IAsyncAction**) override {
        return Unsupported("WriteBytesAsync");
    }
};
HRESULT MakeFileIOFactory(IActivationFactory** value) {
    auto factory = Make<FileIOFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
const ShimClass fileIOClass{L"Windows.Storage.FileIO", MakeFileIOFactory, ShimPolicy::FillMissing};
}
