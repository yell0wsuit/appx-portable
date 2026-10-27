#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#undef GetCurrentTime
#include <roapi.h>
#include <windows.applicationmodel.search.core.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.hosting.h>
#include <windows.ui.xaml.markup.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
namespace Core = ABI::Windows::ApplicationModel::Search::Core;

#ifndef _WIN64
// The page creates its SearchBox in system XAML, through the parser.
static int XamlSearchBox() {
    ComPtr<ABI::Windows::UI::Xaml::Hosting::IWindowsXamlManagerStatics> statics;
    ComPtr<ABI::Windows::UI::Xaml::Hosting::IWindowsXamlManager> xaml;
    if (FAILED(RoGetActivationFactory(HStringReference(L"Windows.UI.Xaml.Hosting.WindowsXamlManager").Get(),
                                      IID_PPV_ARGS(&statics))) ||
        FAILED(statics->InitializeForCurrentThread(&xaml)))
        return 9;
    ComPtr<IInspectable> object;
    HRESULT hr = RoActivateInstance(HStringReference(L"Windows.UI.Xaml.Controls.SearchBox").Get(), &object);
    ComPtr<ABI::Windows::UI::Xaml::Controls::ISearchBox> box;
    if (FAILED(hr) || FAILED(object.As(&box))) {
        printf("FAIL SearchBox hr=%08lx\n", hr);
        return 10;
    }
    if (FAILED(box->put_QueryText(HStringReference(L"affect").Get()))) return 11;
    // The game's page creates its SearchBox through the XAML parser.
    ComPtr<ABI::Windows::UI::Xaml::Markup::IXamlReaderStatics> reader;
    object.Reset();
    hr = RoGetActivationFactory(HStringReference(L"Windows.UI.Xaml.Markup.XamlReader").Get(), IID_PPV_ARGS(&reader));
    if (SUCCEEDED(hr))
        hr = reader->Load(
            HStringReference(L"<SearchBox xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
                             L"PlaceholderText=\"Search\"/>")
                .Get(),
            &object);
    if (FAILED(hr) || FAILED(object.As(&box))) {
        printf("FAIL parsed SearchBox hr=%08lx\n", hr);
        return 12;
    }
    return 0;
}
#endif
// Windows 8.1 SearchBox: system XAML creates a SearchSuggestionManager for it,
// which Windows refuses outside an app container (Practical English Usage).
int main() {
    if (FAILED(RoInitialize(RO_INIT_SINGLETHREADED))) return 1;
    auto* dll = LoadLibraryW(L"appxportable.dll");
    if (!dll) return 2;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));

    ComPtr<IActivationFactory> factory;
    ComPtr<IInspectable> object;
    ComPtr<Core::ISearchSuggestionManager> manager;
    if (FAILED(
            get(HStringReference(L"Windows.ApplicationModel.Search.Core.SearchSuggestionManager").Get(), &factory)) ||
        FAILED(factory->ActivateInstance(&object)) || FAILED(object.As(&manager))) {
        puts("FAIL SearchSuggestionManager");
        return 3;
    }
    boolean enabled = false;
    if (FAILED(manager->get_SearchHistoryEnabled(&enabled)) || !enabled) return 4;
    if (FAILED(manager->put_SearchHistoryEnabled(false)) || FAILED(manager->get_SearchHistoryEnabled(&enabled)) ||
        enabled)
        return 5;
    HString context;
    if (FAILED(manager->put_SearchHistoryContext(HStringReference(L"words").Get())) ||
        FAILED(manager->get_SearchHistoryContext(context.GetAddressOf())) ||
        wcscmp(context.GetRawBuffer(nullptr), L"words") != 0)
        return 6;
    ComPtr<__FIObservableVector_1_Windows__CApplicationModel__CSearch__CCore__CSearchSuggestion> suggestions;
    ComPtr<__FIVector_1_Windows__CApplicationModel__CSearch__CCore__CSearchSuggestion> list;
    UINT32 size = 1;
    if (FAILED(manager->get_Suggestions(&suggestions)) || !suggestions || FAILED(suggestions.As(&list)) ||
        FAILED(list->get_Size(&size)) || size != 0)
        return 7;
    if (FAILED(manager->SetQuery(HStringReference(L"affect").Get())) ||
        FAILED(manager->AddToHistory(HStringReference(L"affect").Get())) || FAILED(manager->ClearHistory()))
        return 8;
#ifndef _WIN64
    // System XAML's own activations reach the shim only through the x86
    // combase hotpatch; x64 hosts no managed games, so no XAML Islands.
    if (int failure = XamlSearchBox()) return failure;
#endif
    puts("searchtest: 0 failure(s)");
    return 0;
}
