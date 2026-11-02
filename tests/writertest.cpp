#include <windows.h>
#include <roapi.h>
#include <windows.storage.streams.h>
#include <robuffer.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <cstring>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Storage::Streams;
using ABI::Windows::Foundation::IClosable;
class Output final : public RuntimeClass<RuntimeClassFlags<WinRt>, IOutputStream, IClosable, FtmBase> {
    InspectableClass(L"Test.Output", BaseTrust);

public:
    int closes = 0;
    HRESULT STDMETHODCALLTYPE WriteAsync(IBuffer*, __FIAsyncOperationWithProgress_2_UINT32_UINT32**) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE FlushAsync(__FIAsyncOperation_1_boolean**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Close() override {
        ++closes;
        return S_OK;
    }
};
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* module = LoadLibraryW(L"appxportable.dll");
    if (!module) return 1;
    auto get =
        reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(module, "ShimGetFactory"));
    ComPtr<IActivationFactory> activation;
    if (FAILED(get(HStringReference(L"Windows.Storage.Streams.DataWriter").Get(), &activation))) return 2;
    ComPtr<IDataWriterFactory> factory;
    if (FAILED(activation.As(&factory))) return 3;
    auto output = Make<Output>();
    ComPtr<IDataWriter> writer;
    if (factory->CreateDataWriter(output.Get(), nullptr) != E_POINTER ||
        FAILED(factory->CreateDataWriter(output.Get(), &writer)))
        return 4;
    const wchar_t text[] = {L'A', 0, 0x00f6, 0xd83d, 0xdc8e};
    HString value;
    if (FAILED(value.Set(text, 5))) return 5;
    UINT32 written = 0, length = 0;
    if (writer->WriteString(value.Get(), nullptr) != E_POINTER || FAILED(writer->WriteString(value.Get(), &written)) ||
        written != 8)
        return 6;
    if (FAILED(writer->WriteString(HStringReference(L"!").Get(), &written)) || written != 1 ||
        FAILED(writer->get_UnstoredBufferLength(&length)) || length != 9)
        return 7;
    if (writer->DetachBuffer(nullptr) != E_POINTER) return 8;
    ComPtr<IBuffer> buffer;
    if (FAILED(writer->DetachBuffer(&buffer)) || FAILED(buffer->get_Length(&length)) || length != 9) return 9;
    ComPtr<Windows::Storage::Streams::IBufferByteAccess> access;
    byte* bytes = nullptr;
    const byte expected[] = {'A', 0, 0xc3, 0xb6, 0xf0, 0x9f, 0x92, 0x8e, '!'};
    if (FAILED(buffer.As(&access)) || FAILED(access->Buffer(&bytes)) || memcmp(bytes, expected, 9)) return 10;
    if (FAILED(writer->get_UnstoredBufferLength(&length)) || length != 0) return 11;
    ComPtr<IBuffer> empty;
    if (FAILED(writer->DetachBuffer(&empty)) || FAILED(empty->get_Length(&length)) || length != 0) return 12;
    ComPtr<IClosable> close;
    if (FAILED(writer.As(&close)) || FAILED(close->Close()) || FAILED(close->Close()) || output->closes != 1) return 13;
    if (writer->WriteString(value.Get(), &written) != RO_E_CLOSED || written != 0 ||
        writer->DetachBuffer(&empty) != RO_E_CLOSED || empty || memcmp(bytes, expected, 9))
        return 14;
    // Detachment transfers the output stream, so closing the writer leaves it open.
    output = Make<Output>();
    if (FAILED(factory->CreateDataWriter(output.Get(), &writer))) return 15;
    ComPtr<IOutputStream> detached;
    if (FAILED(writer->DetachStream(&detached)) || detached.Get() != static_cast<IOutputStream*>(output.Get()) ||
        FAILED(writer.As(&close)) || FAILED(close->Close()) || output->closes != 0)
        return 16;
    ComPtr<IInspectable> instance;
    if (FAILED(activation->ActivateInstance(&instance)) || FAILED(instance.As(&writer)) ||
        FAILED(writer->WriteString(nullptr, &written)) || written != 0)
        return 17;
    puts("writertest: 0 failure(s)");
    return 0;
}
