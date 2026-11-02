#include "registry.h"
#include "com/factory.h"
#include "generated/streams.h"
#include "com/async_id.h"
#include <robuffer.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <mutex>
#include <cstring>
#include <new>
#include <thread>
#include <vector>
#include <memory>
#include <roapi.h>
#include <climits>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Storage::Streams;
using ABI::Windows::Foundation::AsyncStatus;
using ABI::Windows::Foundation::IAsyncInfo;
using ABI::Windows::Foundation::IClosable;
using Windows::Storage::Streams::IBufferByteAccess;
namespace appx {
namespace {
using LoadAbi = __FIAsyncOperation_1_UINT32;
using LoadHandler = __FIAsyncOperationCompletedHandler_1_UINT32;
using ReadAbi = __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT32;
using ReadHandler = __FIAsyncOperationWithProgressCompletedHandler_2_Windows__CStorage__CStreams__CIBuffer_UINT32;
// DataReader.LoadAsync's operation, finished by the loading thread.
class LoadOperation final : public RuntimeClass<RuntimeClassFlags<WinRt>, LoadAbi, IAsyncInfo, FtmBase> {
    InspectableClass(L"Windows.Storage.Streams.DataReaderLoadOperation", BaseTrust);
    const UINT32 id_ = NextAsyncId();
    Event canceled_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::mutex mutex_;
    AsyncStatus status_ = AsyncStatus::Started;
    HRESULT error_ = S_OK;
    UINT32 loaded_ = 0;
    ComPtr<LoadHandler> completed_;
    bool assigned_ = false, closed_ = false;

public:
    HANDLE CancellationHandle() const { return canceled_.Get(); }
    void Finish(HRESULT error, UINT32 loaded) {
        ComPtr<LoadHandler> callback;
        AsyncStatus status;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (status_ != AsyncStatus::Started) return;
            error_ = error;
            loaded_ = loaded;
            status = status_ = FAILED(error) ? AsyncStatus::Error : AsyncStatus::Completed;
            callback = completed_;
        }
        if (callback) callback->Invoke(this, status);
    }
    bool Canceled() {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_ == AsyncStatus::Canceled;
    }
    HRESULT STDMETHODCALLTYPE put_Completed(LoadHandler* handler) override {
        AsyncStatus status;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return RO_E_CLOSED;
            if (assigned_) return E_ILLEGAL_DELEGATE_ASSIGNMENT;
            assigned_ = true;
            completed_ = handler;
            status = status_;
        }
        if (handler && status != AsyncStatus::Started) handler->Invoke(this, status);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Completed(LoadHandler** value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *value = nullptr;
        return closed_ ? RO_E_CLOSED : completed_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE GetResults(UINT32* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *value = 0;
        if (closed_) return RO_E_CLOSED;
        if (status_ == AsyncStatus::Started) return E_ILLEGAL_METHOD_CALL;
        if (status_ == AsyncStatus::Canceled) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        if (FAILED(error_)) return error_;
        *value = loaded_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Id(UINT32* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *value = id_;
        return closed_ ? RO_E_CLOSED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Status(AsyncStatus* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *value = status_;
        return closed_ ? RO_E_CLOSED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *value = error_;
        return closed_ ? RO_E_CLOSED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE Cancel() override {
        ComPtr<LoadHandler> callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return RO_E_CLOSED;
            if (status_ != AsyncStatus::Started) return S_OK;
            status_ = AsyncStatus::Canceled;
            error_ = HRESULT_FROM_WIN32(ERROR_CANCELLED);
            SetEvent(canceled_.Get());
            callback = completed_;
        }
        if (callback) callback->Invoke(this, AsyncStatus::Canceled);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        ComPtr<LoadHandler> releasing;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (status_ == AsyncStatus::Started) return E_ILLEGAL_METHOD_CALL;
            closed_ = true;
            releasing.Attach(completed_.Detach());
        }
        return S_OK;
    }
};
// Each read owns its storage, including when a stream retains the supplied buffer.
class LoadBuffer final
    : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IBuffer, IBufferByteAccess, FtmBase> {
    InspectableClass(L"Windows.Storage.Streams.Buffer", BaseTrust);
    std::vector<byte> bytes_;
    UINT32 capacity_, length_ = 0;

public:
    explicit LoadBuffer(UINT32 capacity) : bytes_(capacity), capacity_(capacity) {}
    HRESULT STDMETHODCALLTYPE get_Capacity(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = capacity_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Length(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = length_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Length(UINT32 value) override {
        if (value > capacity_) return E_INVALIDARG;
        length_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Buffer(byte** value) noexcept override {
        if (!value) return E_POINTER;
        *value = bytes_.data();
        return S_OK;
    }
};
// Completion may arrive after cancellation; the callback owns its signal's lifetime.
HRESULT ReadOnce(IInputStream* stream, LoadOperation* load, UINT32 count, InputStreamOptions options,
                 IBuffer** result) {
    *result = nullptr;
    auto buffer = Make<LoadBuffer>(count);
    if (!buffer) return E_OUTOFMEMORY;
    ComPtr<ReadAbi> operation;
    auto hr = stream->ReadAsync(buffer.Get(), count, options, &operation);
    if (FAILED(hr)) return hr;
    if (!operation) return E_UNEXPECTED;
    Event signal(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!signal.IsValid()) return HRESULT_FROM_WIN32(GetLastError());
    auto done = std::make_shared<Event>(std::move(signal));
    auto handler =
        Callback<Implements<RuntimeClassFlags<ClassicCom>, ReadHandler, FtmBase>>([done](ReadAbi*, AsyncStatus) {
            SetEvent(done->Get());
            return S_OK;
        });
    if (!handler) return E_OUTOFMEMORY;
    hr = operation->put_Completed(handler.Get());
    if (FAILED(hr)) return hr;
    HANDLE signals[] = {load->CancellationHandle(), done->Get()};
    const auto wait = WaitForMultipleObjects(2, signals, FALSE, INFINITE);
    if (wait == WAIT_OBJECT_0) {
        ComPtr<IAsyncInfo> info;
        if (SUCCEEDED(operation.As(&info))) info->Cancel();
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }
    if (wait != WAIT_OBJECT_0 + 1) return HRESULT_FROM_WIN32(GetLastError());
    return operation->GetResults(result);
}
// A reader over a buffer (FromBuffer) or over a stream it loads from (the constructor).
class BufferReader final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIDataReader, IClosable, FtmBase> {
    InspectableClass(L"Windows.Storage.Streams.DataReader", BaseTrust);
    std::mutex mutex_;
    ComPtr<IBuffer> buffer_;
    ComPtr<IBufferByteAccess> access_;
    ComPtr<IInputStream> stream_;
    ComPtr<LoadOperation> active_;
    std::vector<byte> loaded_;
    UINT32 length_ = 0, position_ = 0;
    bool closed_ = false, loading_ = false, streamed_ = false;
    UnicodeEncoding encoding_ = UnicodeEncoding_Utf8;
    ByteOrder byteOrder_ = ByteOrder_LittleEndian;
    InputStreamOptions options_ = InputStreamOptions_None;

    HRESULT Bytes(byte** bytes) {
        if (streamed_) {
            *bytes = loaded_.data();
            return S_OK;
        }
        return access_->Buffer(bytes);
    }
    void Load(ComPtr<LoadOperation> operation, ComPtr<IInputStream> stream, UINT32 count, InputStreamOptions options) {
        UINT32 total = 0;
        const HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED);
        HRESULT hr = initialized;
        if (SUCCEEDED(hr)) {
            try {
                while (total < count && !operation->Canceled()) {
                    ComPtr<IBuffer> buffer;
                    const UINT32 requested = std::min<UINT32>(count - total, 64 * 1024);
                    hr = ReadOnce(stream.Get(), operation.Get(), requested, options, &buffer);
                    if (FAILED(hr)) break;
                    if (!buffer) {
                        hr = E_UNEXPECTED;
                        break;
                    }
                    UINT32 read = 0, capacity = 0;
                    hr = buffer->get_Length(&read);
                    if (SUCCEEDED(hr)) hr = buffer->get_Capacity(&capacity);
                    if (FAILED(hr)) break;
                    if (read > requested || read > capacity) {
                        hr = E_UNEXPECTED;
                        break;
                    }
                    if (!read) break;
                    ComPtr<IBufferByteAccess> access;
                    byte* source = nullptr;
                    hr = buffer.As(&access);
                    if (SUCCEEDED(hr)) hr = access->Buffer(&source);
                    if (FAILED(hr)) break;
                    if (!source) {
                        hr = E_POINTER;
                        break;
                    }
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (closed_) {
                            hr = RO_E_CLOSED;
                            break;
                        }
                        if (operation->Canceled()) break;
                        memcpy(loaded_.data() + length_, source, read);
                        length_ += read;
                    }
                    total += read;
                    if (options & InputStreamOptions_Partial) break;
                }
            } catch (const std::bad_alloc&) {
                hr = E_OUTOFMEMORY;
            } catch (const std::length_error&) {
                hr = E_OUTOFMEMORY;
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loading_ = false;
            active_.Reset();
            loaded_.resize(length_);
        }
        operation->Finish(hr, total);
        if (SUCCEEDED(initialized)) RoUninitialize();
    }

public:
    HRESULT InitializeStream(IInputStream* stream) {
        if (!stream) return E_INVALIDARG;
        stream_ = stream;
        streamed_ = true;
        return S_OK;
    }
    HRESULT Initialize(IBuffer* buffer) {
        if (!buffer) return E_INVALIDARG;
        UINT32 capacity = 0;
        auto hr = buffer->get_Length(&length_);
        if (FAILED(hr)) return hr;
        hr = buffer->get_Capacity(&capacity);
        if (FAILED(hr)) return hr;
        if (length_ > capacity) return E_INVALIDARG;
        hr = buffer->QueryInterface(IID_PPV_ARGS(&access_));
        if (FAILED(hr)) return hr;
        buffer_ = buffer;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_UnconsumedBufferLength(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        *value = length_ - position_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ReadByte(BYTE* value) override { return ReadBytes(1, value); }
    HRESULT STDMETHODCALLTYPE ReadBytes(UINT32 count, BYTE* value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        if (count && !value) return E_POINTER;
        if (count > length_ - position_) return E_BOUNDS;
        if (count) {
            BYTE* bytes = nullptr;
            auto hr = Bytes(&bytes);
            if (FAILED(hr)) return hr;
            if (!bytes) return E_POINTER;
            memcpy(value, bytes + position_, count);
            position_ += count;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_UnicodeEncoding(UnicodeEncoding* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        *value = encoding_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_UnicodeEncoding(UnicodeEncoding value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        if (value != UnicodeEncoding_Utf8 && value != UnicodeEncoding_Utf16LE && value != UnicodeEncoding_Utf16BE)
            return E_INVALIDARG;
        encoding_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ByteOrder(ByteOrder* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        *value = byteOrder_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_ByteOrder(ByteOrder value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        if (value != ByteOrder_LittleEndian && value != ByteOrder_BigEndian) return E_INVALIDARG;
        byteOrder_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_InputStreamOptions(InputStreamOptions* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        *value = options_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_InputStreamOptions(InputStreamOptions value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        if (static_cast<unsigned>(value) > 3) return E_INVALIDARG;
        options_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ReadString(UINT32 count, HSTRING* value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        // A code unit is a byte in UTF-8 and two bytes in UTF-16.
        const UINT64 size = encoding_ == UnicodeEncoding_Utf8 ? count : UINT64(count) * 2;
        if (size > length_ - position_) return E_BOUNDS;
        if (!count) return S_OK;
        BYTE* bytes = nullptr;
        auto hr = Bytes(&bytes);
        if (FAILED(hr)) return hr;
        if (!bytes) return E_POINTER;
        if (encoding_ == UnicodeEncoding_Utf8 && count > INT_MAX) return E_INVALIDARG;
        const BYTE* source = bytes + position_;
        try {
            std::wstring text;
            if (encoding_ == UnicodeEncoding_Utf8) {
                const int units =
                    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(source),
                                        static_cast<int>(count), nullptr, 0);
                if (!units) return HRESULT_FROM_WIN32(GetLastError());
                text.resize(units);
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(source),
                                    static_cast<int>(count), text.data(), units);
            } else {
                text.resize(count);
                for (UINT32 i = 0; i < count; ++i)
                    text[i] = encoding_ == UnicodeEncoding_Utf16LE ? wchar_t(source[2 * i] | source[2 * i + 1] << 8)
                                                                   : wchar_t(source[2 * i] << 8 | source[2 * i + 1]);
            }
            hr = WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), value);
            if (SUCCEEDED(hr)) position_ += static_cast<UINT32>(size);
            return hr;
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        } catch (const std::length_error&) {
            return E_OUTOFMEMORY;
        }
    }
    HRESULT STDMETHODCALLTYPE LoadAsync(UINT32 count, LoadAbi** operation) override {
        if (!operation) return E_POINTER;
        *operation = nullptr;
        ComPtr<LoadOperation> load;
        ComPtr<IInputStream> stream;
        InputStreamOptions options;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return RO_E_CLOSED;
            if (!stream_ || loading_) return E_ILLEGAL_METHOD_CALL;
            if (count > UINT32_MAX - (length_ - position_)) return E_INVALIDARG;
            // Compact consumed bytes and reserve space before publishing the operation.
            loaded_.erase(loaded_.begin(), loaded_.begin() + position_);
            length_ -= position_;
            position_ = 0;
            try {
                loaded_.resize(size_t(length_) + count);
            } catch (const std::bad_alloc&) {
                return E_OUTOFMEMORY;
            } catch (const std::length_error&) {
                return E_OUTOFMEMORY;
            }
            load = Make<LoadOperation>();
            if (!load) return E_OUTOFMEMORY;
            if (!load->CancellationHandle()) return HRESULT_FROM_WIN32(GetLastError());
            loading_ = true;
            active_ = load;
            stream = stream_;
            options = options_;
        }
        ComPtr<BufferReader> self(this);
        try {
            std::thread([self, load, stream, count, options] { self->Load(load, stream, count, options); }).detach();
        } catch (const std::exception&) {
            std::lock_guard<std::mutex> lock(mutex_);
            loading_ = false;
            active_.Reset();
            loaded_.resize(length_);
            return E_OUTOFMEMORY;
        }
        return load.CopyTo(operation);
    }
    HRESULT STDMETHODCALLTYPE DetachBuffer(IBuffer** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        if (streamed_) {
            Log("UNIMPL", "DataReader.DetachBuffer on a stream reader");
            return E_NOTIMPL;
        }
        *value = buffer_.Detach();
        access_.Reset();
        length_ = position_ = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DetachStream(IInputStream** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        if (loading_) return E_ILLEGAL_METHOD_CALL;
        *value = stream_.Detach();
        return S_OK;
    }
    // Closing the reader closes the stream it still owns.
    HRESULT STDMETHODCALLTYPE Close() override {
        ComPtr<IInputStream> stream;
        ComPtr<LoadOperation> active;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return S_OK;
            closed_ = true;
            buffer_.Reset();
            access_.Reset();
            stream.Attach(stream_.Detach());
            active = active_;
            loaded_.clear();
            length_ = position_ = 0;
        }
        if (active) active->Cancel();
        ComPtr<IClosable> closable;
        if (stream && SUCCEEDED(stream.As(&closable))) closable->Close();
        return S_OK;
    }
};
class ReaderFactory final : public Factory<IDataReaderStatics, IDataReaderFactory> {
public:
    ReaderFactory() : Factory(L"Windows.Storage.Streams.DataReader") {}
    HRESULT STDMETHODCALLTYPE CreateDataReader(IInputStream* stream, IDataReader** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto reader = Make<BufferReader>();
        if (!reader) return E_OUTOFMEMORY;
        auto hr = reader->InitializeStream(stream);
        if (FAILED(hr)) return hr;
        return reader.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE FromBuffer(IBuffer* buffer, IDataReader** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto reader = Make<BufferReader>();
        if (!reader) return E_OUTOFMEMORY;
        auto hr = reader->Initialize(buffer);
        if (FAILED(hr)) return hr;
        return reader.CopyTo(value);
    }
};
}
HRESULT MakeDataReaderFactory(IActivationFactory** value) {
    auto factory = Make<ReaderFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
// A buffer and stream reader for Wine, which lacks the class; Windows' is complete.
const ShimClass dataReaderClass{L"Windows.Storage.Streams.DataReader", MakeDataReaderFactory, ShimPolicy::FillMissing};
}
