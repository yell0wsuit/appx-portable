// The game tears down its renderer on Activated(Deactivated) and reloads on the
// next activation. Focus moving to another window must not deactivate a view
// that is still visible; minimizing and restoring must.
#include <windows.h>
#include <roapi.h>
#undef GetCurrentTime
#include <windows.ui.core.h>
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.media.h>
#include <windows.ui.xaml.input.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <functional>
#include <vector>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Core;
using namespace ABI::Windows::UI::Xaml;
static int failures = 0;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)
static std::vector<int> states; // 1 = active, 0 = deactivated, for CoreWindow
static std::vector<int> xamlStates;
static std::vector<int> visibility;
static std::vector<std::pair<float, float>> sizes; // CoreWindow SizeChanged
static ComPtr<ICoreDispatcher> dispatcher;
static HWND host;
static std::vector<int> keys;
static bool initializing = true, rootLoaded = false, firstFrame = false;
// Runs `step` after the UI queue has drained the work posted before it.
static void Then(const std::function<void()>& step) {
    auto work = Callback<IDispatchedHandler>([step]() -> HRESULT {
        step();
        return S_OK;
    });
    ComPtr<ABI::Windows::Foundation::IAsyncAction> action;
    CHECK(SUCCEEDED(dispatcher->RunAsync(CoreDispatcherPriority_Normal, work.Get(), &action)));
}
int main(int argc, char**) {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"appxportable.dll");
    CHECK(dll);
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    ComPtr<IApplicationStatics> statics;
    ComPtr<IApplicationFactory> compose;
    ComPtr<IApplication> app;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Application").Get(), &factory)));
    if (!factory || FAILED(factory.As(&statics)) || FAILED(factory.As(&compose))) return 1;
    auto callback = Callback<IApplicationInitializationCallback>([&](IApplicationInitializationCallbackParams*) {
        CHECK(SUCCEEDED(compose->CreateInstance(nullptr, nullptr, &app)));
        ComPtr<IActivationFactory> windowFactory;
        ComPtr<IWindowStatics> windows;
        ComPtr<IWindow> window;
        ComPtr<ICoreWindow> core;
        if (FAILED(get(HStringReference(L"Windows.UI.Xaml.Window").Get(), &windowFactory)) ||
            FAILED(windowFactory.As(&windows)) || FAILED(windows->get_Current(&window)) ||
            FAILED(window->get_CoreWindow(&core)) || FAILED(window->get_Dispatcher(&dispatcher))) {
            CHECK(false);
            PostQuitMessage(0);
            return S_OK;
        }
        EventRegistrationToken token{};
        // The initial compositor pass completes after startup returns, before
        // visibility handlers can access the objects built by Rendering.
        ComPtr<IActivationFactory> pageActivation, compositionActivation;
        ComPtr<ABI::Windows::UI::Xaml::Controls::IPageFactory> pages;
        ComPtr<ABI::Windows::UI::Xaml::Controls::IPage> page;
        ComPtr<IFrameworkElement> element;
        ComPtr<IUIElement> ui;
        CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Controls.Page").Get(), &pageActivation)));
        CHECK(pageActivation && SUCCEEDED(pageActivation.As(&pages)));
        CHECK(pages && SUCCEEDED(pages->CreateInstance(nullptr, nullptr, &page)));
        CHECK(page && SUCCEEDED(page.As(&element)) && SUCCEEDED(page.As(&ui)));
        auto loaded = Callback<IRoutedEventHandler>([](IInspectable*, IRoutedEventArgs*) -> HRESULT {
            CHECK(!initializing);
            rootLoaded = true;
            return S_OK;
        });
        CHECK(element && SUCCEEDED(element->add_Loaded(loaded.Get(), &token)));
        CHECK(SUCCEEDED(window->put_Content(ui.Get())));
        ComPtr<ABI::Windows::UI::Xaml::Media::ICompositionTargetStatics> composition;
        CHECK(
            SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Media.CompositionTarget").Get(), &compositionActivation)));
        CHECK(compositionActivation && SUCCEEDED(compositionActivation.As(&composition)));
        auto rendering = Callback<ABI::Windows::Foundation::IEventHandler<IInspectable*>>(
            [](IInspectable*, IInspectable*) -> HRESULT {
                CHECK(!initializing && rootLoaded);
                firstFrame = true;
                return S_OK;
            });
        CHECK(composition && SUCCEEDED(composition->add_Rendering(rendering.Get(), &token)));
        auto activated = Callback<ABI::Windows::Foundation::ITypedEventHandler<CoreWindow*, WindowActivatedEventArgs*>>(
            [](ICoreWindow*, IWindowActivatedEventArgs* args) -> HRESULT {
                CoreWindowActivationState state{};
                args->get_WindowActivationState(&state);
                states.push_back(state == CoreWindowActivationState_Deactivated ? 0 : 1);
                return S_OK;
            });
        CHECK(SUCCEEDED(core->add_Activated(activated.Get(), &token)));
        auto visible = Callback<ABI::Windows::Foundation::ITypedEventHandler<CoreWindow*, VisibilityChangedEventArgs*>>(
            [](ICoreWindow* core, IVisibilityChangedEventArgs* args) -> HRESULT {
                boolean value{}, current{};
                args->get_Visible(&value);
                CHECK(firstFrame);
                core->get_Visible(&current);
                CHECK(value == current);
                visibility.push_back(value ? 1 : 0);
                return S_OK;
            });
        CHECK(SUCCEEDED(core->add_VisibilityChanged(visible.Get(), &token)));
        auto resized = Callback<ABI::Windows::Foundation::ITypedEventHandler<CoreWindow*, WindowSizeChangedEventArgs*>>(
            [](ICoreWindow*, IWindowSizeChangedEventArgs* args) -> HRESULT {
                ABI::Windows::Foundation::Size size{};
                args->get_Size(&size);
                sizes.emplace_back(size.Width, size.Height);
                return S_OK;
            });
        CHECK(SUCCEEDED(core->add_SizeChanged(resized.Get(), &token)));
        auto keyDown = Callback<ABI::Windows::Foundation::ITypedEventHandler<CoreWindow*, KeyEventArgs*>>(
            [](ICoreWindow*, IKeyEventArgs* args) -> HRESULT {
                ABI::Windows::System::VirtualKey key{};
                args->get_VirtualKey(&key);
                keys.push_back(static_cast<int>(key));
                return S_OK;
            });
        CHECK(SUCCEEDED(core->add_KeyDown(keyDown.Get(), &token)));
        auto keyUp = Callback<ABI::Windows::Foundation::ITypedEventHandler<CoreWindow*, KeyEventArgs*>>(
            [](ICoreWindow*, IKeyEventArgs* args) -> HRESULT {
                ABI::Windows::System::VirtualKey key{};
                args->get_VirtualKey(&key);
                keys.push_back(-static_cast<int>(key));
                return S_OK;
            });
        CHECK(SUCCEEDED(core->add_KeyUp(keyUp.Get(), &token)));
        auto xamlActivated =
            Callback<IWindowActivatedEventHandler>([](IInspectable*, IWindowActivatedEventArgs* args) -> HRESULT {
                CoreWindowActivationState state{};
                args->get_WindowActivationState(&state);
                xamlStates.push_back(state == CoreWindowActivationState_Deactivated ? 0 : 1);
                return S_OK;
            });
        CHECK(SUCCEEDED(window->add_Activated(xamlActivated.Get(), &token)));
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND hwnd, LPARAM) -> BOOL {
                wchar_t name[64]{};
                GetClassNameW(hwnd, name, 64);
                if (wcscmp(name, L"AppxPortableWindow") != 0) return TRUE;
                host = hwnd;
                return FALSE;
            },
            0);
        CHECK(host != nullptr);
        if (argc > 1) ShowWindow(host, SW_SHOWMAXIMIZED);
        CHECK(SUCCEEDED(window->Activate())); // Shows the window.
        // Native XAML pages still receive keyboard events from CoreWindow.
        SendMessageW(host, WM_KEYDOWN, VK_SPACE, 1);
        SendMessageW(host, WM_KEYUP, VK_SPACE, static_cast<LPARAM>(0xC0000001u));
        CHECK((keys == std::vector<int>{VK_SPACE, -VK_SPACE}));
        // XAML keys originate at the graphics panel and bubble to the page.
        // Verify arguments, removal, handled routing and detached content.
        ComPtr<IInspectable> panel;
        ComPtr<IUIElement> panelUi;
        CHECK(SUCCEEDED(element->FindName(HStringReference(L"SwapChainPanel").Get(), &panel)));
        CHECK(panel && SUCCEEDED(panel.As(&panelUi)));
        std::vector<int> xamlKeys;
        bool handleAtPanel = false;
        auto onPanelKey =
            Callback<Input::IKeyEventHandler>([&](IInspectable* sender, Input::IKeyRoutedEventArgs* args) -> HRESULT {
                ComPtr<IUnknown> senderId, panelId;
                sender->QueryInterface(IID_PPV_ARGS(&senderId));
                panel.As(&panelId);
                CHECK(senderId == panelId);
                ABI::Windows::System::VirtualKey key{};
                CorePhysicalKeyStatus status{};
                CHECK(SUCCEEDED(args->get_Key(&key)) && key == ABI::Windows::System::VirtualKey_Space);
                CHECK(SUCCEEDED(args->get_KeyStatus(&status)));
                CHECK(status.ScanCode == 0x39 && status.RepeatCount == 1);
                CHECK(status.WasKeyDown == status.IsKeyReleased);
                ComPtr<IRoutedEventArgs> routed;
                ComPtr<IInspectable> source;
                CHECK(SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&routed))));
                CHECK(routed && SUCCEEDED(routed->get_OriginalSource(&source)));
                ComPtr<IUnknown> sourceId;
                if (source) source.As(&sourceId);
                CHECK(sourceId == panelId);
                ComPtr<Input::IKeyRoutedEventArgs2> original;
                CHECK(SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&original))));
                CHECK(original && SUCCEEDED(original->get_OriginalKey(&key)) &&
                      key == ABI::Windows::System::VirtualKey_Space);
                boolean handled = true;
                CHECK(SUCCEEDED(args->get_Handled(&handled)) && !handled);
                xamlKeys.push_back(status.IsKeyReleased ? -1 : 1);
                return args->put_Handled(handleAtPanel);
            });
        auto onPageKey =
            Callback<Input::IKeyEventHandler>([&](IInspectable*, Input::IKeyRoutedEventArgs* args) -> HRESULT {
                CorePhysicalKeyStatus status{};
                args->get_KeyStatus(&status);
                xamlKeys.push_back(status.IsKeyReleased ? -2 : 2);
                return S_OK;
            });
        EventRegistrationToken panelDown{}, panelUp{}, pageDown{}, pageUp{};
        CHECK(panelUi && SUCCEEDED(panelUi->add_KeyDown(onPanelKey.Get(), &panelDown)));
        CHECK(panelUi && SUCCEEDED(panelUi->add_KeyUp(onPanelKey.Get(), &panelUp)));
        CHECK(SUCCEEDED(ui->add_KeyDown(onPageKey.Get(), &pageDown)));
        CHECK(SUCCEEDED(ui->add_KeyUp(onPageKey.Get(), &pageUp)));
        auto sendKeys = [&](bool system) {
            SendMessageW(host, system ? WM_SYSKEYDOWN : WM_KEYDOWN, VK_SPACE, 0x00390001);
            SendMessageW(host, system ? WM_SYSKEYUP : WM_KEYUP, VK_SPACE, static_cast<LPARAM>(0xC0390001u));
        };
        sendKeys(false);
        CHECK((xamlKeys == std::vector<int>{1, 2, -1, -2}));
        xamlKeys.clear();
        handleAtPanel = true;
        EventRegistrationToken laterDown{}, laterUp{};
        // Once handled, later ordinary handlers on the same element are skipped too.
        CHECK(panelUi && SUCCEEDED(panelUi->add_KeyDown(onPageKey.Get(), &laterDown)));
        CHECK(panelUi && SUCCEEDED(panelUi->add_KeyUp(onPageKey.Get(), &laterUp)));
        sendKeys(true);
        CHECK((xamlKeys == std::vector<int>{1, -1}));
        CHECK(panelUi && SUCCEEDED(panelUi->remove_KeyDown(laterDown)));
        CHECK(panelUi && SUCCEEDED(panelUi->remove_KeyUp(laterUp)));
        xamlKeys.clear();
        CHECK(SUCCEEDED(window->put_Content(nullptr)));
        sendKeys(false);
        CHECK(xamlKeys.empty());
        CHECK(SUCCEEDED(window->put_Content(ui.Get())));
        CHECK(panelUi && SUCCEEDED(panelUi->remove_KeyDown(panelDown)));
        CHECK(panelUi && SUCCEEDED(panelUi->remove_KeyUp(panelUp)));
        sendKeys(false);
        CHECK((xamlKeys == std::vector<int>{2, -2}));
        xamlKeys.clear();
        CHECK(SUCCEEDED(ui->remove_KeyDown(pageDown)));
        CHECK(SUCCEEDED(ui->remove_KeyUp(pageUp)));
        sendKeys(false);
        CHECK(xamlKeys.empty());
        Then([] {
            CHECK((states == std::vector<int>{1}));
            CHECK((visibility == std::vector<int>{1}));
            // Focus moves to another window; this one stays visible.
            SendMessageW(host, WM_ACTIVATE, WA_INACTIVE, 0);
            Then([] {
                CHECK((states == std::vector<int>{1}));
                SendMessageW(host, WM_ACTIVATE, WA_CLICKACTIVE, 0); // Back again: no second activation.
                ShowWindow(host, SW_MINIMIZE);
                Then([] {
                    CHECK((states == std::vector<int>{1, 0}));
                    CHECK((visibility == std::vector<int>{1, 0}));
                    ShowWindow(host, SW_RESTORE);
                    Then([] {
                        CHECK((states == std::vector<int>{1, 0, 1}));
                        CHECK((visibility == std::vector<int>{1, 0, 1}));
                        CHECK(xamlStates == states);
                        // Dragging the frame: the game rebuilds its renderer (and shows
                        // Loading) on every size change, so it hears only the final size.
                        sizes.clear();
                        SendMessageW(host, WM_ENTERSIZEMOVE, 0, 0);
                        for (int width : {700, 800, 900})
                            SetWindowPos(host, nullptr, 0, 0, width, 600, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                        Then([] {
                            CHECK(sizes.empty());
                            SendMessageW(host, WM_EXITSIZEMOVE, 0, 0);
                            Then([] {
                                RECT client{};
                                GetClientRect(host, &client);
                                CHECK(sizes.size() == 1);
                                CHECK(!sizes.empty() && sizes.back().first == static_cast<float>(client.right) &&
                                      sizes.back().second == static_cast<float>(client.bottom));
                                // A move without resizing changes nothing.
                                sizes.clear();
                                SendMessageW(host, WM_ENTERSIZEMOVE, 0, 0);
                                SendMessageW(host, WM_EXITSIZEMOVE, 0, 0);
                                Then([] {
                                    CHECK(sizes.empty());
                                    PostQuitMessage(0);
                                });
                            });
                        });
                    });
                });
            });
        });
        CHECK(!firstFrame && !rootLoaded);
        initializing = false;
        return S_OK;
    });
    CHECK(SUCCEEDED(statics->Start(callback.Get())));
    CHECK(firstFrame && rootLoaded);
    CHECK(states.size() == 3);
    printf("focustest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
