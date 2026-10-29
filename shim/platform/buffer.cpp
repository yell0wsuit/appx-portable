// Own byte buffer for Wine, whose Windows.Storage.Streams.Buffer is absent.
#include "com/factory.h"
#include "registry.h"
#include <windows.storage.streams.h>
#include <robuffer.h>
#include <vector>
#include <atomic>
#include <stdexcept>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Storage::Streams;
namespace appx {
namespace {
class ByteBuffer final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IBuffer,
                                            Windows::Storage::Streams::IBufferByteAccess, FtmBase> {
    InspectableClass(L"Windows.Storage.Streams.Buffer", BaseTrust);
    std::vector<byte> bytes_;
    std::atomic<UINT32> length_{0};
public:
    explicit ByteBuffer(UINT32 capacity) : bytes_(capacity) {}
    HRESULT STDMETHODCALLTYPE get_Capacity(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<UINT32>(bytes_.size()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Length(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = length_.load(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Length(UINT32 value) override {
        if (value > bytes_.size()) return E_INVALIDARG;
        length_.store(value); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Buffer(byte** value) noexcept override {
        if (!value) return E_POINTER;
        *value = bytes_.data(); return S_OK;
    }
};
class BufferFactory final : public Factory<IBufferFactory> {
public:
    BufferFactory() : Factory(L"Windows.Storage.Streams.Buffer") {}
    HRESULT STDMETHODCALLTYPE Create(UINT32 capacity, IBuffer** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (capacity > std::vector<byte>().max_size()) return E_OUTOFMEMORY;
        try {
            auto buffer = Make<ByteBuffer>(capacity);
            return buffer ? buffer.CopyTo(value) : E_OUTOFMEMORY;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        catch (const std::length_error&) { return E_OUTOFMEMORY; }
    }
};
const ShimClass bufferClass(L"Windows.Storage.Streams.Buffer", [](IActivationFactory** value) {
    auto factory = Make<BufferFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}, ShimPolicy::FillMissing);
}
}
