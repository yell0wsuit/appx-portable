// The offline Facebook bridge must activate without a packaged CLR host and
// complete requests as failures, without leaving the game waiting for a callback.
#include <windows.h>
#include <roapi.h>
#include "stubs/facebook_abi.h"
#include <wrl.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <cwchar>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace appx::facebook;
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
    if (!get) return 1;
    ComPtr<IActivationFactory> factory;
    CHECK(SUCCEEDED(get(HStringReference(L"FacebookRuntimeComponent.FacebookHelper").Get(), &factory)));
    if (!factory) return 1;
    ComPtr<IFacebookHelperStatic> statics;
    CHECK(SUCCEEDED(factory.As(&statics)));
    ComPtr<IInspectable> instance;
    CHECK(factory->ActivateInstance(nullptr) == E_POINTER);
    CHECK(SUCCEEDED(factory->ActivateInstance(&instance)));
    ComPtr<IFacebookHelperClass> helper;
    CHECK(instance && SUCCEEDED(instance.As(&helper)));
    if (!statics || !helper) return 1;
    boolean loggedIn = true;
    CHECK(SUCCEEDED(helper->IsLoggedIn(&loggedIn)) && !loggedIn);
    ComPtr<IActivationFactory> browserActivation, canvasActivation;
    CHECK(SUCCEEDED(get(HStringReference(L"FacebookRuntimeComponent.FacebookWebView").Get(), &browserActivation)));
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Controls.Canvas").Get(), &canvasActivation)));
    ComPtr<IInspectable> canvas;
    ComPtr<ABI::Windows::UI::Xaml::Controls::IPanel> panel;
    CHECK(canvasActivation && SUCCEEDED(canvasActivation->ActivateInstance(&canvas)));
    CHECK(canvas && SUCCEEDED(canvas.As(&panel)));
    ComPtr<IFacebookWebViewStatic> browserStatics;
    ComPtr<IFacebookWebViewFactory> browsers;
    CHECK(browserActivation && SUCCEEDED(browserActivation.As(&browserStatics)) &&
          SUCCEEDED(browserActivation.As(&browsers)));
    if (!browserStatics || !browsers) return 1;
    boolean accepted = false;
    HString errorMessage;
    CHECK(SUCCEEDED(browserStatics->SetGamePanel(panel.Get(), errorMessage.GetAddressOf(), &accepted)) && accepted);
    CHECK(errorMessage.Length() == 0);
    CHECK(SUCCEEDED(browserStatics->SetGamePanel(nullptr, errorMessage.GetAddressOf(), &accepted)) && !accepted);
    CHECK(errorMessage.Length() != 0);
    ComPtr<IFacebookWebViewClass> browser;
    CHECK(browsers->CreateFacebookWebView(nullptr, &browser) == E_INVALIDARG && !browser);
    CHECK(SUCCEEDED(browsers->CreateFacebookWebView(helper.Get(), &browser)) && browser);
    ComPtr<ABI::Windows::UI::Xaml::Controls::IPage> browserPage;
    CHECK(browser && SUCCEEDED(browser.As(&browserPage)));
    CHECK(browser && browser->GoToPage(nullptr) == E_INVALIDARG);
    CHECK(browser && SUCCEEDED(browser->HideBrowser()));
    CHECK(helper->IsLoggedIn(nullptr) == E_POINTER);
    HString token;
    CHECK(SUCCEEDED(helper->GetAccessToken(token.GetAddressOf())) && token.Length() == 0);
    UINT32 status = 0;
    CHECK(SUCCEEDED(statics->GetPermissionStatus(HStringReference(L"email").Get(), &status)) && status == 2);
    CHECK(SUCCEEDED(statics->SetAppID(HStringReference(L"test-app").Get())));
    HString appId;
    CHECK(SUCCEEDED(helper->GetAppID(appId.GetAddressOf())) && wcscmp(appId.GetRawBuffer(nullptr), L"test-app") == 0);
    UINT32 size = 42;
    BYTE* bytes = reinterpret_cast<BYTE*>(1);
    CHECK(SUCCEEDED(statics->GetPictureData(nullptr, &size, &bytes)) && size == 0 && bytes == nullptr);
    CHECK(statics->GetPictureData(nullptr, nullptr, &bytes) == E_POINTER);
    int calls = 0;
    auto callback = Callback<IRequestFinished>([&](boolean error, HSTRING response, boolean canceled) -> HRESULT {
        ++calls;
        CHECK(error && !canceled);
        CHECK(wcsstr(WindowsGetStringRawBuffer(response, nullptr), L"Offline") != nullptr);
        // Reenter the bridge while completing a request (no lock held).
        CHECK(SUCCEEDED(helper->InitFacebookHelper(nullptr, nullptr, 0, nullptr)));
        return S_OK;
    });
    CHECK(SUCCEEDED(helper->InitFacebookHelper(nullptr, nullptr, 0, callback.Get())) && calls == 0);
    CHECK(SUCCEEDED(helper->CachePermissions(false)) && calls == 0);
    CHECK(SUCCEEDED(helper->Login()) && calls == 1);
    CHECK(SUCCEEDED(helper->Login()) && calls == 1); // Callback removed by the reentrant call.
    CHECK(SUCCEEDED(helper->InitFacebookHelper(nullptr, nullptr, 0, callback.Get())));
    CHECK(SUCCEEDED(helper->UploadVideo(nullptr, nullptr, nullptr, nullptr, nullptr)) && calls == 2);
    CHECK(SUCCEEDED(helper->IsLoggedIn(&loggedIn)) && !loggedIn);
    printf("facebooktest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
