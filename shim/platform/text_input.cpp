#include "text_input.h"
#include "registry.h"
#include "log.h"
#include "com/factory.h"
#include "generated/text_core.h"
#include "host/host.h"
#include <windows.globalization.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <string>
#include <utility>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Text::Core;
namespace appx {
namespace {
// Windows' CoreTextServicesManager takes text input through a packaged app's
// CoreWindow, which the game's desktop window is not, so text never reaches
// its edit contexts (Unity's text fields take text only from one). Here the
// game window's WM_CHAR becomes TextUpdating on the focused context, at the
// selection the game reports. No IME composition or layout is offered.
using TextRequestedHandler =
    __FITypedEventHandler_2_Windows__CUI__CText__CCore__CCoreTextEditContext_Windows__CUI__CText__CCore__CCoreTextTextRequestedEventArgs;
using SelectionRequestedHandler =
    __FITypedEventHandler_2_Windows__CUI__CText__CCore__CCoreTextEditContext_Windows__CUI__CText__CCore__CCoreTextSelectionRequestedEventArgs;
using LayoutRequestedHandler =
    __FITypedEventHandler_2_Windows__CUI__CText__CCore__CCoreTextEditContext_Windows__CUI__CText__CCore__CCoreTextLayoutRequestedEventArgs;
using TextUpdatingHandler =
    __FITypedEventHandler_2_Windows__CUI__CText__CCore__CCoreTextEditContext_Windows__CUI__CText__CCore__CCoreTextTextUpdatingEventArgs;
using SelectionUpdatingHandler =
    __FITypedEventHandler_2_Windows__CUI__CText__CCore__CCoreTextEditContext_Windows__CUI__CText__CCore__CCoreTextSelectionUpdatingEventArgs;
using FormatUpdatingHandler =
    __FITypedEventHandler_2_Windows__CUI__CText__CCore__CCoreTextEditContext_Windows__CUI__CText__CCore__CCoreTextFormatUpdatingEventArgs;
using CompositionStartedHandler =
    __FITypedEventHandler_2_Windows__CUI__CText__CCore__CCoreTextEditContext_Windows__CUI__CText__CCore__CCoreTextCompositionStartedEventArgs;
using CompositionCompletedHandler =
    __FITypedEventHandler_2_Windows__CUI__CText__CCore__CCoreTextEditContext_Windows__CUI__CText__CCore__CCoreTextCompositionCompletedEventArgs;
using ContextHandler = __FITypedEventHandler_2_Windows__CUI__CText__CCore__CCoreTextEditContext_IInspectable;
using LanguageHandler = __FITypedEventHandler_2_Windows__CUI__CText__CCore__CCoreTextServicesManager_IInspectable;

// The keyboard's language, as the input language Windows reports.
HRESULT InputLanguage(ABI::Windows::Globalization::ILanguage** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    wchar_t tag[LOCALE_NAME_MAX_LENGTH]{};
    const auto layout = LOWORD(HandleToUlong(GetKeyboardLayout(0)));
    if (!LCIDToLocaleName(MAKELCID(layout, SORT_DEFAULT), tag, ARRAYSIZE(tag), 0)) wcscpy_s(tag, L"en-US");
    ComPtr<ABI::Windows::Globalization::ILanguageFactory> factory;
    auto hr = OsGetActivationFactory(Wrappers::HStringReference(L"Windows.Globalization.Language").Get(),
                                     IID_PPV_ARGS(&factory));
    return SUCCEEDED(hr) ? factory->CreateLanguage(Wrappers::HStringReference(tag).Get(), value) : hr;
}
// Every request completes synchronously, so a deferral has nothing to wait for.
class Deferral final : public RuntimeClass<RuntimeClassFlags<WinRt>, ABI::Windows::Foundation::IDeferral,
                                           ABI::Windows::Foundation::IClosable, FtmBase> {
    InspectableClass(L"Windows.Foundation.Deferral", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE Complete() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};
HRESULT MakeDeferral(ABI::Windows::Foundation::IDeferral** value) {
    if (!value) return E_POINTER;
    auto deferral = Make<Deferral>();
    return deferral ? deferral.CopyTo(value) : E_OUTOFMEMORY;
}
class TextUpdatingArgs final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, ABI::Windows::UI::Text::Core::DefaultICoreTextTextUpdatingEventArgs,
                          FtmBase> {
    InspectableClass(L"Windows.UI.Text.Core.CoreTextTextUpdatingEventArgs", BaseTrust);
    CoreTextRange range_, selection_;
    std::wstring text_;
    CoreTextTextUpdatingResult result_ = CoreTextTextUpdatingResult_Succeeded;

public:
    TextUpdatingArgs(CoreTextRange range, std::wstring text, CoreTextRange selection)
        : range_(range), selection_(selection), text_(std::move(text)) {}
    CoreTextTextUpdatingResult Result() const { return result_; }
    HRESULT STDMETHODCALLTYPE get_Range(CoreTextRange* value) override {
        if (!value) return E_POINTER;
        *value = range_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Text(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(text_.c_str(), static_cast<UINT32>(text_.size()), value);
    }
    HRESULT STDMETHODCALLTYPE get_NewSelection(CoreTextRange* value) override {
        if (!value) return E_POINTER;
        *value = selection_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_InputLanguage(ABI::Windows::Globalization::ILanguage** value) override {
        return InputLanguage(value);
    }
    HRESULT STDMETHODCALLTYPE get_Result(CoreTextTextUpdatingResult* value) override {
        if (!value) return E_POINTER;
        *value = result_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Result(CoreTextTextUpdatingResult value) override {
        result_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsCanceled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeferral(ABI::Windows::Foundation::IDeferral** value) override {
        return MakeDeferral(value);
    }
};
class SelectionRequest final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, ABI::Windows::UI::Text::Core::DefaultICoreTextSelectionRequest,
                          FtmBase> {
    InspectableClass(L"Windows.UI.Text.Core.CoreTextSelectionRequest", BaseTrust);

public:
    CoreTextRange selection{0, 0};
    HRESULT STDMETHODCALLTYPE get_Selection(CoreTextRange* value) override {
        if (!value) return E_POINTER;
        *value = selection;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Selection(CoreTextRange value) override {
        selection = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsCanceled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeferral(ABI::Windows::Foundation::IDeferral** value) override {
        return MakeDeferral(value);
    }
};
class SelectionRequestedArgs final
    : public RuntimeClass<RuntimeClassFlags<WinRt>,
                          ABI::Windows::UI::Text::Core::DefaultICoreTextSelectionRequestedEventArgs, FtmBase> {
    InspectableClass(L"Windows.UI.Text.Core.CoreTextSelectionRequestedEventArgs", BaseTrust);
    ComPtr<SelectionRequest> request_;

public:
    explicit SelectionRequestedArgs(ComPtr<SelectionRequest> request) : request_(std::move(request)) {}
    HRESULT STDMETHODCALLTYPE get_Request(ICoreTextSelectionRequest** value) override {
        if (!value) return E_POINTER;
        return request_.CopyTo(value);
    }
};
class EditContext;
// The context with focus; the UI thread alone reads and changes it.
ComPtr<EditContext> focused;
class EditContext final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, ABI::Windows::UI::Text::Core::DefaultICoreTextEditContext,
                          ABI::Windows::UI::Text::Core::DefaultICoreTextEditContext2, FtmBase> {
    InspectableClass(L"Windows.UI.Text.Core.CoreTextEditContext", BaseTrust);
    Wrappers::HString name_;
    CoreTextInputScope scope_ = CoreTextInputScope_Default;
    boolean readOnly_ = false;
    CoreTextInputPaneDisplayPolicy policy_ = CoreTextInputPaneDisplayPolicy_Automatic;
    CoreTextRange selection_{0, 0};
    wchar_t highSurrogate_ = 0;
    EventSource<TextRequestedHandler> textRequested_;
    EventSource<SelectionRequestedHandler> selectionRequested_;
    EventSource<LayoutRequestedHandler> layoutRequested_;
    EventSource<TextUpdatingHandler> textUpdating_;
    EventSource<SelectionUpdatingHandler> selectionUpdating_;
    EventSource<FormatUpdatingHandler> formatUpdating_;
    EventSource<CompositionStartedHandler> compositionStarted_;
    EventSource<CompositionCompletedHandler> compositionCompleted_;
    EventSource<ContextHandler> focusRemoved_;
    EventSource<ContextHandler> focusLeaveCompleted_;

    ICoreTextEditContext* Self() { return static_cast<ICoreTextEditContext*>(this); }

public:
    CoreTextRange Selection() const {
        CoreTextRange range = selection_;
        if (range.StartCaretPosition > range.EndCaretPosition)
            std::swap(range.StartCaretPosition, range.EndCaretPosition);
        return range;
    }
    // Text the user typed, or a deletion (empty text), replacing the range.
    void Type(const std::wstring& text, CoreTextRange range) {
        const INT32 caret = range.StartCaretPosition + static_cast<INT32>(text.size());
        auto args = Make<TextUpdatingArgs>(range, text, CoreTextRange{caret, caret});
        if (!args) return;
        textUpdating_.InvokeAll(Self(), args.Get());
        Log("TRACE", "TextUpdating %d-%d \"%ls\" result=%d", range.StartCaretPosition, range.EndCaretPosition,
            text.c_str(), args->Result());
        if (args->Result() == CoreTextTextUpdatingResult_Succeeded) selection_ = {caret, caret};
    }
    bool Character(wchar_t character) {
        if (IS_HIGH_SURROGATE(character)) {
            highSurrogate_ = character;
            return true;
        }
        // Backspace deleted on its key press (Backspace below).
        if (character == L'\b') {
            highSurrogate_ = 0;
            return true;
        }
        std::wstring text;
        if (IS_LOW_SURROGATE(character)) {
            if (!highSurrogate_) return true;
            text = {highSurrogate_, character};
        } else if (character < 0x20 || character == 0x7F) {
            // Enter, Tab, Escape and Ctrl+letter are keys, not text.
            return false;
        } else {
            text = character;
        }
        highSurrogate_ = 0;
        if (!readOnly_) Type(text, Selection());
        return true;
    }
    // Windows' text service deletes the selection, or the character before the
    // caret, when Backspace goes down, and keeps the key from the game: Unity's
    // KeyDown moves only its own record of the text, whose field changes
    // through TextUpdating alone.
    void Backspace() {
        CoreTextRange range = Selection();
        if (range.StartCaretPosition == range.EndCaretPosition) {
            if (range.StartCaretPosition == 0) return;
            --range.StartCaretPosition;
        }
        if (!readOnly_) Type(std::wstring(), range);
    }
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* value) override {
        if (!value) return E_POINTER;
        return name_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Name(HSTRING value) override { return name_.Set(value); }
    HRESULT STDMETHODCALLTYPE get_InputScope(CoreTextInputScope* value) override {
        if (!value) return E_POINTER;
        *value = scope_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_InputScope(CoreTextInputScope value) override {
        scope_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(boolean* value) override {
        if (!value) return E_POINTER;
        *value = readOnly_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsReadOnly(boolean value) override {
        readOnly_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_InputPaneDisplayPolicy(CoreTextInputPaneDisplayPolicy* value) override {
        if (!value) return E_POINTER;
        *value = policy_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_InputPaneDisplayPolicy(CoreTextInputPaneDisplayPolicy value) override {
        policy_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_TextRequested(TextRequestedHandler* handler, EventRegistrationToken* token) override {
        return textRequested_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_TextRequested(EventRegistrationToken token) override {
        return textRequested_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_SelectionRequested(SelectionRequestedHandler* handler,
                                                     EventRegistrationToken* token) override {
        return selectionRequested_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_SelectionRequested(EventRegistrationToken token) override {
        return selectionRequested_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_LayoutRequested(LayoutRequestedHandler* handler,
                                                  EventRegistrationToken* token) override {
        return layoutRequested_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_LayoutRequested(EventRegistrationToken token) override {
        return layoutRequested_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_TextUpdating(TextUpdatingHandler* handler, EventRegistrationToken* token) override {
        return textUpdating_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_TextUpdating(EventRegistrationToken token) override {
        return textUpdating_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_SelectionUpdating(SelectionUpdatingHandler* handler,
                                                    EventRegistrationToken* token) override {
        return selectionUpdating_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_SelectionUpdating(EventRegistrationToken token) override {
        return selectionUpdating_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_FormatUpdating(FormatUpdatingHandler* handler,
                                                 EventRegistrationToken* token) override {
        return formatUpdating_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_FormatUpdating(EventRegistrationToken token) override {
        return formatUpdating_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_CompositionStarted(CompositionStartedHandler* handler,
                                                     EventRegistrationToken* token) override {
        return compositionStarted_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_CompositionStarted(EventRegistrationToken token) override {
        return compositionStarted_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_CompositionCompleted(CompositionCompletedHandler* handler,
                                                       EventRegistrationToken* token) override {
        return compositionCompleted_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_CompositionCompleted(EventRegistrationToken token) override {
        return compositionCompleted_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_FocusRemoved(ContextHandler* handler, EventRegistrationToken* token) override {
        return focusRemoved_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_FocusRemoved(EventRegistrationToken token) override {
        return focusRemoved_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_NotifyFocusLeaveCompleted(ContextHandler* handler,
                                                            EventRegistrationToken* token) override {
        return focusLeaveCompleted_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_NotifyFocusLeaveCompleted(EventRegistrationToken token) override {
        return focusLeaveCompleted_.Remove(token);
    }
    // Focus starts from the selection the game reports.
    HRESULT STDMETHODCALLTYPE NotifyFocusEnter() override {
        if (GetCurrentThreadId() != HostThread()) return RPC_E_WRONG_THREAD;
        focused = this;
        highSurrogate_ = 0;
        auto request = Make<SelectionRequest>();
        auto args = request ? Make<SelectionRequestedArgs>(request) : nullptr;
        if (!args) return E_OUTOFMEMORY;
        request->selection = selection_;
        selectionRequested_.InvokeAll(Self(), args.Get());
        selection_ = request->selection;
        Log("TRACE", "NotifyFocusEnter selection %d-%d", selection_.StartCaretPosition, selection_.EndCaretPosition);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE NotifyFocusLeave() override {
        if (GetCurrentThreadId() != HostThread()) return RPC_E_WRONG_THREAD;
        if (focused.Get() == this) focused.Reset();
        return focusLeaveCompleted_.InvokeAll(Self(), nullptr);
    }
    HRESULT STDMETHODCALLTYPE NotifyTextChanged(CoreTextRange range, INT32 length, CoreTextRange selection) override {
        Log("TRACE", "NotifyTextChanged %d-%d length=%d selection %d-%d", range.StartCaretPosition,
            range.EndCaretPosition, length, selection.StartCaretPosition, selection.EndCaretPosition);
        selection_ = selection;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE NotifySelectionChanged(CoreTextRange selection) override {
        Log("TRACE", "NotifySelectionChanged %d-%d", selection.StartCaretPosition, selection.EndCaretPosition);
        selection_ = selection;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE NotifyLayoutChanged() override { return S_OK; }
};
class ServicesManager final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, ABI::Windows::UI::Text::Core::DefaultICoreTextServicesManager,
                          FtmBase> {
    InspectableClass(L"Windows.UI.Text.Core.CoreTextServicesManager", BaseTrust);
    EventSource<LanguageHandler> languageChanged_;

public:
    HRESULT STDMETHODCALLTYPE get_InputLanguage(ABI::Windows::Globalization::ILanguage** value) override {
        return InputLanguage(value);
    }
    HRESULT STDMETHODCALLTYPE add_InputLanguageChanged(LanguageHandler* handler,
                                                       EventRegistrationToken* token) override {
        return languageChanged_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_InputLanguageChanged(EventRegistrationToken token) override {
        return languageChanged_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE CreateEditContext(ICoreTextEditContext** value) override {
        if (!value) return E_POINTER;
        auto context = Make<EditContext>();
        return context ? context.CopyTo(value) : E_OUTOFMEMORY;
    }
};
// One manager for the view, on its UI thread only, as on Windows.
class ServicesManagerFactory final
    : public Factory<ABI::Windows::UI::Text::Core::DefaultICoreTextServicesManagerStatics> {
public:
    ServicesManagerFactory() : Factory(L"Windows.UI.Text.Core.CoreTextServicesManager") {}
    HRESULT STDMETHODCALLTYPE GetForCurrentView(ICoreTextServicesManager** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!HostThread() || GetCurrentThreadId() != HostThread()) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        static ComPtr<ServicesManager> manager = Make<ServicesManager>();
        return manager ? manager.CopyTo(value) : E_OUTOFMEMORY;
    }
};
HRESULT MakeServicesManagerFactory(IActivationFactory** value) {
    auto factory = Make<ServicesManagerFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
bool TextInputCharacter(WPARAM character) noexcept {
    try {
        return focused && focused->Character(static_cast<wchar_t>(character));
    } catch (...) {
        return true;
    }
}
bool TextInputKey(WPARAM key) noexcept {
    if (!focused || key != VK_BACK) return false;
    try {
        focused->Backspace();
    } catch (...) {}
    return true;
}
const ShimClass textServicesClass{L"Windows.UI.Text.Core.CoreTextServicesManager", MakeServicesManagerFactory};
}
