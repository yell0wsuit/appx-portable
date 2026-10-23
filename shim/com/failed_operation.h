#pragma once
#include <windows.foundation.h>
namespace appx {
// An IAsyncOperation<T> that has already failed with `error`, known only by
// the instance's IID (see ParameterizedIid), so stand-ins for libraries with
// no SDK header can return one. Every IAsyncOperation<T> shares one vtable
// shape; `resultSize` is sizeof(T) for GetResults (4 for runtime classes,
// interfaces, HSTRING and Int32 on x86). Completed handlers run as soon as
// they are assigned, with AsyncStatus::Error.
HRESULT MakeFailedOperation(const GUID& iid, HRESULT error, size_t resultSize, IInspectable** value);
// The same for IAsyncAction.
HRESULT MakeFailedAction(HRESULT error, ABI::Windows::Foundation::IAsyncAction** value);
}
