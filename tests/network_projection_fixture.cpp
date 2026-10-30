// Test-only ABI caller: invokes a projected handler through the SDK interface,
// independently of the generator's delegate signature and IID.
#include <windows.networking.connectivity.h>
#include <wrl.h>
extern "C" HRESULT __cdecl InvokeNetworkChanged(IUnknown* handler, IInspectable* sender) {
    if (!handler) return E_POINTER;
    Microsoft::WRL::ComPtr<ABI::Windows::Networking::Connectivity::INetworkStatusChangedEventHandler> callback;
    auto hr = handler->QueryInterface(IID_PPV_ARGS(&callback));
    return FAILED(hr) ? hr : callback->Invoke(sender);
}
