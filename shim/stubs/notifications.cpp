#include "generated/notifications.h"
#include "com/factory.h"
#include "registry.h"
#include "islands/islands.h"
#include "xml.h"
#include <mutex>
#include <vector>
#include <wrl/event.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Notifications;
namespace appx {
namespace {
// Notifications behave as disabled by the user: nothing is ever displayed, but
// calls succeed as on Windows. C++/CX turns any failing HRESULT into an
// exception, which ended the game's render work item.
using ToastView = __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification;
using ToastIterable = __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification;
using ToastIterator = __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification;
using ToastList = std::vector<ComPtr<IScheduledToastNotification>>;
class ToastListIterator final : public RuntimeClass<RuntimeClassFlags<WinRt>, ToastIterator, FtmBase> {
    InspectableClass(L"Windows.Foundation.Collections.IIterator`1", BaseTrust);
    ToastList items_;
    UINT32 index_ = 0;

public:
    explicit ToastListIterator(ToastList items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE get_Current(IScheduledToastNotification** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return index_ < items_.size() ? items_[index_].CopyTo(value) : E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* value) override {
        if (!value) return E_POINTER;
        *value = index_ < items_.size();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* value) override {
        if (index_ < items_.size()) ++index_;
        return get_HasCurrent(value);
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 capacity, IScheduledToastNotification** values, UINT32* actual) override {
        if (!actual) return E_POINTER;
        *actual = 0;
        if (capacity && !values) return E_POINTER;
        while (*actual < capacity && index_ < items_.size()) {
            items_[index_++].CopyTo(&values[(*actual)++]);
        }
        return S_OK;
    }
};
class ToastListView final : public RuntimeClass<RuntimeClassFlags<WinRt>, ToastView, ToastIterable, FtmBase> {
    InspectableClass(L"Windows.Foundation.Collections.IVectorView`1", BaseTrust);
    APPX_LOG_MISSING_INTERFACES("ToastListView")
    ToastList items_;

public:
    explicit ToastListView(ToastList items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, IScheduledToastNotification** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return index < items_.size() ? items_[index].CopyTo(value) : E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<UINT32>(items_.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IndexOf(IScheduledToastNotification* item, UINT32* index, boolean* found) override {
        if (!index || !found) return E_POINTER;
        *index = 0;
        *found = false;
        for (UINT32 i = 0; i < items_.size(); ++i)
            if (items_[i].Get() == item) {
                *index = i;
                *found = true;
                break;
            }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 start, UINT32 capacity, IScheduledToastNotification** values,
                                      UINT32* actual) override {
        if (!actual) return E_POINTER;
        *actual = 0;
        if (capacity && !values) return E_POINTER;
        if (start > items_.size()) return E_BOUNDS;
        for (UINT32 i = start; i < items_.size() && *actual < capacity; ++i) items_[i].CopyTo(&values[(*actual)++]);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE First(ToastIterator** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto iterator = Make<ToastListIterator>(items_);
        return iterator ? iterator.CopyTo(value) : E_OUTOFMEMORY;
    }
};
// Process-wide schedule; every notifier for this app shares it.
std::mutex scheduleMutex;
ToastList& Schedule() {
    static ToastList schedule;
    return schedule;
}
class ScheduledToast final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIScheduledToastNotification, FtmBase> {
    InspectableClass(L"Windows.UI.Notifications.ScheduledToastNotification", BaseTrust);
    APPX_LOG_MISSING_INTERFACES("ScheduledToast")
    ComPtr<ABI::Windows::Data::Xml::Dom::IXmlDocument> content_;
    ABI::Windows::Foundation::DateTime delivery_;
    Wrappers::HString id_;

public:
    ScheduledToast(ABI::Windows::Data::Xml::Dom::IXmlDocument* content, ABI::Windows::Foundation::DateTime delivery)
        : content_(content), delivery_(delivery) {}
    HRESULT STDMETHODCALLTYPE get_Content(ABI::Windows::Data::Xml::Dom::IXmlDocument** value) override {
        if (!value) return E_POINTER;
        return content_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_DeliveryTime(ABI::Windows::Foundation::DateTime* value) override {
        if (!value) return E_POINTER;
        *value = delivery_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SnoozeInterval(__FIReference_1_Windows__CFoundation__CTimeSpan** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_MaximumSnoozeCount(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Id(HSTRING value) override { return id_.Set(value); }
    HRESULT STDMETHODCALLTYPE get_Id(HSTRING* value) override {
        if (!value) return E_POINTER;
        return id_.CopyTo(value);
    }
};
class DisabledNotifier final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIToastNotifier, FtmBase> {
    InspectableClass(L"Windows.UI.Notifications.ToastNotifier", BaseTrust);
    APPX_LOG_MISSING_INTERFACES("DisabledNotifier")
public:
    HRESULT STDMETHODCALLTYPE Show(IToastNotification* value) override { return value ? S_OK : E_INVALIDARG; }
    HRESULT STDMETHODCALLTYPE Hide(IToastNotification* value) override { return value ? S_OK : E_INVALIDARG; }
    HRESULT STDMETHODCALLTYPE get_Setting(NotificationSetting* value) override {
        if (!value) return E_POINTER;
        *value = NotificationSetting_DisabledForApplication;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AddToSchedule(IScheduledToastNotification* value) override {
        if (!value) return E_INVALIDARG;
        std::lock_guard<std::mutex> lock(scheduleMutex);
        Schedule().emplace_back(value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RemoveFromSchedule(IScheduledToastNotification* value) override {
        if (!value) return E_INVALIDARG;
        std::lock_guard<std::mutex> lock(scheduleMutex);
        auto& schedule = Schedule();
        for (auto it = schedule.begin(); it != schedule.end(); ++it)
            if (it->Get() == value) {
                schedule.erase(it);
                break;
            }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetScheduledToastNotifications(ToastView** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        ToastList snapshot;
        {
            std::lock_guard<std::mutex> lock(scheduleMutex);
            snapshot = Schedule();
        }
        auto view = Make<ToastListView>(std::move(snapshot));
        return view ? view.CopyTo(value) : E_OUTOFMEMORY;
    }
};
class ToastFactory final : public Factory<DefaultIToastNotificationManagerStatics> {
public:
    ToastFactory() : Factory(L"Windows.UI.Notifications.ToastNotificationManager") {}
    HRESULT STDMETHODCALLTYPE CreateToastNotifier(IToastNotifier** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto notifier = Make<DisabledNotifier>();
        return notifier ? notifier.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE CreateToastNotifierWithId(HSTRING, IToastNotifier** value) override {
        return CreateToastNotifier(value);
    }
    HRESULT STDMETHODCALLTYPE GetTemplateContent(ToastTemplateType type,
                                                 ABI::Windows::Data::Xml::Dom::IXmlDocument** value) override {
        if (IslandsMode()) {
            // The OS ToastNotification constructor expects an OS XML document.
            // Template retrieval works unpackaged; notifier creation does not.
            ComPtr<IToastNotificationManagerStatics> real;
            auto hr = OsGetActivationFactory(
                Wrappers::HStringReference(L"Windows.UI.Notifications.ToastNotificationManager").Get(),
                IID_PPV_ARGS(&real));
            return SUCCEEDED(hr) ? real->GetTemplateContent(type, value) : hr;
        }
        return MakeToastTemplate(type, value);
    }
};
// A toast the game fills and shows; notifications are disabled, so the notifier
// accepts it silently and none of its events is ever raised.
class Toast final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIToastNotification, FtmBase> {
    InspectableClass(L"Windows.UI.Notifications.ToastNotification", BaseTrust);
    APPX_LOG_MISSING_INTERFACES("Toast")
    ComPtr<ABI::Windows::Data::Xml::Dom::IXmlDocument> content_;
    ComPtr<__FIReference_1_Windows__CFoundation__CDateTime> expiration_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_Windows__CUI__CNotifications__CToastDismissedEventArgs> dismissed_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable> activated_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_Windows__CUI__CNotifications__CToastFailedEventArgs> failed_;

public:
    explicit Toast(ABI::Windows::Data::Xml::Dom::IXmlDocument* content) : content_(content) {}
    HRESULT STDMETHODCALLTYPE get_Content(ABI::Windows::Data::Xml::Dom::IXmlDocument** value) override {
        if (!value) return E_POINTER;
        return content_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_ExpirationTime(__FIReference_1_Windows__CFoundation__CDateTime* value) override {
        expiration_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ExpirationTime(__FIReference_1_Windows__CFoundation__CDateTime** value) override {
        if (!value) return E_POINTER;
        return expiration_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE add_Dismissed(
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_Windows__CUI__CNotifications__CToastDismissedEventArgs* handler,
        EventRegistrationToken* token) override {
        return dismissed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Dismissed(EventRegistrationToken token) override { return dismissed_.Remove(token); }
    HRESULT STDMETHODCALLTYPE add_Activated(__FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable* handler,
                                            EventRegistrationToken* token) override {
        return activated_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Activated(EventRegistrationToken token) override { return activated_.Remove(token); }
    HRESULT STDMETHODCALLTYPE add_Failed(
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_Windows__CUI__CNotifications__CToastFailedEventArgs* handler,
        EventRegistrationToken* token) override {
        return failed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Failed(EventRegistrationToken token) override { return failed_.Remove(token); }
};
class ToastNotificationFactory final : public Factory<DefaultIToastNotificationFactory> {
public:
    ToastNotificationFactory() : Factory(L"Windows.UI.Notifications.ToastNotification") {}
    HRESULT STDMETHODCALLTYPE CreateToastNotification(ABI::Windows::Data::Xml::Dom::IXmlDocument* content,
                                                      IToastNotification** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!content) return E_INVALIDARG;
        auto toast = Make<Toast>(content);
        return toast ? toast.CopyTo(value) : E_OUTOFMEMORY;
    }
};
class ScheduledToastFactory final : public Factory<DefaultIScheduledToastNotificationFactory> {
public:
    ScheduledToastFactory() : Factory(L"Windows.UI.Notifications.ScheduledToastNotification") {}
    HRESULT STDMETHODCALLTYPE CreateScheduledToastNotification(ABI::Windows::Data::Xml::Dom::IXmlDocument* content,
                                                               ABI::Windows::Foundation::DateTime delivery,
                                                               IScheduledToastNotification** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!content) return E_INVALIDARG;
        auto toast = Make<ScheduledToast>(content, delivery);
        return toast ? toast.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE CreateScheduledToastNotificationRecurring(
        ABI::Windows::Data::Xml::Dom::IXmlDocument* content, ABI::Windows::Foundation::DateTime delivery,
        ABI::Windows::Foundation::TimeSpan, UINT32, IScheduledToastNotification** value) override {
        return CreateScheduledToastNotification(content, delivery, value);
    }
};
}
HRESULT MakeToastFactory(IActivationFactory** value) {
    auto factory = Make<ToastFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
HRESULT MakeToastNotificationFactory(IActivationFactory** value) {
    auto factory = Make<ToastNotificationFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
HRESULT MakeScheduledToastFactory(IActivationFactory** value) {
    auto factory = Make<ScheduledToastFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
const ShimClass toastsClass{L"Windows.UI.Notifications.ToastNotificationManager", MakeToastFactory, ShimPolicy::Replace,
                            InIslands};
const ShimClass scheduledToastClass{L"Windows.UI.Notifications.ScheduledToastNotification", MakeScheduledToastFactory,
                                    ShimPolicy::Replace, InIslands};
// Windows' own toast object where it exists (Islands); the shim's under Wine.
const ShimClass toastNotificationClass{L"Windows.UI.Notifications.ToastNotification", MakeToastNotificationFactory,
                                       ShimPolicy::FillMissing};
}
