// Included after window_size_projection_fixture.h in the Page native fixture.
#pragma once
// The deprecated Windows 8 getter is independently exercised through the SDK ABI.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#else
#pragma warning(disable : 4996)
#endif
#include <windows.ui.viewmanagement.h>
extern "C" HRESULT __cdecl SetApplicationViewOrientation(INT32 portrait) {
    HWND host = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), window_size_fixture::FindHost, reinterpret_cast<LPARAM>(&host));
    if (!host) return E_UNEXPECTED;
    if (!SetWindowPos(host, nullptr, 0, 0, portrait ? 479 : 641, portrait ? 641 : 479,
                      SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER))
        return HRESULT_FROM_WIN32(GetLastError());
    return window_size_fixture::Drain();
}
extern "C" HRESULT __cdecl CheckApplicationViewState(INT32 expected) {
    using namespace ABI::Windows::UI::ViewManagement;
    using GetFactory = HRESULT(WINAPI*)(HSTRING, IActivationFactory**);
    auto get = reinterpret_cast<GetFactory>(GetProcAddress(GetModuleHandleW(L"appxportable.dll"), "ShimGetFactory"));
    if (!get) return E_UNEXPECTED;
    ComPtr<IActivationFactory> factory;
    auto hr = get(Wrappers::HStringReference(L"Windows.UI.ViewManagement.ApplicationView").Get(), &factory);
    ComPtr<IApplicationViewStatics> statics;
    if (SUCCEEDED(hr)) hr = factory.As(&statics);
    if (FAILED(hr)) return hr;
    if (statics->get_Value(nullptr) != E_POINTER) return E_FAIL;
    ApplicationViewState value{};
    hr = statics->get_Value(&value);
    return FAILED(hr) ? hr : static_cast<INT32>(value) == expected ? S_OK : E_FAIL;
}
