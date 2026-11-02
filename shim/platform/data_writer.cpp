// A small native writer for systems whose DataWriter cannot encode strings.
#include "registry.h"
#include "com/factory.h"
#include "generated/streams.h"
#include <robuffer.h>
#include <wrl/wrappers/corewrappers.h>
#include <climits>
#include <cstring>
#include <mutex>
#include <vector>
#include <stdexcept>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Storage::Streams;
using ABI::Windows::Foundation::IClosable;
using Windows::Storage::Streams::IBufferByteAccess;
namespace appx {
namespace {
// A constructor probe owns no external resource and must never be written to.
class ProbeOutput final : public RuntimeClass<RuntimeClassFlags<WinRt>, IOutputStream, IClosable, FtmBase> {
    InspectableClass(L"AppxPortable.WriterProbe", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE WriteAsync(IBuffer*, __FIAsyncOperationWithProgress_2_UINT32_UINT32** value) override {
        if (value) *value = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE FlushAsync(__FIAsyncOperation_1_boolean** value) override {
        if (value) *value = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};
class Writer final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIDataWriter, IClosable, FtmBase> {
    InspectableClass(L"Windows.Storage.Streams.DataWriter", BaseTrust);
    std::mutex mutex_;
    ComPtr<IOutputStream> stream_;
    std::vector<byte> bytes_;
    bool closed_ = false;

public:
    explicit Writer(IOutputStream* stream) : stream_(stream) {}
    HRESULT STDMETHODCALLTYPE get_UnstoredBufferLength(UINT32* value) override {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(mutex_);
        *value = 0;
        if (closed_) return RO_E_CLOSED;
        *value = static_cast<UINT32>(bytes_.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE WriteString(HSTRING value, UINT32* count) override {
        if (!count) return E_POINTER;
        *count = 0;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        UINT32 length = 0;
        const wchar_t* text = WindowsGetStringRawBuffer(value, &length);
        if (!length) return S_OK;
        if (length > INT_MAX) return E_INVALIDARG;
        const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, static_cast<int>(length), nullptr,
                                                 0, nullptr, nullptr);
        if (!required) return HRESULT_FROM_WIN32(GetLastError());
        if (static_cast<size_t>(required) > UINT32_MAX - bytes_.size()) return E_OUTOFMEMORY;
        const size_t start = bytes_.size();
        try {
            bytes_.resize(start + required);
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        } catch (const std::length_error&) {
            return E_OUTOFMEMORY;
        }
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, static_cast<int>(length),
                                 reinterpret_cast<char*>(bytes_.data() + start), required, nullptr, nullptr)) {
            const HRESULT error = HRESULT_FROM_WIN32(GetLastError());
            bytes_.resize(start);
            return error;
        }
        *count = static_cast<UINT32>(required);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DetachBuffer(IBuffer** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        ComPtr<IActivationFactory> activation;
        auto hr = ShimGetFactory(HStringReference(L"Windows.Storage.Streams.Buffer").Get(), &activation);
        ComPtr<IBufferFactory> factory;
        if (SUCCEEDED(hr)) hr = activation.As(&factory);
        ComPtr<IBuffer> buffer;
        if (SUCCEEDED(hr)) hr = factory->Create(static_cast<UINT32>(bytes_.size()), &buffer);
        ComPtr<IBufferByteAccess> access;
        if (SUCCEEDED(hr)) hr = buffer.As(&access);
        byte* target = nullptr;
        if (SUCCEEDED(hr)) hr = access->Buffer(&target);
        if (FAILED(hr)) return hr;
        if (!bytes_.empty()) {
            if (!target) return E_POINTER;
            memcpy(target, bytes_.data(), bytes_.size());
        }
        hr = buffer->put_Length(static_cast<UINT32>(bytes_.size()));
        if (FAILED(hr)) return hr;
        bytes_.clear();
        return buffer.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE DetachStream(IOutputStream** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return RO_E_CLOSED;
        *value = stream_.Detach();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        ComPtr<IOutputStream> stream;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return S_OK;
            closed_ = true;
            bytes_.clear();
            stream.Attach(stream_.Detach());
        }
        ComPtr<IClosable> close;
        return stream && SUCCEEDED(stream.As(&close)) ? close->Close() : S_OK;
    }
};
class WriterFactory final : public Factory<IDataWriterFactory> {
public:
    WriterFactory() : Factory(L"Windows.Storage.Streams.DataWriter") {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto writer = Make<Writer>(nullptr);
        return writer ? writer.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE CreateDataWriter(IOutputStream* stream, IDataWriter** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto writer = Make<Writer>(stream);
        return writer ? writer.CopyTo(value) : E_OUTOFMEMORY;
    }
};
HRESULT MakeWriterFactory(IActivationFactory** value) {
    // Activation alone is not sufficient: Wine supplies a factory but stubs WriteString.
    // Probe the consumed constructor and operations using an isolated output stream.
    ComPtr<IActivationFactory> system;
    if (SUCCEEDED(OsGetActivationFactory(HStringReference(L"Windows.Storage.Streams.DataWriter").Get(),
                                         IID_PPV_ARGS(&system)))) {
        ComPtr<IDataWriterFactory> constructor;
        auto output = Make<ProbeOutput>();
        if (!output) return E_OUTOFMEMORY;
        ComPtr<IDataWriter> writer;
        ComPtr<IClosable> close;
        if (SUCCEEDED(system.As(&constructor)) && SUCCEEDED(constructor->CreateDataWriter(output.Get(), &writer)) &&
            SUCCEEDED(writer.As(&close))) {
            UINT32 written = 0;
            const auto result = writer->WriteString(HStringReference(L"x").Get(), &written);
            ComPtr<IBuffer> buffer;
            UINT32 length = 0;
            const bool detached = SUCCEEDED(result) && written == 1 && SUCCEEDED(writer->DetachBuffer(&buffer)) &&
                                  buffer && SUCCEEDED(buffer->get_Length(&length)) && length == 1;
            const auto closed = close->Close();
            if (detached && SUCCEEDED(closed)) return system.CopyTo(value);
        }
    }
    auto factory = Make<WriterFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
const ShimClass writerClass{L"Windows.Storage.Streams.DataWriter", MakeWriterFactory};
}
}
