#include <windows.h>
#undef GetCurrentTime
#include <roapi.h>
#include <windows.system.h>
#include <windows.foundation.metadata.h>
#include <windows.applicationmodel.core.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.core.h>
#include <windows.devices.input.h>
#include <wrl.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <thread>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Core;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
static int failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto dll = LoadLibraryW(L"appxportable.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.System.MemoryManager").Get(), &factory)));
    ComPtr<ABI::Windows::System::IMemoryManagerStatics> memory;
    CHECK(SUCCEEDED(factory.As(&memory)));
    if (!memory) return 1;
    UINT64 usage{}, limit{};
    CHECK(SUCCEEDED(memory->get_AppMemoryUsage(&usage)) && usage > 0);
    CHECK(SUCCEEDED(memory->get_AppMemoryUsageLimit(&limit)) && limit > 0);
    CHECK(memory->get_AppMemoryUsageLimit(nullptr) == E_POINTER);
    // Answered by Windows, or by the shim where Wine stubs it with E_NOTIMPL.
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.Foundation.Metadata.ApiInformation").Get(), &factory)));
    ComPtr<ABI::Windows::Foundation::Metadata::IApiInformationStatics> api;
    CHECK(SUCCEEDED(factory.As(&api)));
    if (!api) return 1;
    boolean present = true;
    CHECK(SUCCEEDED(api->IsTypePresent(HStringReference(L"Appx.Portable.NoSuchType").Get(), &present)) && !present);
    CHECK(SUCCEEDED(api->IsTypePresent(HStringReference(L"Windows.Phone.UI.Input.HardwareButtons").Get(), &present)));
    CHECK(SUCCEEDED(api->IsMethodPresent(HStringReference(L"Windows.System.MemoryManager").Get(),
                                         HStringReference(L"NoSuchMethod").Get(), &present)) &&
          !present);
    CHECK(api->IsTypePresent(HStringReference(L"Windows.System.MemoryManager").Get(), nullptr) == E_POINTER);
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.Devices.Input.TouchCapabilities").Get(), &factory)));
    ComPtr<IInspectable> touchObject;
    CHECK(SUCCEEDED(factory->ActivateInstance(&touchObject)));
    ComPtr<ABI::Windows::Devices::Input::ITouchCapabilities> touch;
    CHECK(touchObject && SUCCEEDED(touchObject.As(&touch)));
    INT32 touchPresent = -1;
    UINT32 contacts{};
    CHECK(touch && SUCCEEDED(touch->get_TouchPresent(&touchPresent)) && SUCCEEDED(touch->get_Contacts(&contacts)) &&
          (touchPresent != 0) == (contacts > 0));
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.Devices.Input.KeyboardCapabilities").Get(), &factory)));
    ComPtr<IInspectable> keyboardObject;
    ComPtr<ABI::Windows::Devices::Input::IKeyboardCapabilities> keyboard;
    INT32 keyboardPresent{};
    CHECK(SUCCEEDED(factory->ActivateInstance(&keyboardObject)) && SUCCEEDED(keyboardObject.As(&keyboard)) &&
          SUCCEEDED(keyboard->get_KeyboardPresent(&keyboardPresent)) && keyboardPresent);
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.Devices.Input.MouseCapabilities").Get(), &factory)));
    ComPtr<IInspectable> mouseObject;
    ComPtr<ABI::Windows::Devices::Input::IMouseCapabilities> mouse;
    INT32 mousePresent = -1;
    UINT32 buttons{};
    CHECK(SUCCEEDED(factory->ActivateInstance(&mouseObject)) && SUCCEEDED(mouseObject.As(&mouse)) &&
          SUCCEEDED(mouse->get_MousePresent(&mousePresent)) && SUCCEEDED(mouse->get_NumberOfButtons(&buttons)) &&
          (mousePresent == 0 || buttons > 0));
    HString capabilitiesName;
    CHECK(SUCCEEDED(mouseObject->GetRuntimeClassName(capabilitiesName.GetAddressOf())) &&
          wcscmp(capabilitiesName.GetRawBuffer(nullptr), L"Windows.Devices.Input.MouseCapabilities") == 0);
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.ApplicationModel.Core.CoreApplication").Get(), &factory)));
    ComPtr<ABI::Windows::ApplicationModel::Core::ICoreApplication> app;
    CHECK(SUCCEEDED(factory.As(&app)));
    ComPtr<ABI::Windows::ApplicationModel::Core::ICoreApplicationView> view;
    CHECK(SUCCEEDED(app->GetCurrentView(&view)));
    ComPtr<ABI::Windows::ApplicationModel::Core::ICoreApplicationView3> view3;
    CHECK(SUCCEEDED(view.As(&view3)));
    ComPtr<ABI::Windows::ApplicationModel::Core::ICoreApplicationViewTitleBar> title;
    CHECK(SUCCEEDED(view3->get_TitleBar(&title)));
    boolean extended{};
    CHECK(SUCCEEDED(title->put_ExtendViewIntoTitleBar(true)));
    CHECK(SUCCEEDED(title->get_ExtendViewIntoTitleBar(&extended)) && extended);
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Controls.Frame").Get(), &factory)));
    ComPtr<IInspectable> frame;
    CHECK(SUCCEEDED(factory->ActivateInstance(&frame)));
    ComPtr<IFrameworkElement> element;
    CHECK(SUCCEEDED(frame.As(&element)));
    ComPtr<IInspectable> named;
    CHECK(SUCCEEDED(element->FindName(HStringReference(L"swapChainPanel").Get(), &named)));
    ComPtr<ISwapChainPanel> panel;
    CHECK(SUCCEEDED(named.As(&panel)));
    ComPtr<ICoreInputSourceBase> input;
    CHECK(SUCCEEDED(panel->CreateCoreIndependentInputSource(CoreInputDeviceTypes_Mouse, &input)));
    if (!input) return 1;
    ComPtr<ICoreDispatcher> dispatcher;
    CHECK(SUCCEEDED(input->get_Dispatcher(&dispatcher)));
    boolean access{};
    CHECK(SUCCEEDED(dispatcher->get_HasThreadAccess(&access)) && access);
    unsigned delivered{};
    const DWORD thread = GetCurrentThreadId();
    ComPtr<ICorePointerInputSource> pointer;
    CHECK(SUCCEEDED(input.As(&pointer)));
    auto handler = Callback<ABI::Windows::Foundation::ITypedEventHandler<IInspectable*, PointerEventArgs*>>(
        [&](IInspectable*, IPointerEventArgs* args) -> HRESULT {
            CHECK(GetCurrentThreadId() == thread);
            ComPtr<ABI::Windows::UI::Input::IPointerPoint> point;
            CHECK(SUCCEEDED(args->get_CurrentPoint(&point)));
            ComPtr<__FIVector_1_Windows__CUI__CInput__CPointerPoint> history;
            CHECK(SUCCEEDED(args->GetIntermediatePoints(&history)));
            UINT32 count{};
            CHECK(history && SUCCEEDED(history->get_Size(&count)) && count == 1);
            ComPtr<ABI::Windows::UI::Input::IPointerPoint> latest;
            CHECK(history && SUCCEEDED(history->GetAt(0, &latest)));
            ABI::Windows::Foundation::Point current{}, previous{};
            CHECK(SUCCEEDED(point->get_Position(&current)));
            CHECK(latest && SUCCEEDED(latest->get_Position(&previous)) && current.X == previous.X &&
                  current.Y == previous.Y);
            ComPtr<ABI::Windows::Devices::Input::IPointerDevice> device;
            CHECK(SUCCEEDED(point->get_PointerDevice(&device)));
            ABI::Windows::Devices::Input::PointerDeviceType type{};
            CHECK(SUCCEEDED(device->get_PointerDeviceType(&type)) &&
                  type == ABI::Windows::Devices::Input::PointerDeviceType_Mouse);
            ++delivered;
            return S_OK;
        });
    EventRegistrationToken token{};
    CHECK(SUCCEEDED(pointer->add_PointerPressed(handler.Get(), &token)));
    HWND queue = FindWindowExW(HWND_MESSAGE, nullptr, L"AppxPortableIndependentInput", nullptr);
    CHECK(queue && PostMessageW(queue, WM_APP + WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(50, 60)));
    CHECK(SUCCEEDED(dispatcher->ProcessEvents(CoreProcessEventsOption_ProcessAllIfPresent)));
    CHECK(delivered == 1);
    CHECK(SUCCEEDED(pointer->remove_PointerPressed(token)));
    bool ran{};
    auto work = Callback<IDispatchedHandler>([&]() -> HRESULT {
        ran = GetCurrentThreadId() == thread;
        return S_OK;
    });
    ComPtr<ABI::Windows::Foundation::IAsyncAction> action;
    CHECK(SUCCEEDED(dispatcher->RunAsync(CoreDispatcherPriority_Normal, work.Get(), &action)));
    CHECK(!ran);
    CHECK(SUCCEEDED(dispatcher->ProcessEvents(CoreProcessEventsOption_ProcessAllIfPresent)) && ran);
    // With nothing queued, ProcessOneAndAllPending waits for the next event
    // rather than returning at once: a few calls, not a busy loop.
    ran = false;
    std::thread later([&] {
        Sleep(50);
        ComPtr<ABI::Windows::Foundation::IAsyncAction> queued;
        CHECK(SUCCEEDED(dispatcher->RunAsync(CoreDispatcherPriority_Normal, work.Get(), &queued)));
    });
    unsigned calls{};
    while (!ran && calls < 1000 &&
           SUCCEEDED(dispatcher->ProcessEvents(CoreProcessEventsOption_ProcessOneAndAllPending)))
        ++calls;
    later.join();
    CHECK(ran && calls < 10);
    printf("win10test: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
