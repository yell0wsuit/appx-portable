// CryptographicBuffer and hash providers for Wine, whose cryptowinrt.dll stubs them.
// Behaviour follows Windows' CryptoWinRT.dll.
#include "registry.h"
#include "buffer.h"
#include "com/factory.h"
#include <windows.security.cryptography.h>
#include <windows.security.cryptography.core.h>
#include <bcrypt.h>
#include <robuffer.h>
#include <string>
#include <vector>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Security::Cryptography;
using namespace ABI::Windows::Security::Cryptography::Core;
using ABI::Windows::Storage::Streams::IBuffer;
using Windows::Storage::Streams::IBufferByteAccess;
namespace appx {
namespace {
// A null buffer reads as no bytes, as Windows' ICryptographicBufferInternal_Data does.
HRESULT Data(IBuffer* buffer, UINT32* length, byte** bytes) {
    *length = 0;
    *bytes = nullptr;
    if (!buffer) return S_OK;
    ComPtr<IBufferByteAccess> access;
    auto hr = buffer->QueryInterface(IID_PPV_ARGS(&access));
    if (SUCCEEDED(hr)) hr = buffer->get_Length(length);
    if (SUCCEEDED(hr)) hr = access->Buffer(bytes);
    return hr;
}
HRESULT Unimplemented(const char* member) {
    Log("UNIMPL", "Windows.Security.Cryptography.CryptographicBuffer.%s", member);
    return E_NOTIMPL;
}
class BufferStatics final : public Factory<ICryptographicBufferStatics> {
public:
    BufferStatics() : Factory(L"Windows.Security.Cryptography.CryptographicBuffer") {}
    HRESULT STDMETHODCALLTYPE Compare(IBuffer*, IBuffer*, boolean*) override { return Unimplemented("Compare"); }
    HRESULT STDMETHODCALLTYPE GenerateRandom(UINT32 length, IBuffer** buffer) override {
        if (!buffer) return E_POINTER;
        *buffer = nullptr;
        std::vector<byte> bytes(length);
        if (length && !BCRYPT_SUCCESS(BCryptGenRandom(nullptr, bytes.data(), length, BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
            return E_FAIL;
        return CopyToBuffer(bytes.data(), length, buffer);
    }
    HRESULT STDMETHODCALLTYPE GenerateRandomNumber(UINT32* value) override {
        if (!value) return E_POINTER;
        return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, reinterpret_cast<byte*>(value), sizeof(*value),
                                              BCRYPT_USE_SYSTEM_PREFERRED_RNG))
                   ? S_OK
                   : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE CreateFromByteArray(UINT32, BYTE*, IBuffer**) override {
        return Unimplemented("CreateFromByteArray");
    }
    HRESULT STDMETHODCALLTYPE CopyToByteArray(IBuffer*, UINT32*, BYTE**) override {
        return Unimplemented("CopyToByteArray");
    }
    HRESULT STDMETHODCALLTYPE DecodeFromHexString(HSTRING, IBuffer**) override {
        return Unimplemented("DecodeFromHexString");
    }
    HRESULT STDMETHODCALLTYPE EncodeToHexString(IBuffer*, HSTRING*) override {
        return Unimplemented("EncodeToHexString");
    }
    HRESULT STDMETHODCALLTYPE DecodeFromBase64String(HSTRING, IBuffer**) override {
        return Unimplemented("DecodeFromBase64String");
    }
    HRESULT STDMETHODCALLTYPE EncodeToBase64String(IBuffer* buffer, HSTRING* value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        UINT32 length = 0;
        byte* bytes = nullptr;
        auto hr = Data(buffer, &length, &bytes);
        if (FAILED(hr) || !length) return hr;
        static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::wstring text;
        text.reserve(4 * ((static_cast<size_t>(length) + 2) / 3));
        for (UINT32 i = 0; i < length; i += 3) {
            UINT32 group =
                bytes[i] << 16 | (i + 1 < length ? bytes[i + 1] << 8 : 0) | (i + 2 < length ? bytes[i + 2] : 0);
            text += alphabet[group >> 18 & 63];
            text += alphabet[group >> 12 & 63];
            text += i + 1 < length ? alphabet[group >> 6 & 63] : L'=';
            text += i + 2 < length ? alphabet[group & 63] : L'=';
        }
        return WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), value);
    }
    HRESULT STDMETHODCALLTYPE ConvertStringToBinary(HSTRING value, BinaryStringEncoding encoding,
                                                    IBuffer** buffer) override {
        if (!buffer) return E_POINTER;
        *buffer = nullptr;
        UINT32 length = 0;
        const wchar_t* text = WindowsGetStringRawBuffer(value, &length);
        switch (encoding) {
        case BinaryStringEncoding_Utf8: {
            // WinRT's empty string is null; Windows converts only non-null strings.
            if (!length) return CopyToBuffer(nullptr, 0, buffer);
            int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, static_cast<int>(length), nullptr, 0,
                                           nullptr, nullptr);
            if (!size) return E_INVALIDARG;
            std::string bytes(size, '\0');
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, static_cast<int>(length), bytes.data(), size,
                                nullptr, nullptr);
            return CopyToBuffer(bytes.data(), static_cast<UINT32>(size), buffer);
        }
        case BinaryStringEncoding_Utf16LE:
            return CopyToBuffer(text, length * 2, buffer);
        case BinaryStringEncoding_Utf16BE: {
            std::vector<byte> bytes(static_cast<size_t>(length) * 2);
            for (UINT32 i = 0; i < length; ++i) {
                bytes[2 * i] = static_cast<byte>(text[i] >> 8);
                bytes[2 * i + 1] = static_cast<byte>(text[i]);
            }
            return CopyToBuffer(bytes.data(), length * 2, buffer);
        }
        default:
            return E_NOTIMPL;
        }
    }
    HRESULT STDMETHODCALLTYPE ConvertBinaryToString(BinaryStringEncoding, IBuffer*, HSTRING*) override {
        return Unimplemented("ConvertBinaryToString");
    }
};
// HashAlgorithmNames and the BCrypt algorithm each opens, as in Windows' table.
struct HashAlgorithm {
    const wchar_t* name;
    const wchar_t* bcrypt;
};
constexpr HashAlgorithm algorithms[] = {{L"MD5", BCRYPT_MD5_ALGORITHM},
                                        {L"SHA1", BCRYPT_SHA1_ALGORITHM},
                                        {L"SHA256", BCRYPT_SHA256_ALGORITHM},
                                        {L"SHA384", BCRYPT_SHA384_ALGORITHM},
                                        {L"SHA512", BCRYPT_SHA512_ALGORITHM}};
