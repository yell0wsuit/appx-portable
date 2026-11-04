#include <windows.h>
#include <roapi.h>
#include <windows.system.profile.h>
#include <windows.storage.streams.h>
#include <robuffer.h>
#include <wrl.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <mutex>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Storage::Streams;
using namespace ABI::Windows::System::Profile;
using ABI::Windows::Foundation::AsyncStatus;
using ABI::Windows::Foundation::IAsyncInfo;
using ReadAbi = __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT32;
using ReadHandler = __FIAsyncOperationWithProgressCompletedHandler_2_Windows__CStorage__CStreams__CIBuffer_UINT32;
using ReadProgress = __FIAsyncOperationProgressHandler_2_Windows__CStorage__CStreams__CIBuffer_UINT32;
// Supports both immediately completed reads and deliberately pending reads.
class Read final : public RuntimeClass<RuntimeClassFlags<WinRt>, ReadAbi, IAsyncInfo, FtmBase> {
    InspectableClass(L"Test.Read", BaseTrust);
    ComPtr<IBuffer> buffer_;
    std::mutex mutex_;
    ComPtr<ReadHandler> handler_;
    AsyncStatus status_;
    HRESULT error_ = S_OK;

public:
    Event assigned{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Event canceled{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    explicit Read(IBuffer* buffer, bool pending = false)
        : buffer_(buffer), status_(pending ? AsyncStatus::Started : AsyncStatus::Completed) {}
    HRESULT STDMETHODCALLTYPE put_Progress(ReadProgress*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Progress(ReadProgress** value) override {
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Completed(ReadHandler* handler) override {
        AsyncStatus status;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            handler_ = handler;
            status = status_;
        }
        if (status != AsyncStatus::Started && handler) handler->Invoke(this, status);
        SetEvent(assigned.Get());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Completed(ReadHandler** value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return handler_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE GetResults(IBuffer** value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        *value = nullptr;
        if (status_ == AsyncStatus::Started) return E_ILLEGAL_METHOD_CALL;
        if (status_ == AsyncStatus::Canceled) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        return FAILED(error_) ? error_ : buffer_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_Id(UINT32* value) override {
        *value = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Status(AsyncStatus* value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        *value = status_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* value) override {
        *value = S_OK;
        return S_OK;
    }
    // Cancellation can be acknowledged before the underlying I/O callback arrives.
    HRESULT STDMETHODCALLTYPE Cancel() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_ = AsyncStatus::Canceled;
        }
        SetEvent(canceled.Get());
        return S_OK;
    }
    void Complete(HRESULT error = S_OK) {
        ComPtr<ReadHandler> handler;
        AsyncStatus status;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (status_ == AsyncStatus::Started) {
                error_ = error;
                status_ = FAILED(error) ? AsyncStatus::Error : AsyncStatus::Completed;
                // Return EOF for a successful delayed read.
                buffer_->put_Length(0);
            }
            status = status_;
            handler = handler_;
        }
        if (handler) handler->Invoke(this, status);
    }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};
class PendingStream final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, IInputStream, ABI::Windows::Foundation::IClosable, FtmBase> {
    InspectableClass(L"Test.PendingStream", BaseTrust);

public:
    Event started{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    ComPtr<Read> read;
    HRESULT STDMETHODCALLTYPE ReadAsync(IBuffer* buffer, UINT32, InputStreamOptions, ReadAbi** value) override {
        read = Make<Read>(buffer, true);
        auto hr = read.CopyTo(value);
        SetEvent(started.Get());
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};
// Returns at most three bytes per read, so a load must keep reading.
class ChunkStream final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, IInputStream, ABI::Windows::Foundation::IClosable> {
    InspectableClass(L"Test.ChunkStream", BaseTrust);
    std::string data_;
    size_t position_ = 0;
    const UINT32 chunk_;

public:
    int closes = 0;
    ComPtr<IBuffer> firstBuffer;
    explicit ChunkStream(std::string data, UINT32 chunk = 3) : data_(std::move(data)), chunk_(chunk) {}
    HRESULT STDMETHODCALLTYPE ReadAsync(IBuffer* buffer, UINT32 count, InputStreamOptions, ReadAbi** value) override {
        if (!firstBuffer) firstBuffer = buffer;
        ComPtr<Windows::Storage::Streams::IBufferByteAccess> access;
        byte* bytes = nullptr;
        if (FAILED(buffer->QueryInterface(IID_PPV_ARGS(&access))) || FAILED(access->Buffer(&bytes))) return E_FAIL;
        UINT32 read = static_cast<UINT32>(std::min<size_t>({count, chunk_, data_.size() - position_}));
        memcpy(bytes, data_.data() + position_, read);
        position_ += read;
        buffer->put_Length(read);
        return Make<Read>(buffer).CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        ++closes;
        return S_OK;
    }
};
// Waits for a load and returns its result, or the failure as a large value.
static UINT32 Loaded(IDataReader* reader, UINT32 count) {
    ComPtr<__FIAsyncOperation_1_UINT32> operation;
    if (FAILED(reader->LoadAsync(count, &operation))) return 0xffffffff;
    Event done(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    HANDLE signal = done.Get();
    auto handler =
        Callback<Implements<RuntimeClassFlags<ClassicCom>, __FIAsyncOperationCompletedHandler_1_UINT32, FtmBase>>(
            [signal](__FIAsyncOperation_1_UINT32*, AsyncStatus) {
                SetEvent(signal);
                return S_OK;
            });
    if (FAILED(operation->put_Completed(handler.Get())) || WaitForSingleObject(signal, 10000) != WAIT_OBJECT_0)
        return 0xfffffffe;
    UINT32 result = 0;
    return FAILED(operation->GetResults(&result)) ? 0xfffffffd : result;
}
static std::wstring Text(IDataReader* reader, UINT32 count, HRESULT* hr) {
    HString value;
    *hr = reader->ReadString(count, value.GetAddressOf());
    UINT32 length = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(value.Get(), &length);
    return std::wstring(raw, length);
}
// The game's pak and device ID reads: a reader over a stream loads, then reads.
static int StreamReader(IDataReaderStatics* statics) {
    ComPtr<IDataReaderFactory> factory;
    if (FAILED(ComPtr<IDataReaderStatics>(statics).As(&factory))) return 30;
    ComPtr<IDataReader> reader;
    auto stream = Make<ChunkStream>("hello, w\xc3\xb6rld");
    if (factory->CreateDataReader(stream.Get(), nullptr) != E_POINTER ||
        factory->CreateDataReader(nullptr, &reader) != E_INVALIDARG || reader)
        return 31;
    if (FAILED(factory->CreateDataReader(stream.Get(), &reader))) return 32;
    UINT32 size = 0;
    HRESULT hr = S_OK;
    if (FAILED(reader->get_UnconsumedBufferLength(&size)) || size != 0 || reader->ReadByte(nullptr) != E_POINTER)
        return 33;
    if (Loaded(reader.Get(), 5) != 5 || FAILED(reader->get_UnconsumedBufferLength(&size)) || size != 5) return 34;
    if (Text(reader.Get(), 5, &hr) != L"hello" || FAILED(hr)) return 35;
    // A load larger than the rest of the stream ends at its end.
    if (Loaded(reader.Get(), 100) != 8 || FAILED(reader->get_UnconsumedBufferLength(&size)) || size != 8) return 36;
    if (Text(reader.Get(), 8, &hr) != L", w\u00f6rld" || FAILED(hr) || Loaded(reader.Get(), 4) != 0) return 37;
    byte unused = 0;
    if (reader->ReadByte(&unused) != E_BOUNDS) return 38;
    ComPtr<ABI::Windows::Foundation::IClosable> close;
    if (FAILED(reader.As(&close)) || FAILED(close->Close()) || stream->closes != 1) return 39;
    if (reader->LoadAsync(1, nullptr) != E_POINTER) return 40;
    ComPtr<__FIAsyncOperation_1_UINT32> operation;
    if (reader->LoadAsync(1, &operation) != RO_E_CLOSED) return 41;
    // Invalid UTF-8 fails and consumes nothing; UTF-16 reads code units of two bytes.
    auto mixed = Make<ChunkStream>(std::string("\xff"
                                               "A\0B\0",
                                               5));
    if (FAILED(factory->CreateDataReader(mixed.Get(), &reader)) || Loaded(reader.Get(), 5) != 5) return 42;
    Text(reader.Get(), 1, &hr);
    if (hr != HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION) || FAILED(reader->get_UnconsumedBufferLength(&size)) ||
        size != 5)
        return 43;
    if (FAILED(reader->ReadByte(&unused)) || FAILED(reader->put_UnicodeEncoding(UnicodeEncoding_Utf16LE)) ||
        Text(reader.Get(), 2, &hr) != L"AB" || FAILED(hr))
        return 44;
    // Partial loads return after one read; a detached stream outlives the reader.
    auto partial = Make<ChunkStream>("abcdefgh");
    if (FAILED(factory->CreateDataReader(partial.Get(), &reader)) ||
        FAILED(reader->put_InputStreamOptions(InputStreamOptions_Partial)) || Loaded(reader.Get(), 8) != 3)
        return 45;
    ComPtr<IInputStream> detached;
    if (FAILED(reader->DetachStream(&detached)) || detached.Get() != static_cast<IInputStream*>(partial.Get()))
        return 46;
    if (FAILED(reader.As(&close)) || FAILED(close->Close()) || partial->closes != 0) return 47;
    // A stream may retain the buffer it received. Later loads must not overwrite it.
    auto retained = Make<ChunkStream>("abcdef");
    if (FAILED(factory->CreateDataReader(retained.Get(), &reader)) || Loaded(reader.Get(), 3) != 3 ||
        Text(reader.Get(), 3, &hr) != L"abc" || FAILED(hr) || Loaded(reader.Get(), 3) != 3)
        return 48;
    ComPtr<Windows::Storage::Streams::IBufferByteAccess> access;
    byte* bytes = nullptr;
    if (FAILED(retained->firstBuffer.As(&access)) || FAILED(access->Buffer(&bytes)) || memcmp(bytes, "abc", 3))
        return 49;
    // Loading appends to unread bytes rather than discarding them.
    auto append = Make<ChunkStream>("abcdef");
    if (FAILED(factory->CreateDataReader(append.Get(), &reader)) || Loaded(reader.Get(), 2) != 2 ||
        Loaded(reader.Get(), 3) != 3 || Text(reader.Get(), 5, &hr) != L"abcde" || FAILED(hr))
        return 58;
    // Exercise pak-sized loads with multiple underlying reads and byte-for-byte verification.
    std::string pak(60 * 1024 * 1024, '\0');
    for (size_t i = 0; i < pak.size(); ++i) pak[i] = static_cast<char>((i * 17 + i / 251) & 255);
    auto large = Make<ChunkStream>(pak, 65536);
    if (FAILED(factory->CreateDataReader(large.Get(), &reader)) ||
        Loaded(reader.Get(), static_cast<UINT32>(pak.size())) != pak.size())
        return 59;
    std::string actual(pak.size(), '\0');
    if (FAILED(reader->ReadBytes(static_cast<UINT32>(actual.size()), reinterpret_cast<byte*>(actual.data()))) ||
        actual != pak)
        return 60;
    // Completion happens after LoadAsync has returned, with both success and failure.
    for (HRESULT error : {S_OK, E_ACCESSDENIED}) {
        auto pending = Make<PendingStream>();
        if (FAILED(factory->CreateDataReader(pending.Get(), &reader))) return 61;
        ComPtr<__FIAsyncOperation_1_UINT32> operation;
        if (FAILED(reader->LoadAsync(8, &operation)) ||
            WaitForSingleObject(pending->started.Get(), 10000) != WAIT_OBJECT_0 ||
            WaitForSingleObject(pending->read->assigned.Get(), 10000) != WAIT_OBJECT_0)
            return 62;
        Event done(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        auto handler =
            Callback<Implements<RuntimeClassFlags<ClassicCom>, __FIAsyncOperationCompletedHandler_1_UINT32, FtmBase>>(
                [signal = done.Get()](__FIAsyncOperation_1_UINT32*, AsyncStatus) {
                    SetEvent(signal);
                    return S_OK;
                });
        if (operation->GetResults(&size) != E_ILLEGAL_METHOD_CALL || FAILED(operation->put_Completed(handler.Get())))
            return 63;
        pending->read->Complete(error);
        if (WaitForSingleObject(done.Get(), 10000) != WAIT_OBJECT_0 || operation->GetResults(&size) != error ||
            size != 0)
            return 64;
    }
    // Cancel and close must unblock a pending underlying read, without waiting for I/O.
    for (bool closing : {false, true}) {
        auto pending = Make<PendingStream>();
        if (FAILED(factory->CreateDataReader(pending.Get(), &reader))) return 50;
        ComPtr<__FIAsyncOperation_1_UINT32> operation, other;
        if (FAILED(reader->LoadAsync(8, &operation)) ||
            WaitForSingleObject(pending->started.Get(), 10000) != WAIT_OBJECT_0 ||
            WaitForSingleObject(pending->read->assigned.Get(), 10000) != WAIT_OBJECT_0)
            return 51;
        if (operation->GetResults(&size) != E_ILLEGAL_METHOD_CALL ||
            reader->LoadAsync(1, &other) != E_ILLEGAL_METHOD_CALL ||
            reader->DetachStream(&detached) != E_ILLEGAL_METHOD_CALL)
            return 52;
        ComPtr<IAsyncInfo> info;
        if (FAILED(operation.As(&info))) return 53;
        if (closing) {
            if (FAILED(reader.As(&close)) || FAILED(close->Close())) return 54;
        } else if (FAILED(info->Cancel()))
            return 55;
        if (WaitForSingleObject(pending->read->canceled.Get(), 10000) != WAIT_OBJECT_0) return 56;
        AsyncStatus status;
        if (FAILED(info->get_Status(&status)) || status != AsyncStatus::Canceled ||
            operation->GetResults(&size) != HRESULT_FROM_WIN32(ERROR_CANCELLED))
            return 57;
        // The stream can retain its operation and deliver this after the reader's worker exits.
        pending->read->Complete();
    }
    return 0;
}
// Integer reads follow the reader's ByteOrder (little-endian until set).
static int Integers(HRESULT(WINAPI* get)(HSTRING, IActivationFactory**), IDataReaderStatics* statics) {
    ComPtr<IActivationFactory> factory;
    ComPtr<IBufferFactory> buffers;
    ComPtr<IBuffer> buffer;
    if (FAILED(get(HStringReference(L"Windows.Storage.Streams.Buffer").Get(), &factory)) ||
        FAILED(factory.As(&buffers)) || FAILED(buffers->Create(10, &buffer)))
        return 40;
    ComPtr<::Windows::Storage::Streams::IBufferByteAccess> access;
    byte* data = nullptr;
    const byte source[10] = {1, 0, 0, 0, 0, 0, 0, 2, 0x34, 0x12};
    if (FAILED(buffer.As(&access)) || FAILED(access->Buffer(&data)) || !data) return 41;
    memcpy(data, source, sizeof(source));
    ComPtr<IDataReader> reader;
    if (FAILED(buffer->put_Length(sizeof(source))) || FAILED(statics->FromBuffer(buffer.Get(), &reader))) return 42;
    INT32 first = 0;
    UINT32 second = 0;
    UINT16 third = 0;
    if (FAILED(reader->ReadInt32(&first)) || first != 1) return 43;
    if (FAILED(reader->put_ByteOrder(ByteOrder_BigEndian)) || FAILED(reader->ReadUInt32(&second)) || second != 2)
        return 44;
    if (FAILED(reader->put_ByteOrder(ByteOrder_LittleEndian)) || FAILED(reader->ReadUInt16(&third)) || third != 0x1234)
        return 45;
    if (reader->ReadInt32(&first) != E_BOUNDS || reader->ReadInt32(nullptr) != E_POINTER) return 46;
    return 0;
}
// Games read files their native code wrote without setting ByteOrder, so the
// shim's reader must start in the order Windows' own does. Ask Windows' reader
// before the shim is loaded; Wine has no implementation of its own to ask.
static int SystemByteOrder() {
    if (GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version")) {
        printf("SKIP system DataReader byte order: no Windows implementation under Wine\n");
        return 0;
    }
    ComPtr<IBufferFactory> buffers;
    ComPtr<IDataReaderStatics> statics;
    ComPtr<IBuffer> buffer;
    ComPtr<IDataReader> reader;
    ByteOrder order{};
    if (FAILED(RoGetActivationFactory(HStringReference(L"Windows.Storage.Streams.Buffer").Get(),
                                      IID_PPV_ARGS(&buffers))) ||
        FAILED(buffers->Create(4, &buffer)) ||
        FAILED(RoGetActivationFactory(HStringReference(L"Windows.Storage.Streams.DataReader").Get(),
                                      IID_PPV_ARGS(&statics))) ||
        FAILED(statics->FromBuffer(buffer.Get(), &reader)) || FAILED(reader->get_ByteOrder(&order)))
        return 50;
    printf("Windows DataReader default byte order: %s\n",
           order == ByteOrder_LittleEndian ? "LittleEndian" : "BigEndian");
    return order == ByteOrder_LittleEndian ? 0 : 51; // the shim's default, checked below (17)
}
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    if (int failed = SystemByteOrder()) return failed;
    auto* dll = LoadLibraryW(L"appxportable.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    HRESULT hr = get(HStringReference(L"Windows.Storage.Streams.DataReader").Get(), &factory);
    if (FAILED(hr)) {
        printf("FAIL DataReader factory hr=%08lx\n", hr);
        return 2;
    }
    ComPtr<IDataReaderStatics> statics;
    if (FAILED(factory.As(&statics))) return 3;
    if (FAILED(get(HStringReference(L"Windows.System.Profile.HardwareIdentification").Get(), &factory))) return 4;
    ComPtr<IHardwareIdentificationStatics> hardware;
    if (FAILED(factory.As(&hardware))) return 5;
    ComPtr<IHardwareToken> token;
    if (FAILED(hardware->GetPackageSpecificToken(nullptr, &token))) return 6;
    ComPtr<IBuffer> buffer;
    if (FAILED(token->get_Id(&buffer))) return 7;
    ComPtr<IDataReader> reader;
    if (statics->FromBuffer(buffer.Get(), nullptr) != E_POINTER ||
        statics->FromBuffer(nullptr, &reader) != E_INVALIDARG || reader)
        return 8;
    if (FAILED(statics->FromBuffer(buffer.Get(), &reader)) || !reader) return 9;
    buffer.Reset();
    token.Reset(); // Reader retains the actual source buffer.
    UINT32 size = 0;
    if (FAILED(reader->get_UnconsumedBufferLength(&size)) || size != 16) return 10;
    if (reader->get_UnconsumedBufferLength(nullptr) != E_POINTER || reader->ReadByte(nullptr) != E_POINTER ||
        reader->ReadBytes(1, nullptr) != E_POINTER)
        return 11;
    std::array<byte, 17> bytes{};
    bytes.fill(0xab);
    if (reader->ReadBytes(17, bytes.data()) != E_BOUNDS || bytes[0] != 0xab ||
        FAILED(reader->get_UnconsumedBufferLength(&size)) || size != 16)
        return 12;
    byte first = 0;
    if (FAILED(reader->ReadByte(&first)) || first != 1) return 13;
    if (FAILED(reader->ReadBytes(15, bytes.data()))) return 14;
    const byte expected[15] = {0, 'A', 'P', 2, 0, 'P', 'X', 3, 0, '0', '0', 9, 0, '0', '1'};
    if (memcmp(bytes.data(), expected, 15) != 0 || FAILED(reader->get_UnconsumedBufferLength(&size)) || size != 0)
        return 15;
    if (reader->ReadByte(&first) != E_BOUNDS || FAILED(reader->ReadBytes(0, nullptr))) return 16;
    ByteOrder order{};
    UnicodeEncoding encoding{};
    InputStreamOptions options{};
    if (FAILED(reader->get_ByteOrder(&order)) || order != ByteOrder_LittleEndian ||
        FAILED(reader->get_UnicodeEncoding(&encoding)) || encoding != UnicodeEncoding_Utf8 ||
        FAILED(reader->get_InputStreamOptions(&options)) || options != InputStreamOptions_None)
        return 17;
    if (FAILED(reader->put_ByteOrder(ByteOrder_BigEndian)) || FAILED(reader->get_ByteOrder(&order)) ||
        order != ByteOrder_BigEndian || reader->put_ByteOrder(static_cast<ByteOrder>(99)) != E_INVALIDARG)
        return 18;
    if (FAILED(reader->put_UnicodeEncoding(UnicodeEncoding_Utf16BE)) ||
        FAILED(reader->get_UnicodeEncoding(&encoding)) || encoding != UnicodeEncoding_Utf16BE ||
        reader->put_UnicodeEncoding(static_cast<UnicodeEncoding>(99)) != E_INVALIDARG)
        return 19;
    if (FAILED(reader->put_InputStreamOptions(InputStreamOptions_ReadAhead)) ||
        FAILED(reader->get_InputStreamOptions(&options)) || options != InputStreamOptions_ReadAhead ||
        reader->put_InputStreamOptions(static_cast<InputStreamOptions>(99)) != E_INVALIDARG)
        return 20;
    // A reader over a buffer has no stream to load from.
    ComPtr<__FIAsyncOperation_1_UINT32> load;
    if (reader->LoadAsync(1, &load) != E_ILLEGAL_METHOD_CALL || load) return 26;
    if (reader->DetachBuffer(nullptr) != E_POINTER || FAILED(reader->DetachBuffer(&buffer)) || !buffer) return 21;
    if (FAILED(buffer->get_Length(&size)) || size != 16 || FAILED(reader->get_UnconsumedBufferLength(&size)) ||
        size != 0 || reader->ReadByte(&first) != E_BOUNDS)
        return 22;
    ComPtr<ABI::Windows::Foundation::IClosable> close;
    if (FAILED(reader.As(&close)) || FAILED(close->Close()) || FAILED(close->Close())) return 23;
    if (reader->get_UnconsumedBufferLength(&size) != RO_E_CLOSED || reader->ReadBytes(0, nullptr) != RO_E_CLOSED ||
        reader->DetachBuffer(&buffer) != RO_E_CLOSED || buffer)
        return 24;
    if (reader->LoadAsync(1, nullptr) != E_POINTER) return 25;
    if (int failure = StreamReader(statics.Get())) {
        printf("FAIL stream reader %d\n", failure);
        return failure;
    }
    if (int failure = Integers(get, statics.Get())) {
        printf("FAIL integers %d\n", failure);
        return failure;
    }
    puts("readertest: 0 failure(s)");
    return 0;
}
