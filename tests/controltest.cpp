#include <windows.h>
#undef GetCurrentTime
#include <roapi.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.media.dxinterop.h>
#include <windows.ui.xaml.media.h>
#include <wrl.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <cmath>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
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
    RoInitialize(RO_INIT_SINGLETHREADED);
    auto* dll = LoadLibraryW(L"appxportable.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Controls.Frame").Get(), &factory)));
    if (!factory) return 1;
    ComPtr<IInspectable> object;
    CHECK(SUCCEEDED(factory->ActivateInstance(&object)));
    if (!object) return 1;
    ComPtr<IUIElement> ui;
    CHECK(SUCCEEDED(object.As(&ui)));
    CHECK(SUCCEEDED(ui->put_Visibility(Visibility_Collapsed)));
    Visibility visibility{};
    CHECK(SUCCEEDED(ui->get_Visibility(&visibility)) && visibility == Visibility_Collapsed);
    CHECK(SUCCEEDED(ui->put_Visibility(Visibility_Visible)));
    ComPtr<IFrameworkElement> placementElement;
    CHECK(SUCCEEDED(object.As(&placementElement)));
    ComPtr<IActivationFactory> gridFactory;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Controls.Grid").Get(), &gridFactory)));
    ComPtr<IGridStatics> gridStatics;
    CHECK(SUCCEEDED(gridFactory.As(&gridStatics)));
    INT32 row{};
    CHECK(SUCCEEDED(gridStatics->SetRow(placementElement.Get(), 3)));
    CHECK(SUCCEEDED(gridStatics->GetRow(placementElement.Get(), &row)) && row == 3);
    CHECK(gridStatics->SetRow(placementElement.Get(), -1) == E_INVALIDARG);
    ABI::Windows::Foundation::Size size{};
    CHECK(SUCCEEDED(ui->get_RenderSize(&size)) && size.Width == 1024 && size.Height == 768);
    // Canvas must be created by the shim too: the OS requires a XAML core.
    ComPtr<IActivationFactory> canvasFactory;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Controls.Canvas").Get(), &canvasFactory)));
    ComPtr<IInspectable> canvasObject;
    CHECK(canvasFactory && SUCCEEDED(canvasFactory->ActivateInstance(&canvasObject)));
    ComPtr<ICanvas> canvas;
    ComPtr<IPanel> canvasPanel;
    CHECK(canvasObject && SUCCEEDED(canvasObject.As(&canvas)) && SUCCEEDED(canvasObject.As(&canvasPanel)));
    ComPtr<ICanvasFactory> canvasCompose;
    CHECK(canvasFactory && SUCCEEDED(canvasFactory.As(&canvasCompose)));
    ComPtr<ICanvas> composedCanvas;
    CHECK(canvasCompose && SUCCEEDED(canvasCompose->CreateInstance(nullptr, nullptr, &composedCanvas)));
    ComPtr<__FIVector_1_Windows__CUI__CXaml__CUIElement> children;
    CHECK(canvasPanel && SUCCEEDED(canvasPanel->get_Children(&children)));
    UINT32 childCount = 99;
    CHECK(children && SUCCEEDED(children->get_Size(&childCount)) && childCount == 0);
    CHECK(children && SUCCEEDED(children->Append(ui.Get())));
    ComPtr<IUIElement> child;
    CHECK(children && SUCCEEDED(children->GetAt(0, &child)) && child == ui);
    CHECK(children && SUCCEEDED(children->Clear()));
    ComPtr<ICanvasStatics> canvasStatics;
    CHECK(SUCCEEDED(canvasFactory.As(&canvasStatics)));
    DOUBLE left = 0, top = 0;
    INT32 zIndex = -1;
    CHECK(canvasStatics && SUCCEEDED(canvasStatics->GetLeft(ui.Get(), &left)) && std::isnan(left));
    CHECK(canvasStatics && SUCCEEDED(canvasStatics->GetTop(ui.Get(), &top)) && std::isnan(top));
    CHECK(canvasStatics && SUCCEEDED(canvasStatics->GetZIndex(ui.Get(), &zIndex)) && zIndex == 0);
    CHECK(canvasStatics && SUCCEEDED(canvasStatics->SetLeft(ui.Get(), 12.5)));
    CHECK(canvasStatics && SUCCEEDED(canvasStatics->SetTop(ui.Get(), -20)));
    CHECK(canvasStatics && SUCCEEDED(canvasStatics->SetZIndex(ui.Get(), -3)));
    CHECK(canvasStatics && SUCCEEDED(canvasStatics->GetLeft(ui.Get(), &left)) && left == 12.5);
    CHECK(canvasStatics && SUCCEEDED(canvasStatics->GetTop(ui.Get(), &top)) && top == -20);
    CHECK(canvasStatics && SUCCEEDED(canvasStatics->GetZIndex(ui.Get(), &zIndex)) && zIndex == -3);
    CHECK(canvasStatics && canvasStatics->SetLeft(nullptr, 0) == E_INVALIDARG);
    ComPtr<IActivationFactory> imageFactory;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Controls.Image").Get(), &imageFactory)));
    ComPtr<IInspectable> imageObject;
    CHECK(imageFactory && SUCCEEDED(imageFactory->ActivateInstance(&imageObject)));
    ComPtr<IImage> image;
    CHECK(imageObject && SUCCEEDED(imageObject.As(&image)));
    ComPtr<ABI::Windows::UI::Xaml::Media::IImageSource> source;
    CHECK(image && SUCCEEDED(image->get_Source(&source)) && !source);
    ABI::Windows::UI::Xaml::Media::Stretch stretch{};
    CHECK(image && SUCCEEDED(image->get_Stretch(&stretch)) &&
          stretch == ABI::Windows::UI::Xaml::Media::Stretch_Uniform);
    CHECK(image && SUCCEEDED(image->put_Stretch(ABI::Windows::UI::Xaml::Media::Stretch_Fill)));
    CHECK(image && SUCCEEDED(image->get_Stretch(&stretch)) && stretch == ABI::Windows::UI::Xaml::Media::Stretch_Fill);
    ComPtr<IUIElement> imageUi;
    CHECK(image && SUCCEEDED(image.As(&imageUi)));
    ComPtr<IFrameworkElement> imageElement;
    CHECK(image && SUCCEEDED(image.As(&imageElement)));
    HorizontalAlignment horizontal{};
    VerticalAlignment vertical{};
    Thickness margin{};
    CHECK(imageElement && SUCCEEDED(imageElement->put_HorizontalAlignment(HorizontalAlignment_Right)));
    CHECK(imageElement && SUCCEEDED(imageElement->get_HorizontalAlignment(&horizontal)) &&
          horizontal == HorizontalAlignment_Right);
    CHECK(imageElement && SUCCEEDED(imageElement->put_VerticalAlignment(VerticalAlignment_Bottom)));
    CHECK(imageElement && SUCCEEDED(imageElement->get_VerticalAlignment(&vertical)) &&
          vertical == VerticalAlignment_Bottom);
    CHECK(imageElement && SUCCEEDED(imageElement->put_Margin({1, -2, 3, 4})));
    CHECK(imageElement && SUCCEEDED(imageElement->get_Margin(&margin)) && margin.Top == -2 && margin.Right == 3);
    DOUBLE opacity = 0;
    boolean hitTest = true;
    CHECK(imageUi && SUCCEEDED(imageUi->put_Opacity(0.5)));
    CHECK(imageUi && SUCCEEDED(imageUi->get_Opacity(&opacity)) && opacity == 0.5);
    CHECK(imageUi && SUCCEEDED(imageUi->put_IsHitTestVisible(false)));
    CHECK(imageUi && SUCCEEDED(imageUi->get_IsHitTestVisible(&hitTest)) && !hitTest);
    CHECK(children && SUCCEEDED(children->Append(imageUi.Get())));
    CHECK(children && SUCCEEDED(children->GetAt(0, &child)) && child == imageUi);
    CHECK(children && SUCCEEDED(children->Clear()));
    // An OS XAML factory can be obtained without a XAML core, but creating a
    // RectangleGeometry then fails with RPC_E_WRONG_THREAD. Use the shim object.
    ComPtr<IActivationFactory> geometryFactory;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Media.RectangleGeometry").Get(), &geometryFactory)));
    ComPtr<IInspectable> geometryObject;
    CHECK(geometryFactory && SUCCEEDED(geometryFactory->ActivateInstance(&geometryObject)));
    ComPtr<ABI::Windows::UI::Xaml::Media::IRectangleGeometry> rectangle;
    CHECK(geometryObject && SUCCEEDED(geometryObject.As(&rectangle)));
    if (!rectangle) return 1;
    ABI::Windows::Foundation::Rect bounds{};
    CHECK(SUCCEEDED(rectangle->get_Rect(&bounds)) && bounds.Width == 0 && bounds.Height == 0);
    CHECK(SUCCEEDED(rectangle->put_Rect({10, 20, 300, 200})));
    CHECK(rectangle->get_Rect(nullptr) == E_POINTER);
    CHECK(rectangle->put_Rect({0, 0, -1, 20}) == E_INVALIDARG);
    ComPtr<ABI::Windows::UI::Xaml::Media::IGeometry> geometry;
    CHECK(SUCCEEDED(rectangle.As(&geometry)));
    CHECK(geometry && SUCCEEDED(geometry->get_Bounds(&bounds)) && bounds.X == 10 && bounds.Y == 20 &&
          bounds.Width == 300 && bounds.Height == 200);
    ComPtr<ABI::Windows::UI::Xaml::Media::IRectangleGeometry> clip;
    CHECK(SUCCEEDED(ui->get_Clip(&clip)) && !clip);
    CHECK(SUCCEEDED(ui->put_Clip(rectangle.Get())));
    rectangle.Reset();
    geometry.Reset();
    geometryObject.Reset();
    CHECK(SUCCEEDED(ui->get_Clip(&clip)) && clip);
    CHECK(clip && SUCCEEDED(clip->get_Rect(&bounds)) && bounds.Width == 300 && bounds.Height == 200);
    CHECK(SUCCEEDED(ui->put_Clip(nullptr)));
    clip.Reset();
    CHECK(SUCCEEDED(ui->get_Clip(&clip)) && !clip);
    ComPtr<INavigate> navigate;
    CHECK(SUCCEEDED(object.As(&navigate)));
    ComPtr<IFrame> frame;
    CHECK(SUCCEEDED(object.As(&frame)));
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Controls.Page").Get(), &factory)));
    if (!factory) return 1;
    ComPtr<IPageFactory> pageFactory;
    CHECK(SUCCEEDED(factory.As(&pageFactory)));
    if (!pageFactory) return 1;
    ComPtr<IPage> page;
    CHECK(SUCCEEDED(pageFactory->CreateInstance(nullptr, nullptr, &page)));
    if (!page) return 1;
    ComPtr<IFrameworkElement> element;
    CHECK(SUCCEEDED(page.As(&element)));
    if (!element) return 1;
    ComPtr<IInspectable> panel, same;
    CHECK(SUCCEEDED(element->FindName(HStringReference(L"swapChainPanel").Get(), &panel)));
    if (!panel) return 1;
    CHECK(SUCCEEDED(element->FindName(HStringReference(L"swapChainPanel").Get(), &same)));
    CHECK(same.Get() == panel.Get());
    ComPtr<ISwapChainPanelNative> nativePanel;
    CHECK(SUCCEEDED(panel.As(&nativePanel)));
    ComPtr<ISwapChainPanel> scale;
    CHECK(SUCCEEDED(panel.As(&scale)));
    if (!scale) return 1;
    float x = 0;
    CHECK(SUCCEEDED(scale->get_CompositionScaleX(&x)) && x == 1);
    ComPtr<IInspectable> background, sameBackground;
    CHECK(SUCCEEDED(element->FindName(HStringReference(L"SwapChainPanel").Get(), &background)) && background);
    CHECK(SUCCEEDED(element->FindName(HStringReference(L"SwapChainPanel").Get(), &sameBackground)) &&
          sameBackground.Get() == background.Get());
    ComPtr<ISwapChainBackgroundPanel> backgroundType;
    CHECK(background && SUCCEEDED(background.As(&backgroundType)));
    ComPtr<ISwapChainBackgroundPanelNative> backgroundNative;
    CHECK(background && SUCCEEDED(background.As(&backgroundNative)));
    CHECK(backgroundNative && SUCCEEDED(backgroundNative->SetSwapChain(nullptr)));
    ComPtr<IPanel> backgroundPanel;
    CHECK(background && SUCCEEDED(background.As(&backgroundPanel)));
    // MainPage.InitializeComponent safe_casts this name to Grid^.
    ComPtr<IInspectable> grid, sameGrid;
    CHECK(SUCCEEDED(element->FindName(HStringReference(L"gridForTextBlocks").Get(), &grid)) && grid);
    CHECK(SUCCEEDED(element->FindName(HStringReference(L"gridForTextBlocks").Get(), &sameGrid)) &&
          sameGrid.Get() == grid.Get());
    ComPtr<IGrid> gridType;
    CHECK(grid && SUCCEEDED(grid.As(&gridType)));
    ComPtr<IPanel> gridPanel;
    CHECK(grid && SUCCEEDED(grid.As(&gridPanel)));
    ComPtr<IPanel> swapPanel;
    CHECK(SUCCEEDED(panel.As(&swapPanel)));
    for (auto* container : {swapPanel.Get(), backgroundPanel.Get(), gridPanel.Get()}) {
        ComPtr<__FIVector_1_Windows__CUI__CXaml__CUIElement> panelChildren, sameChildren;
        CHECK(container && SUCCEEDED(container->get_Children(&panelChildren)) && panelChildren);
        CHECK(container && SUCCEEDED(container->get_Children(&sameChildren)) && panelChildren == sameChildren);
        CHECK(panelChildren && SUCCEEDED(panelChildren->Append(imageUi.Get())));
        ComPtr<IUIElement> retainedChild;
        CHECK(panelChildren && SUCCEEDED(panelChildren->GetAt(0, &retainedChild)) && retainedChild == imageUi);
        CHECK(panelChildren && SUCCEEDED(panelChildren->Clear()));
    }
    ComPtr<IFrameworkElement> gridElement;
    CHECK(grid && SUCCEEDED(grid.As(&gridElement)));
    CHECK(SUCCEEDED(element->FindName(HStringReference(L"missing").Get(), &same)) && !same);
    ComPtr<IContentControl> content;
    CHECK(SUCCEEDED(object.As(&content)));
    if (!content) return 1;
    CHECK(SUCCEEDED(content->put_Content(panel.Get())));
    CHECK(SUCCEEDED(content->get_Content(&same)) && same.Get() == panel.Get());
    factory.Reset();
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Controls.UserControl").Get(), &factory)));
    if (!factory) return 1;
    ComPtr<IUserControlFactory> userFactory;
    CHECK(SUCCEEDED(factory.As(&userFactory)));
    if (!userFactory) return 1;
    ComPtr<IUserControl> user;
    CHECK(SUCCEEDED(userFactory->CreateInstance(nullptr, nullptr, &user)));
    if (!user) return 1;
    ComPtr<IUIElement> panelElement;
    CHECK(SUCCEEDED(panel.As(&panelElement)));
    CHECK(SUCCEEDED(user->put_Content(panelElement.Get())));
    ComPtr<IUIElement> userContent;
    CHECK(SUCCEEDED(user->get_Content(&userContent)) && userContent.Get() == panelElement.Get());
    ComPtr<IControl> userControl;
    CHECK(SUCCEEDED(user.As(&userControl)));
    ComPtr<IUnknown> userIdentity, controlIdentity;
    CHECK(SUCCEEDED(user.As(&userIdentity)) && SUCCEEDED(userControl.As(&controlIdentity)) &&
          userIdentity.Get() == controlIdentity.Get());
    ComPtr<IFrameworkElement> userElement;
    CHECK(SUCCEEDED(user.As(&userElement)));
    CHECK(SUCCEEDED(user->put_Content(nullptr)));
    userContent.Reset();
    CHECK(SUCCEEDED(user->get_Content(&userContent)) && !userContent);
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Media.CompositionTarget").Get(), &factory)));
    ComPtr<ABI::Windows::UI::Xaml::Media::ICompositionTargetStatics> composition;
    CHECK(SUCCEEDED(factory.As(&composition)));
    if (!composition) return 1;
    unsigned frames = 0;
    INT64 previousTime = 0;
    const DWORD renderThread = GetCurrentThreadId();
    auto handler = Callback<ABI::Windows::Foundation::IEventHandler<IInspectable*>>(
        [&](IInspectable*, IInspectable* args) -> HRESULT {
            CHECK(GetCurrentThreadId() == renderThread);
            ComPtr<ABI::Windows::UI::Xaml::Media::IRenderingEventArgs> rendering;
            CHECK(args && SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&rendering))));
            ABI::Windows::Foundation::TimeSpan time{};
            CHECK(rendering && SUCCEEDED(rendering->get_RenderingTime(&time)) && time.Duration > previousTime);
            previousTime = time.Duration;
            ++frames;
            return S_OK;
        });
    EventRegistrationToken renderingToken{};
    CHECK(SUCCEEDED(composition->add_Rendering(handler.Get(), &renderingToken)));
    auto pump = [](DWORD duration) {
        const ULONGLONG until = GetTickCount64() + duration;
        while (GetTickCount64() < until) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        }
    };
    pump(150);
    CHECK(frames >= 2);
    // Static events may be removed using a separately requested factory.
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Media.CompositionTarget").Get(), &factory)));
    CHECK(SUCCEEDED(factory.As(&composition)));
    CHECK(SUCCEEDED(composition->remove_Rendering(renderingToken)));
    const unsigned stopped = frames;
    pump(65);
    CHECK(frames == stopped);
    // Angry Birds Space presents with vsync inside Rendering. The next event
    // must be due when that frame ends, or 60 Hz slips to every other vblank.
    LARGE_INTEGER frequency{}, start{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    unsigned vsynced = 0;
    auto present =
        Callback<ABI::Windows::Foundation::IEventHandler<IInspectable*>>([&](IInspectable*, IInspectable*) -> HRESULT {
            const LONGLONG vblank = start.QuadPart + ++vsynced * frequency.QuadPart / 60;
            LARGE_INTEGER now{};
            do QueryPerformanceCounter(&now);
            while (now.QuadPart < vblank);
            return S_OK;
        });
    CHECK(SUCCEEDED(composition->add_Rendering(present.Get(), &renderingToken)));
    pump(500);
    CHECK(SUCCEEDED(composition->remove_Rendering(renderingToken)));
    CHECK(vsynced >= 28);
    printf("controltest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
