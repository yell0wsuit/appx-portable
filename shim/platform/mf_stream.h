#pragma once
#include <mfobjects.h>
namespace appx {
// Wine fallback for the WinRT stream overload; successful native implementations
// and their errors are preserved.
HRESULT WINAPI CreateMFByteStreamOnStreamEx(IUnknown* source, IMFByteStream** result) noexcept;
}
