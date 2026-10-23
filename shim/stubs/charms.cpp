// Windows 8 shell integration that needs a real CoreWindow and the shell:
// the Share and Settings charms and live tiles. Games get working objects;
// with no charms and no Start tile, events never fire and updates go nowhere.
// SettingsPane is deprecated in later SDKs; Windows 8 games still call it.
#pragma warning(disable : 4996)
#include "generated/share.h"
#include "generated/settings_pane.h"
#include "generated/tiles.h"
#include "com/factory.h"
#include "registry.h"
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
using namespace Microsoft::WRL;
namespace appx {
namespace {
namespace Share = ABI::Windows::ApplicationModel::DataTransfer;
namespace Settings = ABI::Windows::UI::ApplicationSettings;
namespace Tiles = ABI::Windows::UI::Notifications;

class DataTransferManager final : public RuntimeClass<RuntimeClassFlags<WinRt>, Share::DefaultIDataTransferManager, FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.DataTransfer.DataTransferManager", BaseTrust);
    EventSource<__FITypedEventHandler_2_Windows__CApplicationModel__CDataTransfer__CDataTransferManager_Windows__CApplicationModel__CDataTransfer__CDataRequestedEventArgs>
        requested_;
    EventSource<__FITypedEventHandler_2_Windows__CApplicationModel__CDataTransfer__CDataTransferManager_Windows__CApplicationModel__CDataTransfer__CTargetApplicationChosenEventArgs>
        chosen_;

public:
    HRESULT STDMETHODCALLTYPE add_DataRequested(
        __FITypedEventHandler_2_Windows__CApplicationModel__CDataTransfer__CDataTransferManager_Windows__CApplicationModel__CDataTransfer__CDataRequestedEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return requested_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_DataRequested(EventRegistrationToken token) override { return requested_.Remove(token); }
    HRESULT STDMETHODCALLTYPE add_TargetApplicationChosen(
        __FITypedEventHandler_2_Windows__CApplicationModel__CDataTransfer__CDataTransferManager_Windows__CApplicationModel__CDataTransfer__CTargetApplicationChosenEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return chosen_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_TargetApplicationChosen(EventRegistrationToken token) override {
        return chosen_.Remove(token);
    }
};
class ShareFactory final : public Factory<Share::DefaultIDataTransferManagerStatics> {
public:
    ShareFactory() : Factory(L"Windows.ApplicationModel.DataTransfer.DataTransferManager") {}
    HRESULT STDMETHODCALLTYPE GetForCurrentView(Share::IDataTransferManager** value) override {
        if (!value) return E_POINTER;
        static auto manager = Make<DataTransferManager>();
        return manager ? manager.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE ShowShareUI() override {
        Log("INFO", "the Share charm is not available outside Windows 8; nothing shared");
        return S_OK;
    }
};

class SettingsPane final : public RuntimeClass<RuntimeClassFlags<WinRt>, Settings::DefaultISettingsPane, FtmBase> {
    InspectableClass(L"Windows.UI.ApplicationSettings.SettingsPane", BaseTrust);
    EventSource<__FITypedEventHandler_2_Windows__CUI__CApplicationSettings__CSettingsPane_Windows__CUI__CApplicationSettings__CSettingsPaneCommandsRequestedEventArgs>
        requested_;

public:
    HRESULT STDMETHODCALLTYPE add_CommandsRequested(
        __FITypedEventHandler_2_Windows__CUI__CApplicationSettings__CSettingsPane_Windows__CUI__CApplicationSettings__CSettingsPaneCommandsRequestedEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return requested_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_CommandsRequested(EventRegistrationToken token) override {
        return requested_.Remove(token);
    }
};
class SettingsFactory final : public Factory<Settings::DefaultISettingsPaneStatics> {
public:
    SettingsFactory() : Factory(L"Windows.UI.ApplicationSettings.SettingsPane") {}
    HRESULT STDMETHODCALLTYPE GetForCurrentView(Settings::ISettingsPane** value) override {
        if (!value) return E_POINTER;
        static auto pane = Make<SettingsPane>();
        return pane ? pane.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE Show() override {
        Log("INFO", "the Settings charm is not available outside Windows 8; not shown");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Edge(Settings::SettingsEdgeLocation* value) override {
        if (!value) return E_POINTER;
        *value = Settings::SettingsEdgeLocation_Right;
        return S_OK;
    }
};

class TileUpdater final : public RuntimeClass<RuntimeClassFlags<WinRt>, Tiles::DefaultITileUpdater, FtmBase> {
    InspectableClass(L"Windows.UI.Notifications.TileUpdater", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE Update(Tiles::ITileNotification*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Clear() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EnableNotificationQueue(boolean) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Setting(Tiles::NotificationSetting* value) override {
        if (!value) return E_POINTER;
        *value = Tiles::NotificationSetting_DisabledForApplication;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AddToSchedule(Tiles::IScheduledTileNotification*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RemoveFromSchedule(Tiles::IScheduledTileNotification*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE StartPeriodicUpdate(ABI::Windows::Foundation::IUriRuntimeClass*,
                                                  Tiles::PeriodicUpdateRecurrence) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE StartPeriodicUpdateAtTime(ABI::Windows::Foundation::IUriRuntimeClass*,
                                                        ABI::Windows::Foundation::DateTime,
                                                        Tiles::PeriodicUpdateRecurrence) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE StopPeriodicUpdate() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE StartPeriodicUpdateBatch(__FIIterable_1_Windows__CFoundation__CUri*,
                                                       Tiles::PeriodicUpdateRecurrence) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE StartPeriodicUpdateBatchAtTime(__FIIterable_1_Windows__CFoundation__CUri*,
                                                             ABI::Windows::Foundation::DateTime,
                                                             Tiles::PeriodicUpdateRecurrence) override {
        return S_OK;
    }
};
class TileFactory final : public Factory<Tiles::DefaultITileUpdateManagerStatics> {
    HRESULT Updater(Tiles::ITileUpdater** value) {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto updater = Make<TileUpdater>();
        return updater ? updater.CopyTo(value) : E_OUTOFMEMORY;
    }

public:
    TileFactory() : Factory(L"Windows.UI.Notifications.TileUpdateManager") {}
    HRESULT STDMETHODCALLTYPE CreateTileUpdaterForApplication(Tiles::ITileUpdater** value) override {
        return Updater(value);
    }
    HRESULT STDMETHODCALLTYPE CreateTileUpdaterForApplicationWithId(HSTRING, Tiles::ITileUpdater** value) override {
        return Updater(value);
    }
    HRESULT STDMETHODCALLTYPE CreateTileUpdaterForSecondaryTile(HSTRING, Tiles::ITileUpdater** value) override {
        return Updater(value);
    }
    // Tile XML templates need no tile; Windows answers.
    HRESULT STDMETHODCALLTYPE GetTemplateContent(Tiles::TileTemplateType type,
                                                 ABI::Windows::Data::Xml::Dom::IXmlDocument** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        ComPtr<Tiles::ITileUpdateManagerStatics> real;
        HRESULT hr = OsGetActivationFactory(
            Wrappers::HStringReference(RuntimeClass_Windows_UI_Notifications_TileUpdateManager).Get(),
            IID_PPV_ARGS(&real));
        return SUCCEEDED(hr) ? real->GetTemplateContent(type, value) : hr;
    }
};
template <class T> HRESULT Shared(IActivationFactory** value) {
    static auto factory = Make<T>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
HRESULT MakeShareFactory(IActivationFactory** value) { return Shared<ShareFactory>(value); }
HRESULT MakeSettingsPaneFactory(IActivationFactory** value) { return Shared<SettingsFactory>(value); }
HRESULT MakeTileFactory(IActivationFactory** value) { return Shared<TileFactory>(value); }
}
