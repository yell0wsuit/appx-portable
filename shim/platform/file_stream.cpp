// A read-only random-access stream over a file, for the path-only StorageFile
// (storage_file.cpp) that stands in for Wine's missing Windows.Storage.
#include "file_stream.h"
#include "log.h"
#include <robuffer.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <mutex>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Storage::Streams;
using namespace ABI::Windows::Foundation;
using Windows::Storage::Streams::IBufferByteAccess;
namespace appx {
namespace {
using ReadOperationAbi = IAsyncOperationWithProgress<IBuffer*, UINT32>;
// A read that has already finished. Its handler runs as soon as it is set:
// a game may wait for the result on the thread a posted callback would need.
class ReadOperation final : public RuntimeClass<RuntimeClassFlags<WinRt>, ReadOperationAbi, IAsyncInfo, FtmBase> {
    InspectableClass(L"Windows.Foundation.IAsyncOperationWithProgress`2", BaseTrust);
    using Completed = IAsyncOperationWithProgressCompletedHandler<IBuffer*, UINT32>;
    using Progress = IAsyncOperationProgressHandler<IBuffer*, UINT32>;
    ComPtr<IBuffer> buffer_;
    const HRESULT error_;
    std::mutex mutex_;
    ComPtr<Completed> completed_;
    ComPtr<Progress> progress_;
    bool assigned_ = false, closed_ = false;

public:
    ReadOperation(ComPtr<IBuffer> buffer, HRESULT error) : buffer_(std::move(buffer)), error_(error) {}
    HRESULT STDMETHODCALLTYPE put_Progress(Progress* handler) override {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_ = handler;
        return closed_ ? RO_E_CLOSED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Progress(Progress** handler) override {
        if (!handler) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        return progress_.CopyTo(handler);
    }
    HRESULT STDMETHODCALLTYPE put_Completed(Completed* handler) override {
        ComPtr<Completed> callback(handler);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return RO_E_CLOSED;
            if (assigned_) return E_ILLEGAL_DELEGATE_ASSIGNMENT;
            assigned_ = true;
            completed_ = callback;
        }
        if (callback) callback->Invoke(this, FAILED(error_) ? AsyncStatus::Error : AsyncStatus::Completed);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Completed(Completed** handler) override {
        if (!handler) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        return completed_.CopyTo(handler);
    }
    HRESULT STDMETHODCALLTYPE GetResults(IBuffer** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return FAILED(error_) ? error_ : buffer_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_Id(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Status(AsyncStatus* value) override {
        if (!value) return E_POINTER;
        *value = FAILED(error_) ? AsyncStatus::Error : AsyncStatus::Completed;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* value) override {
        if (!value) return E_POINTER;
        *value = FAILED(error_) ? error_ : S_OK;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Cancel() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close() override {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        return S_OK;
    }
};
class FileStream final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, IRandomAccessStreamWithContentType, IRandomAccessStream,
                          IInputStream, IOutputStream, IClosable, IContentTypeProvider, FtmBase> {
    InspectableClass(L"Windows.Storage.Streams.FileRandomAccessStream", BaseTrust);
    const std::wstring path_, contentType_;
    Wrappers::FileHandle file_;
    UINT64 position_ = 0;
    std::mutex mutex_;

public:
    FileStream(std::wstring path, std::wstring contentType, UINT64 position)
        : path_(std::move(path)), contentType_(std::move(contentType)), position_(position) {}
    HRESULT CustomQueryInterface(REFIID iid, void** value, bool* handled) {
        *handled = true;
        auto hr = RuntimeClassT::QueryInterface(iid, value);
        if (FAILED(hr))
            Log("FAIL", "StorageFile stream %ls QI missing %08lx-%04x-%04x", path_.c_str(), iid.Data1, iid.Data2,
                iid.Data3);
        return hr;
    }
    HRESULT Open() {
        file_.Attach(CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        return file_.IsValid() ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
    static HRESULT Make(const std::wstring& path, const std::wstring& contentType, UINT64 position,
                        ComPtr<FileStream>& stream) {
        stream = Microsoft::WRL::Make<FileStream>(path, contentType, position);
        return stream ? stream->Open() : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT64* value) override {
        if (!value) return E_POINTER;
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file_.Get(), &size)) return HRESULT_FROM_WIN32(GetLastError());
        *value = static_cast<UINT64>(size.QuadPart);
        return S_OK;
    }
    // Leo's Fortune sets the size of its read-only pak0.pak stream, which works
    // on Windows, and never reads it through this stream afterwards. A read-only
    // stream leaves the file as it is.
    HRESULT STDMETHODCALLTYPE put_Size(UINT64 value) override {
        Log("TRACE", "StorageFile stream %ls: ignoring Size=%llu on a read-only stream", path_.c_str(), value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetInputStreamAt(UINT64 position, IInputStream** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        ComPtr<FileStream> stream;
        auto hr = Make(path_, contentType_, position, stream);
        return FAILED(hr) ? hr : stream.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE GetOutputStreamAt(UINT64, IOutputStream** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        Log("TRACE", "StorageFile stream %ls: cannot write a read-only stream", path_.c_str());
        return E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE get_Position(UINT64* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *value = position_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Seek(UINT64 position) override {
        std::lock_guard<std::mutex> lock(mutex_);
        position_ = position;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CloneStream(IRandomAccessStream** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        ComPtr<FileStream> stream;
        auto hr = Make(path_, contentType_, 0, stream);
        return FAILED(hr) ? hr : stream.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_CanRead(boolean* value) override {
        if (!value) return E_POINTER;
        *value = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CanWrite(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    // Reads up to count bytes at the stream's position into buffer, synchronously.
    HRESULT STDMETHODCALLTYPE ReadAsync(IBuffer* buffer, UINT32 count, InputStreamOptions,
                                        ReadOperationAbi** operation) override {
        if (!buffer || !operation) return E_POINTER;
        *operation = nullptr;
        ComPtr<IBufferByteAccess> access;
        UINT32 capacity = 0;
        byte* bytes = nullptr;
        auto hr = buffer->QueryInterface(IID_PPV_ARGS(&access));
        if (SUCCEEDED(hr)) hr = buffer->get_Capacity(&capacity);
        if (SUCCEEDED(hr)) hr = access->Buffer(&bytes);
        if (SUCCEEDED(hr) && count > capacity) hr = E_INVALIDARG;
        DWORD read = 0;
        if (SUCCEEDED(hr)) {
            std::lock_guard<std::mutex> lock(mutex_);
            OVERLAPPED at{};
            at.Offset = static_cast<DWORD>(position_);
            at.OffsetHigh = static_cast<DWORD>(position_ >> 32);
            if (count && !ReadFile(file_.Get(), bytes, count, &read, &at) && GetLastError() != ERROR_HANDLE_EOF)
                hr = HRESULT_FROM_WIN32(GetLastError());
            else
                position_ += read;
        }
        if (SUCCEEDED(hr)) hr = buffer->put_Length(read);
        auto result = Microsoft::WRL::Make<ReadOperation>(ComPtr<IBuffer>(buffer), hr);
        return result ? result.CopyTo(operation) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE WriteAsync(IBuffer*, IAsyncOperationWithProgress<UINT32, UINT32>** operation) override {
        if (!operation) return E_POINTER;
        *operation = nullptr;
        Log("TRACE", "StorageFile stream %ls: cannot write a read-only stream", path_.c_str());
        return E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE FlushAsync(IAsyncOperation<bool>** operation) override {
        if (!operation) return E_POINTER;
        *operation = nullptr;
        return E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        file_.Close();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ContentType(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(contentType_.data(), static_cast<UINT32>(contentType_.size()), value);
    }
};
}
HRESULT OpenFileStream(const std::wstring& path, const std::wstring& contentType,
                       IRandomAccessStreamWithContentType** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    ComPtr<FileStream> stream;
    auto hr = FileStream::Make(path, contentType, 0, stream);
    if (FAILED(hr)) {
        Log("FAIL", "StorageFile stream %ls hr=%08lx", path.c_str(), hr);
        return hr;
    }
    return stream.CopyTo(value);
}
}
