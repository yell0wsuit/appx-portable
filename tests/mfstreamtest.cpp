#include <windows.h>
#include <windows.storage.h>
#include <windows.storage.streams.h>
#include <mfapi.h>
#include <mfidl.h>
#include <shlwapi.h>
#include <shcore.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <cstring>
#include <thread>
#include <robuffer.h>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Storage;
using namespace ABI::Windows::Storage::Streams;
using namespace ABI::Windows::Foundation;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL line %d: %s\n", __LINE__, #x);                                                                \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
class ReadDone final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IMFAsyncCallback, FtmBase> {
public:
    Event event{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    ComPtr<IMFAsyncResult> result;
    HRESULT STDMETHODCALLTYPE GetParameters(DWORD*, DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(IMFAsyncResult* asyncResult) override {
        result = asyncResult;
        SetEvent(event.Get());
        return S_OK;
    }
};
bool Wait(IInspectable* operation) {
    ComPtr<IAsyncInfo> info;
    if (FAILED(operation->QueryInterface(IID_PPV_ARGS(&info)))) return false;
    const auto deadline = GetTickCount64() + 5000;
    AsyncStatus status = AsyncStatus::Started;
    do {
        if (FAILED(info->get_Status(&status))) return false;
        if (status != AsyncStatus::Started) return status == AsyncStatus::Completed;
        Sleep(1);
    } while (GetTickCount64() < deadline);
    return false;
}
using ReadOperation = IAsyncOperationWithProgress<IBuffer*, UINT32>;
using ReadHandler = IAsyncOperationWithProgressCompletedHandler<IBuffer*, UINT32>;
using ProgressHandler = IAsyncOperationProgressHandler<IBuffer*, UINT32>;
class AlternateBuffer final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IBuffer,
                                                  ::Windows::Storage::Streams::IBufferByteAccess, FtmBase> {
    InspectableClass(L"Test.AlternateBuffer", BaseTrust);
    BYTE data_[64]{};
    UINT32 length_ = 0;

public:
    HRESULT STDMETHODCALLTYPE get_Capacity(UINT32* value) override {
        *value = sizeof(data_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Length(UINT32* value) override {
        *value = length_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Length(UINT32 value) override {
        if (value > sizeof(data_)) return E_INVALIDARG;
        length_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Buffer(byte** value) noexcept override {
        *value = data_;
        return S_OK;
    }
};
// Finish only after put_Completed has returned. Return a different buffer from
// the one the adapter supplied; GetResults also checks that it waited.
class DelayedRead final : public RuntimeClass<RuntimeClassFlags<WinRt>, ReadOperation, IAsyncInfo, FtmBase> {
    InspectableClass(L"Test.DelayedRead", BaseTrust);
    ComPtr<IBuffer> result_;
    HRESULT error_;
    std::atomic<bool> finished_{false};

public:
    DelayedRead(IBuffer* result, HRESULT error) : result_(result), error_(error) {}
    HRESULT STDMETHODCALLTYPE put_Completed(ReadHandler* handler) override {
        ComPtr<DelayedRead> self(this);
        ComPtr<ReadHandler> callback(handler);
        std::thread([self, callback] {
            Sleep(20);
            self->finished_ = true;
            callback->Invoke(self.Get(), FAILED(self->error_) ? AsyncStatus::Error : AsyncStatus::Completed);
        }).detach();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Completed(ReadHandler**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_Progress(ProgressHandler*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Progress(ProgressHandler**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetResults(IBuffer** value) override {
        if (!finished_) return E_ILLEGAL_METHOD_CALL;
        return FAILED(error_) ? error_ : result_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_Id(UINT32* value) override {
        *value = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Status(AsyncStatus* value) override {
        *value = !finished_ ? AsyncStatus::Started : FAILED(error_) ? AsyncStatus::Error : AsyncStatus::Completed;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* value) override {
        *value = error_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Cancel() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};
class DelayedInput final : public RuntimeClass<RuntimeClassFlags<WinRt>, IInputStream, FtmBase> {
    InspectableClass(L"Test.DelayedInput", BaseTrust);
    ComPtr<IInputStream> source_;
    HRESULT error_;
    bool nullResult_;

public:
    DelayedInput(IInputStream* source, HRESULT error, bool nullResult = false)
        : source_(source), error_(error), nullResult_(nullResult) {}
    HRESULT STDMETHODCALLTYPE ReadAsync(IBuffer*, UINT32 count, InputStreamOptions options,
                                        ReadOperation** value) override {
        auto buffer = Make<AlternateBuffer>();
        ComPtr<IBuffer> filled;
        if (SUCCEEDED(error_) && !nullResult_) {
            ComPtr<ReadOperation> operation;
            auto hr = source_->ReadAsync(buffer.Get(), count, options, &operation);
            if (FAILED(hr)) return hr;
            if (!Wait(operation.Get())) return E_FAIL;
            hr = operation->GetResults(&filled);
            if (FAILED(hr)) return hr;
        }
        auto delayed = Make<DelayedRead>(filled.Get(), error_);
        return delayed.CopyTo(value);
    }
};
class DelayedSource final : public RuntimeClass<RuntimeClassFlags<WinRt>, IRandomAccessStream, FtmBase> {
    InspectableClass(L"Test.DelayedSource", BaseTrust);
    ComPtr<IRandomAccessStream> source_;
    HRESULT error_;
    bool* destroyed_;
    bool nullResult_;

public:
    DelayedSource(IRandomAccessStream* source, HRESULT error, bool* destroyed = nullptr, bool nullResult = false)
        : source_(source), error_(error), destroyed_(destroyed), nullResult_(nullResult) {}
    ~DelayedSource() {
        if (destroyed_) *destroyed_ = true;
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT64* value) override { return source_->get_Size(value); }
    HRESULT STDMETHODCALLTYPE put_Size(UINT64 value) override { return source_->put_Size(value); }
    HRESULT STDMETHODCALLTYPE get_Position(UINT64* value) override { return source_->get_Position(value); }
    HRESULT STDMETHODCALLTYPE Seek(UINT64 value) override { return source_->Seek(value); }
    HRESULT STDMETHODCALLTYPE CloneStream(IRandomAccessStream** value) override { return source_->CloneStream(value); }
    HRESULT STDMETHODCALLTYPE get_CanRead(boolean* value) override { return source_->get_CanRead(value); }
    HRESULT STDMETHODCALLTYPE get_CanWrite(boolean* value) override { return source_->get_CanWrite(value); }
    HRESULT STDMETHODCALLTYPE GetInputStreamAt(UINT64 position, IInputStream** value) override {
        ComPtr<IInputStream> input;
        auto hr = source_->GetInputStreamAt(position, &input);
        if (FAILED(hr)) return hr;
        return Make<DelayedInput>(input.Get(), error_, nullResult_).CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE GetOutputStreamAt(UINT64 position, IOutputStream** value) override {
        return source_->GetOutputStreamAt(position, value);
    }
};
int main() {
    CHECK(SUCCEEDED(RoInitialize(RO_INIT_MULTITHREADED)));
    CHECK(SUCCEEDED(MFStartup(MF_VERSION)));
    HMODULE shim = LoadLibraryW(L"appxportable.dll");
    CHECK(shim);
    auto get =
        reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(shim, "ShimGetFactory"));
    CHECK(get);
    wchar_t path[MAX_PATH]{};
    CHECK(GetFullPathNameW(L"mfstream-fixture.bin", MAX_PATH, path, nullptr));
    constexpr char payload[] = "prefix: embedded media payload";
    FileHandle file{CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr)};
    CHECK(file.IsValid());
    DWORD written = 0;
    CHECK(WriteFile(file.Get(), payload, sizeof(payload) - 1, &written, nullptr));
    file.Close();
    ComPtr<IActivationFactory> factory;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.Storage.StorageFile").Get(), &factory)));
    ComPtr<IStorageFileStatics> files;
    CHECK(SUCCEEDED(factory.As(&files)));
    ComPtr<IAsyncOperation<StorageFile*>> lookup;
    CHECK(SUCCEEDED(files->GetFileFromPathAsync(HStringReference(path).Get(), &lookup)));
    CHECK(Wait(lookup.Get()));
    ComPtr<IStorageFile> storage;
    CHECK(SUCCEEDED(lookup->GetResults(&storage)));
    ComPtr<IAsyncOperation<IRandomAccessStreamWithContentType*>> opening;
    ComPtr<IRandomAccessStreamReference> reference;
    CHECK(SUCCEEDED(storage.As(&reference)));
    CHECK(SUCCEEDED(reference->OpenReadAsync(&opening)));
    CHECK(Wait(opening.Get()));
    ComPtr<IRandomAccessStreamWithContentType> source;
    CHECK(SUCCEEDED(opening->GetResults(&source)));
    ComPtr<IRandomAccessStream> randomSource;
    CHECK(SUCCEEDED(source.As(&randomSource)));
    // Native request bodies use shcore's WinRT-to-IStream conversion.
    ComPtr<IStream> bridge;
    CHECK(SUCCEEDED(CreateStreamOverRandomAccessStream(source.Get(), IID_PPV_ARGS(&bridge))));
    STATSTG stat{};
    CHECK(SUCCEEDED(bridge->Stat(&stat, STATFLAG_NONAME)) && stat.type == STGTY_STREAM &&
          stat.cbSize.QuadPart == sizeof(payload) - 1);
    BYTE bridged[64]{};
    ULONG bridgedRead = 0;
    CHECK(bridge->Read(bridged, 7, &bridgedRead) == S_OK && bridgedRead == 7 && !memcmp(bridged, payload, 7));
    LARGE_INTEGER move{};
    move.QuadPart = -2;
    ULARGE_INTEGER bridgedPosition{};
    CHECK(SUCCEEDED(bridge->Seek(move, STREAM_SEEK_CUR, &bridgedPosition)) && bridgedPosition.QuadPart == 5);
    move.QuadPart = sizeof(payload) - 3;
    CHECK(SUCCEEDED(bridge->Seek(move, STREAM_SEEK_SET, nullptr)));
    CHECK(bridge->Read(bridged, 8, &bridgedRead) == S_FALSE && bridgedRead == 2 &&
          !memcmp(bridged, payload + sizeof(payload) - 3, 2));
    CHECK(bridge->Read(bridged, 1, &bridgedRead) == S_FALSE && bridgedRead == 0);
    CHECK(bridge->Write(payload, 1, &bridgedRead) == STG_E_ACCESSDENIED && bridgedRead == 0);
    bridge.Reset();
    CHECK(SUCCEEDED(randomSource->Seek(0)));
    ComPtr<IMFByteStream> bytes;
    HRESULT hr = MFCreateMFByteStreamOnStreamEx(source.Get(), &bytes);
    printf("MFCreateMFByteStreamOnStreamEx hr=%08lx\n", hr);
    CHECK(SUCCEEDED(hr) && bytes);
    DWORD capabilities = 0;
    CHECK(SUCCEEDED(bytes->GetCapabilities(&capabilities)) && (capabilities & MFBYTESTREAM_IS_READABLE) &&
          (capabilities & MFBYTESTREAM_IS_SEEKABLE) && !(capabilities & MFBYTESTREAM_IS_WRITABLE));
    QWORD length = 0, position = 0;
    CHECK(SUCCEEDED(bytes->GetLength(&length)) && length == sizeof(payload) - 1);
    BYTE buffer[64]{};
    ULONG read = 0;
    CHECK(SUCCEEDED(bytes->Read(buffer, 7, &read)) && read == 7 && !memcmp(buffer, payload, 7));
    CHECK(SUCCEEDED(bytes->Seek(msoCurrent, 2, 0, &position)) && position == 9);
    CHECK(SUCCEEDED(bytes->Seek(msoCurrent, -2, 0, &position)) && position == 7);
    CHECK(SUCCEEDED(bytes->GetCurrentPosition(&position)) && position == 7);
    CHECK(SUCCEEDED(bytes->SetCurrentPosition(9)));
    auto done = Make<ReadDone>();
    CHECK(SUCCEEDED(bytes->BeginRead(buffer, 8, done.Get(), nullptr)));
    CHECK(WaitForSingleObject(done->event.Get(), 5000) == WAIT_OBJECT_0);
    CHECK(SUCCEEDED(bytes->EndRead(done->result.Get(), &read)) && read == 8 && !memcmp(buffer, payload + 9, 8));
    CHECK(SUCCEEDED(bytes->SetCurrentPosition(length - 2)));
    CHECK(SUCCEEDED(bytes->Read(buffer, 8, &read)) && read == 2 && !memcmp(buffer, payload + length - 2, 2));
    CHECK(SUCCEEDED(bytes->Read(buffer, 8, &read)) && read == 0);
    BOOL end = FALSE;
    CHECK(SUCCEEDED(bytes->IsEndOfStream(&end)) && end);
    CHECK(SUCCEEDED(bytes->SetCurrentPosition(length + 10)));
    CHECK(SUCCEEDED(bytes->Read(buffer, 8, &read)) && read == 0);
    CHECK(SUCCEEDED(bytes->Close()));
    bytes.Reset();
    // These fixtures target the Wine fallback, rather than Microsoft's adapter.
    if (GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version")) {
        ComPtr<IRandomAccessStream> random;
        CHECK(SUCCEEDED(source.As(&random)));
        CHECK(CreateStreamOverRandomAccessStream(nullptr, IID_PPV_ARGS(&bridge)) == E_POINTER && !bridge);
        CHECK(CreateStreamOverRandomAccessStream(source.Get(), __uuidof(IStream), nullptr) == E_POINTER);
        ComPtr<IMFByteStream> unsupported;
        CHECK(CreateStreamOverRandomAccessStream(source.Get(), IID_PPV_ARGS(&unsupported)) == E_NOINTERFACE &&
              !unsupported);
        CHECK(SUCCEEDED(random->Seek(0)));
        bool destroyed = false;
        auto owned = Make<DelayedSource>(random.Get(), S_OK, &destroyed);
        CHECK(SUCCEEDED(
            CreateStreamOverRandomAccessStream(static_cast<IRandomAccessStream*>(owned.Get()), IID_PPV_ARGS(&bridge))));
        owned.Reset();
        CHECK(!destroyed);
        CHECK(bridge->Read(buffer, 7, &read) == S_OK && read == 7 && !memcmp(buffer, payload, 7));
        bridge.Reset();
        CHECK(destroyed);
        auto broken = Make<DelayedSource>(random.Get(), S_OK, nullptr, true);
        CHECK(SUCCEEDED(CreateStreamOverRandomAccessStream(static_cast<IRandomAccessStream*>(broken.Get()),
                                                           IID_PPV_ARGS(&bridge))));
        CHECK(bridge->Read(buffer, 7, &read) == E_UNEXPECTED && read == 0);
        bridge.Reset();
        broken.Reset();
        CHECK(SUCCEEDED(random->Seek(0)));
        auto delayed = Make<DelayedSource>(random.Get(), S_OK);
        CHECK(SUCCEEDED(MFCreateMFByteStreamOnStreamEx(static_cast<IRandomAccessStream*>(delayed.Get()), &bytes)));
        CHECK(SUCCEEDED(bytes->Read(buffer, 7, &read)) && read == 7 && !memcmp(buffer, payload, 7));
        CHECK(SUCCEEDED(bytes->Close()));
        bytes.Reset();
        auto failing = Make<DelayedSource>(random.Get(), E_ACCESSDENIED);
        CHECK(SUCCEEDED(MFCreateMFByteStreamOnStreamEx(static_cast<IRandomAccessStream*>(failing.Get()), &bytes)));
        CHECK(bytes->Read(buffer, 7, &read) == E_ACCESSDENIED);
        CHECK(SUCCEEDED(bytes->Close()));
        bytes.Reset();
        failing.Reset();
        delayed.Reset();
        random.Reset();
    }
    opening.Reset();
    randomSource.Reset();
    source.Reset();
    storage.Reset();
    lookup.Reset();
    CHECK(DeleteFileW(path));
    // The same overload also accepts a classic COM stream.
    ComPtr<IStream> classic;
    classic.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(payload), sizeof(payload) - 1));
    CHECK(classic);
    CHECK(SUCCEEDED(MFCreateMFByteStreamOnStreamEx(classic.Get(), &bytes)));
    CHECK(SUCCEEDED(bytes->Read(buffer, 7, &read)) && read == 7 && !memcmp(buffer, payload, 7));
    CHECK(SUCCEEDED(bytes->Close()));
    bytes.Reset();
    MFShutdown();
    puts("mfstreamtest: conversion, reads, seeking, EOF and async read passed");
    return 0;
}
