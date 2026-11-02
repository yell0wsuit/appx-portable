#pragma once
#include <windows.ui.xaml.h>
#include <wrl/client.h>
#include <functional>
#include <set>
#include <string>
#include <vector>
namespace appx {
// Builds a source dictionary entry on its first lookup.
using ResourceFactory = std::function<HRESULT(Microsoft::WRL::ComPtr<IInspectable>& value)>;
HRESULT MakeResourceDictionary(ABI::Windows::UI::Xaml::IResourceDictionary** value);
HRESULT AddSourceResource(ABI::Windows::UI::Xaml::IResourceDictionary* dictionary, HSTRING key, IInspectable* value);
HRESULT AddDeferredSourceResource(ABI::Windows::UI::Xaml::IResourceDictionary* dictionary, const std::wstring& key,
                                  ResourceFactory factory);
// source is the package path a dictionary was loaded from, empty for inline dictionaries.
HRESULT SetSourceDictionaryParts(ABI::Windows::UI::Xaml::IResourceDictionary* dictionary, const std::wstring& source,
                                 std::vector<Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::IResourceDictionary>> merged,
                                 std::set<std::wstring> themeKeys);
}
