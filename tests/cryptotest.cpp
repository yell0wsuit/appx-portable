// Behaviour from Windows' CryptoWinRT.dll (CCryptographicBufferStatics, CHashAlgorithmProvider).
#include <windows.h>
#include <roapi.h>
#include <windows.security.cryptography.h>
#include <windows.security.cryptography.core.h>
#include <robuffer.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <cstring>
#include <string>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Security::Cryptography;
using namespace ABI::Windows::Security::Cryptography::Core;
using ABI::Windows::Storage::Streams::IBuffer;
using GetFactory = HRESULT(WINAPI*)(HSTRING, IActivationFactory**);
static std::string Bytes(IBuffer* buffer) {
    UINT32 length = 0;
    ComPtr<Windows::Storage::Streams::IBufferByteAccess> access;
    byte* data = nullptr;
    if (!buffer || FAILED(buffer->get_Length(&length)) || FAILED(buffer->QueryInterface(IID_PPV_ARGS(&access))) ||
        FAILED(access->Buffer(&data)))
        return "<invalid>";
    return std::string(reinterpret_cast<char*>(data), length);
}
static std::wstring Text(HSTRING value) {
    UINT32 length = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(value, &length);
    return std::wstring(raw, length);
}
template <class T> static ComPtr<T> Statics(GetFactory get, const wchar_t* name) {
    ComPtr<IActivationFactory> factory;
    ComPtr<T> statics;
    if (SUCCEEDED(get(HStringReference(name).Get(), &factory))) factory.As(&statics);
    return statics;
}
static int Buffers(ICryptographicBufferStatics* crypto) {
    ComPtr<IBuffer> buffer;
    if (crypto->ConvertStringToBinary(HStringReference(L"a").Get(), BinaryStringEncoding_Utf8, nullptr) != E_POINTER)
        return 10;
    const wchar_t text[] = L"dđá \U0001F48E";
    if (FAILED(crypto->ConvertStringToBinary(HStringReference(text).Get(), BinaryStringEncoding_Utf8, &buffer)) ||
        Bytes(buffer.Get()) != "d\xc4\x91\xc3\xa1 \xf0\x9f\x92\x8e")
        return 11;
    if (FAILED(crypto->ConvertStringToBinary(HStringReference(L"Az").Get(), BinaryStringEncoding_Utf16LE, &buffer)) ||
        Bytes(buffer.Get()) != std::string("A\0z\0", 4))
        return 12;
    if (FAILED(crypto->ConvertStringToBinary(HStringReference(L"Az").Get(), BinaryStringEncoding_Utf16BE, &buffer)) ||
        Bytes(buffer.Get()) != std::string("\0A\0z", 4))
        return 13;
    // WinRT's empty string is a null HSTRING: an empty buffer in every encoding.
    for (auto encoding : {BinaryStringEncoding_Utf8, BinaryStringEncoding_Utf16LE, BinaryStringEncoding_Utf16BE})
        if (FAILED(crypto->ConvertStringToBinary(nullptr, encoding, &buffer)) || Bytes(buffer.Get()) != "") return 14;
    // UTF-8 conversion uses WC_ERR_INVALID_CHARS; an unknown encoding is not implemented.
    const wchar_t lone[] = {L'a', 0xd800, 0};
    if (crypto->ConvertStringToBinary(HStringReference(lone).Get(), BinaryStringEncoding_Utf8, &buffer) !=
            E_INVALIDARG ||
        buffer)
        return 15;
    if (crypto->ConvertStringToBinary(HStringReference(L"a").Get(), static_cast<BinaryStringEncoding>(3), &buffer) !=
        E_NOTIMPL)
        return 16;
    HString encoded;
    if (FAILED(crypto->ConvertStringToBinary(HStringReference(L"ÿþ-").Get(), BinaryStringEncoding_Utf16LE, &buffer)) ||
        FAILED(crypto->EncodeToBase64String(buffer.Get(), encoded.GetAddressOf())) ||
        Text(encoded.Get()) != L"/wD+AC0A")
        return 17;
    // A null buffer reads as no bytes: an empty string, not an error.
    if (FAILED(crypto->EncodeToBase64String(nullptr, encoded.GetAddressOf())) || encoded.Get() != nullptr) return 18;
    if (crypto->EncodeToBase64String(nullptr, nullptr) != E_POINTER) return 19;
    UINT32 random = 0;
    if (FAILED(crypto->GenerateRandom(16, &buffer)) || Bytes(buffer.Get()).size() != 16 ||
        FAILED(crypto->GenerateRandomNumber(&random)))
        return 20;
    return 0;
}
static int Hashes(IHashAlgorithmProviderStatics* providers, IHashAlgorithmNamesStatics* names,
                  ICryptographicBufferStatics* crypto) {
    HString sha1;
    if (FAILED(names->get_Sha1(sha1.GetAddressOf())) || Text(sha1.Get()) != L"SHA1") return 30;
    struct Algorithm {
        HRESULT (STDMETHODCALLTYPE IHashAlgorithmNamesStatics::*name)(HSTRING*);
        const wchar_t* text;
        UINT32 length;
    };
    for (auto algorithm : {Algorithm{&IHashAlgorithmNamesStatics::get_Md5, L"MD5", 16},
                           Algorithm{&IHashAlgorithmNamesStatics::get_Sha1, L"SHA1", 20},
                           Algorithm{&IHashAlgorithmNamesStatics::get_Sha256, L"SHA256", 32},
                           Algorithm{&IHashAlgorithmNamesStatics::get_Sha384, L"SHA384", 48},
                           Algorithm{&IHashAlgorithmNamesStatics::get_Sha512, L"SHA512", 64}}) {
        HString name;
        ComPtr<IHashAlgorithmProvider> provider;
        UINT32 length = 0;
        HString opened;
        if (FAILED((names->*algorithm.name)(name.GetAddressOf())) || Text(name.Get()) != algorithm.text ||
            FAILED(providers->OpenAlgorithm(name.Get(), &provider)) || FAILED(provider->get_HashLength(&length)) ||
            length != algorithm.length || FAILED(provider->get_AlgorithmName(opened.GetAddressOf())) ||
            Text(opened.Get()) != algorithm.text)
            return 31;
    }
    ComPtr<IHashAlgorithmProvider> provider;
    // Names match the table exactly; anything else is ERROR_NOT_FOUND.
    if (providers->OpenAlgorithm(HStringReference(L"sha1").Get(), &provider) != HRESULT_FROM_WIN32(ERROR_NOT_FOUND) ||
        provider)
        return 32;
    if (providers->OpenAlgorithm(nullptr, &provider) != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) return 33;
    if (FAILED(providers->OpenAlgorithm(sha1.Get(), &provider))) return 34;
    ComPtr<IBuffer> data, hash;
    if (FAILED(crypto->ConvertStringToBinary(HStringReference(L"abc").Get(), BinaryStringEncoding_Utf8, &data)) ||
        FAILED(provider->HashData(data.Get(), &hash)) ||
        Bytes(hash.Get()) != "\xa9\x99\x3e\x36\x47\x06\x81\x6a\xba\x3e\x25\x71\x78\x50\xc2\x6c\x9c\xd0\xd8\x9d")
        return 35;
    if (FAILED(provider->HashData(nullptr, &hash)) ||
        Bytes(hash.Get()) != "\xda\x39\xa3\xee\x5e\x6b\x4b\x0d\x32\x55\xbf\xef\x95\x60\x18\x90\xaf\xd8\x07\x09")
        return 36;
    return 0;
}
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"appxportable.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<GetFactory>(GetProcAddress(dll, "ShimGetFactory"));
    auto crypto = Statics<ICryptographicBufferStatics>(get, L"Windows.Security.Cryptography.CryptographicBuffer");
    auto providers =
        Statics<IHashAlgorithmProviderStatics>(get, L"Windows.Security.Cryptography.Core.HashAlgorithmProvider");
    auto names = Statics<IHashAlgorithmNamesStatics>(get, L"Windows.Security.Cryptography.Core.HashAlgorithmNames");
    if (!crypto || !providers || !names) {
        puts("FAIL crypto factories");
        return 2;
    }
    int failure = Buffers(crypto.Get());
    if (!failure) failure = Hashes(providers.Get(), names.Get(), crypto.Get());
    if (failure) {
        printf("FAIL crypto %d\n", failure);
        return failure;
    }
    puts("cryptotest: 0 failure(s)");
    return 0;
}
