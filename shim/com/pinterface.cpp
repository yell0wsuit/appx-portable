#include "pinterface.h"
#include <bcrypt.h>
#include <cstdio>
namespace appx {
std::string GuidSignature(const GUID& g) {
    char text[40];
    snprintf(text, sizeof(text), "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", g.Data1, g.Data2, g.Data3,
             g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return text;
}
std::string AsyncOperationOfClass(const char* runtimeClass, const GUID& defaultInterface) {
    return std::string("pinterface({9fc2b0bb-e446-44e2-aa61-9cab8f636af2};rc(") + runtimeClass + ";" +
           GuidSignature(defaultInterface) + "))";
}
GUID ParameterizedIid(const std::string& signature) {
    // The WinRT parameterized-type namespace, 11f47ad5-7b73-42c0-abae-878b1e16adee, in network byte order.
    static constexpr BYTE space[16] = {0x11, 0xf4, 0x7a, 0xd5, 0x7b, 0x73, 0x42, 0xc0,
                                       0xab, 0xae, 0x87, 0x8b, 0x1e, 0x16, 0xad, 0xee};
    BYTE hash[20]{};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE handle = nullptr;
    const bool hashed =
        BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0)) &&
        BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &handle, nullptr, 0, nullptr, 0, 0)) &&
        BCRYPT_SUCCESS(BCryptHashData(handle, const_cast<PUCHAR>(space), sizeof(space), 0)) &&
        BCRYPT_SUCCESS(BCryptHashData(handle, reinterpret_cast<PUCHAR>(const_cast<char*>(signature.data())),
                                      static_cast<ULONG>(signature.size()), 0)) &&
        BCRYPT_SUCCESS(BCryptFinishHash(handle, hash, sizeof(hash), 0));
    if (handle) BCryptDestroyHash(handle);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!hashed) return GUID{};
    hash[6] = static_cast<BYTE>((hash[6] & 0x0f) | 0x50);  // version 5
    hash[8] = static_cast<BYTE>((hash[8] & 0x3f) | 0x80);  // RFC 4122 variant
    GUID result{};
    result.Data1 = static_cast<unsigned long>(hash[0]) << 24 | hash[1] << 16 | hash[2] << 8 | hash[3];
    result.Data2 = static_cast<unsigned short>(hash[4] << 8 | hash[5]);
    result.Data3 = static_cast<unsigned short>(hash[6] << 8 | hash[7]);
    memcpy(result.Data4, hash + 8, 8);
    return result;
}
}
