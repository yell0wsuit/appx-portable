#include "mf_stream.h"
#include "log.h"
#include <windows.storage.streams.h>
#include <robuffer.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <memory>
#include <limits>
#include <mutex>
#include <vector>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Storage::Streams;
using namespace ABI::Windows::Foundation;
namespace appx {
namespace {
// Own the read's storage until the WinRT operation has completed, including when
// the source completes asynchronously and returns a different buffer.
class ReadBuffer final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IBuffer,
                                             Windows::Storage::Streams::IBufferByteAccess, FtmBase> {
    InspectableClass(L"Windows.Storage.Streams.Buffer", BaseTrust);
    std::vector<byte> bytes_;
    UINT32 length_ = 0;

public:
    explicit ReadBuffer(UINT32 capacity) : bytes_(capacity) {}
    HRESULT STDMETHODCALLTYPE get_Capacity(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<UINT32>(bytes_.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Length(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = length_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Length(UINT32 value) override {
        if (value > bytes_.size()) return E_INVALIDARG;
        length_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Buffer(byte** value) noexcept override {
        if (!value) return E_POINTER;
        *value = bytes_.data();
        return S_OK;
    }
};
class Stream final
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, ChainInterfaces<IStream, ISequentialStream>, FtmBase> {
    ComPtr<IRandomAccessStream> source_;
    std::mutex mutex_;

public:
    explicit Stream(IRandomAccessStream* source) : source_(source) {}
    HRESULT STDMETHODCALLTYPE Read(void* destination, ULONG count, ULONG* read) override {
        if (read) *read = 0;
        if (!destination && count) return STG_E_INVALIDPOINTER;
        if (!count) return S_OK;
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            UINT64 position = 0;
            auto hr = source_->get_Position(&position);
            ComPtr<IInputStream> input;
            if (SUCCEEDED(hr)) hr = source_->GetInputStreamAt(position, &input);
            if (FAILED(hr)) return hr;
            auto buffer = Make<ReadBuffer>(count);
            if (!buffer) return E_OUTOFMEMORY;
            ComPtr<IAsyncOperationWithProgress<IBuffer*, UINT32>> operation;
            hr = input->ReadAsync(buffer.Get(), count, InputStreamOptions_None, &operation);
            if (FAILED(hr)) return hr;
            auto event = std::make_shared<Wrappers::Event>(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!event->IsValid()) return HRESULT_FROM_WIN32(GetLastError());
            auto completed = Callback<IAsyncOperationWithProgressCompletedHandler<IBuffer*, UINT32>>(
                [event](IAsyncOperationWithProgress<IBuffer*, UINT32>*, AsyncStatus) -> HRESULT {
                    SetEvent(event->Get());
                    return S_OK;
                });
            if (!completed) return E_OUTOFMEMORY;
            hr = operation->put_Completed(completed.Get());
            if (FAILED(hr)) return hr;
            if (WaitForSingleObject(event->Get(), INFINITE) != WAIT_OBJECT_0) return HRESULT_FROM_WIN32(GetLastError());
            ComPtr<IBuffer> filled;
            hr = operation->GetResults(&filled);
            UINT32 length = 0;
            if (SUCCEEDED(hr)) hr = filled->get_Length(&length);
            if (FAILED(hr)) return hr;
            if (length > count) return E_UNEXPECTED;
            ComPtr<Windows::Storage::Streams::IBufferByteAccess> access;
            byte* bytes = nullptr;
            hr = filled.As(&access);
            if (SUCCEEDED(hr)) hr = access->Buffer(&bytes);
            if (FAILED(hr)) return hr;
            if (length) memcpy(destination, bytes, length);
            hr = source_->Seek(position + length);
            if (FAILED(hr)) return hr;
            if (read) *read = length;
            return length == count ? S_OK : S_FALSE;
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        }
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* result) override {
        std::lock_guard<std::mutex> lock(mutex_);
        UINT64 base = 0;
        HRESULT hr = S_OK;
        if (origin == STREAM_SEEK_CUR)
            hr = source_->get_Position(&base);
        else if (origin == STREAM_SEEK_END)
            hr = source_->get_Size(&base);
        else if (origin != STREAM_SEEK_SET)
            return STG_E_INVALIDFUNCTION;
        if (FAILED(hr)) return hr;
        UINT64 magnitude =
            move.QuadPart < 0 ? static_cast<UINT64>(-(move.QuadPart + 1)) + 1 : static_cast<UINT64>(move.QuadPart);
        if ((move.QuadPart < 0 && magnitude > base) ||
            (move.QuadPart >= 0 && magnitude > std::numeric_limits<UINT64>::max() - base))
            return STG_E_INVALIDFUNCTION;
        UINT64 position = move.QuadPart < 0 ? base - magnitude : base + magnitude;
        hr = source_->Seek(position);
        if (SUCCEEDED(hr) && result) result->QuadPart = position;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override {
        if (!stat) return STG_E_INVALIDPOINTER;
        *stat = {};
        stat->type = STGTY_STREAM;
        stat->grfMode = STGM_READ;
        return source_->get_Size(&stat->cbSize.QuadPart);
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream** clone) override {
        if (!clone) return E_POINTER;
        *clone = nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        UINT64 position = 0;
        ComPtr<IRandomAccessStream> copy;
        auto hr = source_->get_Position(&position);
        if (SUCCEEDED(hr)) hr = source_->CloneStream(&copy);
        if (SUCCEEDED(hr)) hr = copy->Seek(position);
        if (FAILED(hr)) return hr;
        auto stream = Make<Stream>(copy.Get());
        return stream ? stream.CopyTo(clone) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG* written) override {
        if (written) *written = 0;
        return STG_E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return STG_E_INVALIDFUNCTION;
    }
};
}
HRESULT WINAPI CreateMFByteStreamOnStreamEx(IUnknown* source, IMFByteStream** result) noexcept {
    if (!result) return E_POINTER;
    *result = nullptr;
    if (!source) return E_POINTER;
    using Convert = HRESULT(WINAPI*)(IUnknown*, IMFByteStream**);
    using Wrap = HRESULT(WINAPI*)(IStream*, IMFByteStream**);
    static HMODULE module = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto native =
        module ? reinterpret_cast<Convert>(GetProcAddress(module, "MFCreateMFByteStreamOnStreamEx")) : nullptr;
    auto wrap = module ? reinterpret_cast<Wrap>(GetProcAddress(module, "MFCreateMFByteStreamOnStream")) : nullptr;
    HRESULT hr = native ? native(source, result) : E_NOTIMPL;
    if (hr != E_NOTIMPL || !wrap) return hr;
    try {
        ComPtr<IStream> stream;
        hr = source->QueryInterface(IID_PPV_ARGS(&stream));
        if (FAILED(hr)) {
            ComPtr<IRandomAccessStream> random;
            hr = source->QueryInterface(IID_PPV_ARGS(&random));
            if (FAILED(hr)) return hr;
            boolean readable = false;
            hr = random->get_CanRead(&readable);
            if (FAILED(hr)) return hr;
            if (!readable) return STG_E_ACCESSDENIED;
            stream = Make<Stream>(random.Get());
            if (!stream) return E_OUTOFMEMORY;
        }
        hr = wrap(stream.Get(), result);
        Log("TRACE", "MF WinRT stream conversion fallback hr=%08lx", hr);
        return hr;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
}
}
