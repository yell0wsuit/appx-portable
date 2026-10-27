// The Windows 8.1 SearchBox: system XAML backs each SearchBox with a
// SearchSuggestionManager, which Windows refuses to create outside an app
// container, failing the page that holds the box. This manager keeps its
// settings and query in memory, stores no search history and offers no
// suggestions; typing in the box and submitting a query work as usual.
#include "generated/search.h"
#include "com/factory.h"
#include "registry.h"
#include <ocidl.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <winrt/Windows.ApplicationModel.Search.Core.h>
#include <winrt/Windows.Foundation.Collections.h>
using namespace Microsoft::WRL;
namespace appx {
namespace {
namespace Core = ABI::Windows::ApplicationModel::Search::Core;
namespace Search = ABI::Windows::ApplicationModel::Search;
using SuggestionsRequestedHandler =
    __FITypedEventHandler_2_Windows__CApplicationModel__CSearch__CCore__CSearchSuggestionManager_Windows__CApplicationModel__CSearch__CCore__CSearchSuggestionsRequestedEventArgs;
using FocusRequestedHandler =
    __FITypedEventHandler_2_Windows__CApplicationModel__CSearch__CCore__CSearchSuggestionManager_Windows__CApplicationModel__CSearch__CCore__CRequestingFocusOnKeyboardInputEventArgs;

class SearchSuggestionManager final
    : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Core::DefaultISearchSuggestionManager, IObjectWithSite,
                          FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.Search.Core.SearchSuggestionManager", BaseTrust);
    boolean history_ = true;
    Wrappers::HString context_, query_;
    winrt::Windows::Foundation::Collections::IObservableVector<
        winrt::Windows::ApplicationModel::Search::Core::SearchSuggestion>
        suggestions_ = winrt::single_threaded_observable_vector<
            winrt::Windows::ApplicationModel::Search::Core::SearchSuggestion>();
    EventSource<SuggestionsRequestedHandler> requested_;
    EventSource<FocusRequestedHandler> focus_;
    ComPtr<IUnknown> site_;

public:
    // System XAML sites the manager on its SearchBox.
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) override {
        site_ = site;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSite(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return site_ ? site_.CopyTo(iid, value) : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE get_SearchHistoryEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = history_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_SearchHistoryEnabled(boolean value) override {
        history_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SearchHistoryContext(HSTRING* value) override {
        if (!value) return E_POINTER;
        return context_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_SearchHistoryContext(HSTRING value) override { return context_.Set(value); }
    HRESULT STDMETHODCALLTYPE SetLocalContentSuggestionSettings(Search::ILocalContentSuggestionSettings*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetQuery(HSTRING queryText) override { return query_.Set(queryText); }
    HRESULT STDMETHODCALLTYPE SetQueryWithLanguage(HSTRING queryText, HSTRING) override {
        return query_.Set(queryText);
    }
    HRESULT STDMETHODCALLTYPE SetQueryWithSearchQueryLinguisticDetails(
        HSTRING queryText, HSTRING, Search::ISearchQueryLinguisticDetails*) override {
        return query_.Set(queryText);
    }
    HRESULT STDMETHODCALLTYPE get_Suggestions(
        __FIObservableVector_1_Windows__CApplicationModel__CSearch__CCore__CSearchSuggestion** value) override {
        if (!value) return E_POINTER;
        *value = static_cast<__FIObservableVector_1_Windows__CApplicationModel__CSearch__CCore__CSearchSuggestion*>(
            winrt::get_abi(suggestions_));
        (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AddToHistory(HSTRING) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddToHistoryWithLanguage(HSTRING, HSTRING) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ClearHistory() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_SuggestionsRequested(SuggestionsRequestedHandler* handler,
                                                       EventRegistrationToken* token) override {
        return requested_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_SuggestionsRequested(EventRegistrationToken token) override {
        return requested_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_RequestingFocusOnKeyboardInput(FocusRequestedHandler* handler,
                                                                 EventRegistrationToken* token) override {
        return focus_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_RequestingFocusOnKeyboardInput(EventRegistrationToken token) override {
        return focus_.Remove(token);
    }
};
class SearchSuggestionManagerFactory final : public Factory<> {
public:
    SearchSuggestionManagerFactory() : Factory(L"Windows.ApplicationModel.Search.Core.SearchSuggestionManager") {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto manager = Make<SearchSuggestionManager>();
        return manager ? manager.CopyTo(value) : E_OUTOFMEMORY;
    }
};
HRESULT MakeSearchSuggestionManagerFactory(IActivationFactory** value) {
    auto factory = Make<SearchSuggestionManagerFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
// System XAML creates the manager for its own SearchBox, so it is served to
// Windows' callers too, through the x86 combase hotpatch (XAML Islands runs
// only in x86 managed games); Windows' class never works outside an app container.
const ShimClass searchSuggestionManagerClass{L"Windows.ApplicationModel.Search.Core.SearchSuggestionManager",
                                             MakeSearchSuggestionManagerFactory, ShimPolicy::ReplaceForAll, InIslands};
}
}