HRESULT Name(const wchar_t* name, HSTRING* value) {
    if (!value) return E_POINTER;
    return WindowsCreateString(name, static_cast<UINT32>(wcslen(name)), value);
}
class NamesStatics final : public Factory<IHashAlgorithmNamesStatics> {
public:
    NamesStatics() : Factory(L"Windows.Security.Cryptography.Core.HashAlgorithmNames") {}
    HRESULT STDMETHODCALLTYPE get_Md5(HSTRING* value) override { return Name(algorithms[0].name, value); }
    HRESULT STDMETHODCALLTYPE get_Sha1(HSTRING* value) override { return Name(algorithms[1].name, value); }
    HRESULT STDMETHODCALLTYPE get_Sha256(HSTRING* value) override { return Name(algorithms[2].name, value); }
    HRESULT STDMETHODCALLTYPE get_Sha384(HSTRING* value) override { return Name(algorithms[3].name, value); }
    HRESULT STDMETHODCALLTYPE get_Sha512(HSTRING* value) override { return Name(algorithms[4].name, value); }
};
class HashProvider final : public RuntimeClass<RuntimeClassFlags<WinRt>, IHashAlgorithmProvider, FtmBase> {
    InspectableClass(L"Windows.Security.Cryptography.Core.HashAlgorithmProvider", BaseTrust);
    const HashAlgorithm& algorithm_;
    BCRYPT_ALG_HANDLE handle_ = nullptr;
    DWORD length_ = 0;

public:
    explicit HashProvider(const HashAlgorithm& algorithm) : algorithm_(algorithm) {}
    ~HashProvider() override {
        if (handle_) BCryptCloseAlgorithmProvider(handle_, 0);
    }
    HRESULT Open() {
        ULONG size = 0;
        NTSTATUS status = BCryptOpenAlgorithmProvider(&handle_, algorithm_.bcrypt, nullptr, 0);
        if (BCRYPT_SUCCESS(status))
            status = BCryptGetProperty(handle_, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&length_), sizeof(length_),
                                       &size, 0);
        return BCRYPT_SUCCESS(status) ? S_OK : HRESULT_FROM_NT(status);
    }
    HRESULT STDMETHODCALLTYPE get_AlgorithmName(HSTRING* value) override { return Name(algorithm_.name, value); }
    HRESULT STDMETHODCALLTYPE get_HashLength(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = length_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HashData(IBuffer* data, IBuffer** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        UINT32 length = 0;
        byte* bytes = nullptr;
        auto hr = Data(data, &length, &bytes);
        if (FAILED(hr)) return hr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        std::vector<byte> digest(length_);
        NTSTATUS status = BCryptCreateHash(handle_, &hash, nullptr, 0, nullptr, 0, 0);
        if (BCRYPT_SUCCESS(status)) status = BCryptHashData(hash, bytes, length, 0);
        if (BCRYPT_SUCCESS(status)) status = BCryptFinishHash(hash, digest.data(), length_, 0);
        if (hash) BCryptDestroyHash(hash);
        if (!BCRYPT_SUCCESS(status)) return HRESULT_FROM_NT(status);
        return CopyToBuffer(digest.data(), length_, value);
    }
    HRESULT STDMETHODCALLTYPE CreateHash(IHashComputation** value) override {
        if (value) *value = nullptr;
        Log("UNIMPL", "Windows.Security.Cryptography.Core.HashAlgorithmProvider.CreateHash");
        return E_NOTIMPL;
    }
};
class ProviderStatics final : public Factory<IHashAlgorithmProviderStatics> {
public:
    ProviderStatics() : Factory(L"Windows.Security.Cryptography.Core.HashAlgorithmProvider") {}
    HRESULT STDMETHODCALLTYPE OpenAlgorithm(HSTRING name, IHashAlgorithmProvider** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        const wchar_t* text = WindowsGetStringRawBuffer(name, nullptr);
        // An exact, case-sensitive match against the table; otherwise STATUS_NOT_FOUND.
        for (const auto& algorithm : algorithms) {
            if (wcscmp(algorithm.name, text) != 0) continue;
            auto provider = Make<HashProvider>(algorithm);
            if (!provider) return E_OUTOFMEMORY;
            auto hr = provider->Open();
            return FAILED(hr) ? hr : provider.CopyTo(value);
        }
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
};
template <class T> HRESULT Shared(IActivationFactory** value) {
    static auto factory = Make<T>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
const ShimClass bufferStatics{L"Windows.Security.Cryptography.CryptographicBuffer", Shared<BufferStatics>,
                              ShimPolicy::Replace, InIslands};
const ShimClass hashNames{L"Windows.Security.Cryptography.Core.HashAlgorithmNames", Shared<NamesStatics>,
                          ShimPolicy::Replace, InIslands};
const ShimClass hashProviders{L"Windows.Security.Cryptography.Core.HashAlgorithmProvider", Shared<ProviderStatics>,
                              ShimPolicy::Replace, InIslands};
}
}
