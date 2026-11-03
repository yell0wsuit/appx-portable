// Exercise the production host's message path using SDK event interfaces.
#include <windows.ui.core.h>
namespace window_size_fixture {
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Core;
class Observer final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IWindowSizeChangedEventHandler> {
public:
    int calls = 0;
    HRESULT result = E_FAIL;
    HRESULT STDMETHODCALLTYPE Invoke(IInspectable* sender, IWindowSizeChangedEventArgs* args) override {
        ++calls;
        ABI::Windows::Foundation::Size size{};
        ComPtr<IWindow> window;
        result = sender && args && SUCCEEDED(sender->QueryInterface(IID_PPV_ARGS(&window))) &&
                         SUCCEEDED(args->get_Size(&size)) && size.Width == 641 && size.Height == 479
                     ? S_OK
                     : E_FAIL;
        return result;
    }
};
BOOL CALLBACK FindHost(HWND window, LPARAM value) {
    wchar_t name[64]{};
    GetClassNameW(window, name, 64);
    if (!wcscmp(name, L"AppxPortableWindow")) {
        *reinterpret_cast<HWND*>(value) = window;
        return FALSE;
    }
    return TRUE;
}
HRESULT Drain() {
    MSG message{};
    for (int i = 0; i < 256; ++i) {
        if (!PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) return S_OK;
        if (message.message == WM_QUIT) {
            PostQuitMessage(static_cast<int>(message.wParam));
            return E_FAIL;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return E_FAIL;
}
HRESULT Send() {
    HWND host = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), FindHost, reinterpret_cast<LPARAM>(&host));
    if (!host) return E_FAIL;
    using FactoryCall = HRESULT(WINAPI*)(HSTRING, IActivationFactory**);
    auto getFactory =
        reinterpret_cast<FactoryCall>(GetProcAddress(GetModuleHandleW(L"appxportable.dll"), "ShimGetFactory"));
    if (!getFactory) return E_UNEXPECTED;
    ComPtr<IActivationFactory> factory;
    auto hr = getFactory(Wrappers::HStringReference(L"Windows.UI.Xaml.Window").Get(), &factory);
    ComPtr<IWindowStatics> statics;
    ComPtr<IWindow> window;
    if (SUCCEEDED(hr)) hr = factory.As(&statics);
    if (SUCCEEDED(hr)) hr = statics->get_Current(&window);
    auto observer = Make<Observer>();
    if (!observer) return E_OUTOFMEMORY;
    EventRegistrationToken token{};
    if (SUCCEEDED(hr)) hr = window->add_SizeChanged(observer.Get(), &token);
    if (FAILED(hr)) return hr;
    SendMessageW(host, WM_SIZE, SIZE_RESTORED, MAKELPARAM(641, 479));
    auto delivered = Drain();
    hr = window->remove_SizeChanged(token);
    if (FAILED(delivered)) return delivered;
    return FAILED(hr) ? hr : observer->calls == 1 ? observer->result : E_FAIL;
}
}
extern "C" HRESULT __cdecl SendWindowSize() { return window_size_fixture::Send(); }
extern "C" HRESULT __cdecl FlushWindowEvents() { return window_size_fixture::Drain(); }
// The view counts as active while it can be seen: minimizing deactivates it.
extern "C" HRESULT __cdecl SetHostMinimized(int minimized) {
    HWND host = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), window_size_fixture::FindHost, reinterpret_cast<LPARAM>(&host));
    if (!host) return E_FAIL;
    ShowWindow(host, minimized ? SW_MINIMIZE : SW_RESTORE);
    return window_size_fixture::Drain();
}
// A key message to the host window, as the user typing.
extern "C" HRESULT __cdecl SendHostKey(UINT message, WPARAM key) {
    HWND host = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), window_size_fixture::FindHost, reinterpret_cast<LPARAM>(&host));
    if (!host) return E_FAIL;
    const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
    SendMessageW(host, message, key, up ? 0xC0000001 : 1);
    return window_size_fixture::Drain();
}
extern "C" HRESULT __cdecl ValidateWindowSizeArgs(ABI::Windows::UI::Core::IWindowSizeChangedEventArgs* args) {
    if (!args) return E_POINTER;
    ABI::Windows::Foundation::Size size{};
    return SUCCEEDED(args->get_Size(&size)) && size.Width == 641 && size.Height == 479 ? S_OK : E_FAIL;
}
