#pragma once
#include <mfobjects.h>
namespace appx {
// Read-only WinRT request-body bridge, used only by Wine import interception.
HRESULT WINAPI CreateStreamOverRandomAccessStream(IUnknown* source, REFIID iid, void** result) noexcept;
// Wine fallback for the WinRT stream overload; successful native implementations
// and their errors are preserved.
HRESULT WINAPI CreateMFByteStreamOnStreamEx(IUnknown* source, IMFByteStream** result) noexcept;
}
