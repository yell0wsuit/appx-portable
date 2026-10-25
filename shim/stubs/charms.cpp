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
#include "host/host.h"
#include <windows.ui.popups.h>
#include <windows.ui.startscreen.h>
#include <shobjidl.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.ApplicationSettings.h>
#include <winrt/Windows.UI.Popups.h>
using namespace Microsoft::WRL;
namespace appx {
namespace {
namespace Share = ABI::Windows::ApplicationModel::DataTransfer;
namespace Settings = ABI::Windows::UI::ApplicationSettings;
namespace Tiles = ABI::Windows::UI::Notifications;
// The system dialog works on desktop when explicitly associated with an HWND.
class MessageDialogFactory final : public Factory<ABI::Windows::UI::Popups::IMessageDialogFactory> {
    HRESULT MakeDialog(HSTRING content, HSTRING title, bool withTitle,
                       ABI::Windows::UI::Popups::IMessageDialog** value) {
        if (!value) return E_POINTER;
        *value = nullptr;
        ComPtr<ABI::Windows::UI::Popups::IMessageDialogFactory> factory;
        HRESULT hr = OsGetActivationFactory(Wrappers::HStringReference(L"Windows.UI.Popups.MessageDialog").Get(),
                                            IID_PPV_ARGS(&factory));
        ComPtr<ABI::Windows::UI::Popups::IMessageDialog> dialog;
        if (SUCCEEDED(hr))
            hr = withTitle ? factory->CreateWithTitle(content, title, &dialog) : factory->Create(content, &dialog);
        ComPtr<IInitializeWithWindow> owner;
        if (SUCCEEDED(hr)) hr = dialog.As(&owner);
        if (SUCCEEDED(hr)) hr = owner->Initialize(HostWindow());
        return SUCCEEDED(hr) ? dialog.CopyTo(value) : hr;
    }

public:
    MessageDialogFactory() : Factory(L"Windows.UI.Popups.MessageDialog") {}
    HRESULT STDMETHODCALLTYPE Create(HSTRING content, ABI::Windows::UI::Popups::IMessageDialog** value) override {
        return MakeDialog(content, nullptr, false, value);
    }
    HRESULT STDMETHODCALLTYPE CreateWithTitle(HSTRING content, HSTRING title,
                                              ABI::Windows::UI::Popups::IMessageDialog** value) override {
        return MakeDialog(content, title, true, value);
    }
};

class DataTransferManager final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, Share::DefaultIDataTransferManager, FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.DataTransfer.DataTransferManager", BaseTrust);
    EventSource<
        __FITypedEventHandler_2_Windows__CApplicationModel__CDataTransfer__CDataTransferManager_Windows__CApplicationModel__CDataTransfer__CDataRequestedEventArgs>
        requested_;
    EventSource<
        __FITypedEventHandler_2_Windows__CApplicationModel__CDataTransfer__CDataTransferManager_Windows__CApplicationModel__CDataTransfer__CTargetApplicationChosenEventArgs>
        chosen_;

public:
    HRESULT STDMETHODCALLTYPE add_DataRequested(
        __FITypedEventHandler_2_Windows__CApplicationModel__CDataTransfer__CDataTransferManager_Windows__CApplicationModel__CDataTransfer__CDataRequestedEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return requested_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_DataRequested(EventRegistrationToken token) override {
        return requested_.Remove(token);
    }
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

namespace ProjectedSettings = winrt::Windows::UI::ApplicationSettings;
struct SettingsRequest : winrt::implements<SettingsRequest, ProjectedSettings::ISettingsPaneCommandsRequest> {
    winrt::Windows::Foundation::Collections::IVector<ProjectedSettings::SettingsCommand> commands =
        winrt::single_threaded_vector<ProjectedSettings::SettingsCommand>();
    auto ApplicationCommands() { return commands; }
};
struct SettingsArgs : winrt::implements<SettingsArgs, ProjectedSettings::ISettingsPaneCommandsRequestedEventArgs> {
    ProjectedSettings::SettingsPaneCommandsRequest request =
        winrt::make<SettingsRequest>().as<ProjectedSettings::SettingsPaneCommandsRequest>();
    auto Request() { return request; }
};
class SettingsPane final : public RuntimeClass<RuntimeClassFlags<WinRt>, Settings::DefaultISettingsPane, FtmBase> {
    InspectableClass(L"Windows.UI.ApplicationSettings.SettingsPane", BaseTrust);
    EventSource<
        __FITypedEventHandler_2_Windows__CUI__CApplicationSettings__CSettingsPane_Windows__CUI__CApplicationSettings__CSettingsPaneCommandsRequestedEventArgs>
        requested_;

public:
    HRESULT Show() {
        // Request a fresh list on every opening, on the game's UI thread.
        // The SDK collection supplies the full WinRT vector/iterator contract.
        static bool showing;
        if (showing) return S_OK;
        showing = true;
        struct Reset {
            bool& value;
            ~Reset() { value = false; }
        } reset{showing};
        try {
            auto args = winrt::make<SettingsArgs>();
            auto hr = requested_.InvokeAll(
                this, reinterpret_cast<Settings::ISettingsPaneCommandsRequestedEventArgs*>(winrt::get_abi(args)));
            if (FAILED(hr)) return hr;
            auto commands = args.Request().ApplicationCommands();
            HMENU menu = CreatePopupMenu();
            if (!menu) return HRESULT_FROM_WIN32(GetLastError());
            struct CloseMenu {
                HMENU value;
                ~CloseMenu() { DestroyMenu(value); }
            } closeMenu{menu};
            for (uint32_t i = 0; i < commands.Size(); ++i) {
                auto command = commands.GetAt(i).as<winrt::Windows::UI::Popups::IUICommand>();
                auto label = command.Label();
                if (!AppendMenuW(menu, MF_STRING, i + 1, label.c_str())) return HRESULT_FROM_WIN32(GetLastError());
            }
            if (!commands.Size()) AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"No app settings available");
            RECT client{};
            GetClientRect(HostWindow(), &client);
            POINT anchor{client.right, 0};
            ClientToScreen(HostWindow(), &anchor);
            Log("TRACE", "settings menu open (%u commands)", commands.Size());
            auto selected = static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_NONOTIFY,
                                                               anchor.x, anchor.y, HostWindow(), nullptr));
            Log("TRACE", "settings menu closed selected=%u", selected);
            showing = false;
            if (selected && selected <= commands.Size()) {
                auto command = commands.GetAt(selected - 1).as<winrt::Windows::UI::Popups::IUICommand>();
                auto invoked = command.Invoked();
                if (invoked) invoked(command);
            }
            return S_OK;
        } catch (...) {
            return winrt::to_hresult();
        }
    }
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
static ComPtr<SettingsPane> CurrentSettingsPane() {
    static auto pane = Make<SettingsPane>();
    return pane;
}
class SettingsFactory final : public Factory<Settings::DefaultISettingsPaneStatics> {
public:
    SettingsFactory() : Factory(L"Windows.UI.ApplicationSettings.SettingsPane") {}
    HRESULT STDMETHODCALLTYPE GetForCurrentView(Settings::ISettingsPane** value) override {
        if (!value) return E_POINTER;
        auto pane = CurrentSettingsPane();
        return pane ? pane.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE Show() override { return ShowGameSettings(); }
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
class BadgeUpdater final : public RuntimeClass<RuntimeClassFlags<WinRt>, Tiles::IBadgeUpdater, FtmBase> {
    InspectableClass(L"Windows.UI.Notifications.BadgeUpdater", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE Update(Tiles::IBadgeNotification*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Clear() override { return S_OK; }
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
};
class BadgeFactory final : public Factory<Tiles::IBadgeUpdateManagerStatics> {
public:
    BadgeFactory() : Factory(L"Windows.UI.Notifications.BadgeUpdateManager") {}
    HRESULT STDMETHODCALLTYPE CreateBadgeUpdaterForApplication(Tiles::IBadgeUpdater** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto updater = Make<BadgeUpdater>();
        return updater ? updater.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE CreateBadgeUpdaterForApplicationWithId(HSTRING, Tiles::IBadgeUpdater** value) override {
        return CreateBadgeUpdaterForApplication(value);
    }
    HRESULT STDMETHODCALLTYPE CreateBadgeUpdaterForSecondaryTile(HSTRING, Tiles::IBadgeUpdater** value) override {
        return CreateBadgeUpdaterForApplication(value);
    }
    HRESULT STDMETHODCALLTYPE GetTemplateContent(Tiles::BadgeTemplateType type,
                                                 ABI::Windows::Data::Xml::Dom::IXmlDocument** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        ComPtr<Tiles::IBadgeUpdateManagerStatics> real;
        auto hr = OsGetActivationFactory(
            Wrappers::HStringReference(L"Windows.UI.Notifications.BadgeUpdateManager").Get(), IID_PPV_ARGS(&real));
        return SUCCEEDED(hr) ? real->GetTemplateContent(type, value) : hr;
    }
};
namespace Start = ABI::Windows::UI::StartScreen;
using TileSearch = __FIAsyncOperation_1___FIVectorView_1_Windows__CUI__CStartScreen__CSecondaryTile;
// Querying a pin is harmless without a package: there are no portable Start
// tiles. Leave explicit pin creation/enumeration to Windows as before.
class SecondaryTileFactory final : public Factory<Start::ISecondaryTileStatics> {
    using Base = Factory<Start::ISecondaryTileStatics>;
    static HRESULT Real(Start::ISecondaryTileStatics** value) {
        return OsGetActivationFactory(Wrappers::HStringReference(L"Windows.UI.StartScreen.SecondaryTile").Get(),
                                      IID_PPV_ARGS(value));
    }

public:
    SecondaryTileFactory() : Base(L"Windows.UI.StartScreen.SecondaryTile") {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) noexcept override {
        auto hr = Base::QueryInterface(iid, value);
        return hr == E_NOINTERFACE
                   ? OsGetActivationFactory(Wrappers::HStringReference(L"Windows.UI.StartScreen.SecondaryTile").Get(),
                                            iid, value)
                   : hr;
    }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        return OsActivateInstance(Wrappers::HStringReference(L"Windows.UI.StartScreen.SecondaryTile").Get(), value);
    }
    HRESULT STDMETHODCALLTYPE Exists(HSTRING, boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FindAllAsync(TileSearch** value) override {
        ComPtr<Start::ISecondaryTileStatics> real;
        auto hr = Real(&real);
        return SUCCEEDED(hr) ? real->FindAllAsync(value) : hr;
    }
    HRESULT STDMETHODCALLTYPE FindAllForApplicationAsync(HSTRING id, TileSearch** value) override {
        ComPtr<Start::ISecondaryTileStatics> real;
        auto hr = Real(&real);
        return SUCCEEDED(hr) ? real->FindAllForApplicationAsync(id, value) : hr;
    }
    HRESULT STDMETHODCALLTYPE FindAllForPackageAsync(TileSearch** value) override {
        ComPtr<Start::ISecondaryTileStatics> real;
        auto hr = Real(&real);
        return SUCCEEDED(hr) ? real->FindAllForPackageAsync(value) : hr;
    }
};
template <class T> HRESULT Shared(IActivationFactory** value) {
    static auto factory = Make<T>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
HRESULT MakeShareFactory(IActivationFactory** value) { return Shared<ShareFactory>(value); }
HRESULT MakeSettingsPaneFactory(IActivationFactory** value) { return Shared<SettingsFactory>(value); }
HRESULT ShowGameSettings() {
    return PostHostWork(0, [] {
        auto pane = CurrentSettingsPane();
        auto hr = pane ? pane->Show() : E_OUTOFMEMORY;
        if (FAILED(hr)) Log("FAIL", "game settings hr=%08lx", hr);
    });
}
HRESULT MakeTileFactory(IActivationFactory** value) { return Shared<TileFactory>(value); }
HRESULT MakeBadgeFactory(IActivationFactory** value) { return Shared<BadgeFactory>(value); }
HRESULT MakeSecondaryTileFactory(IActivationFactory** value) { return Shared<SecondaryTileFactory>(value); }
HRESULT MakeMessageDialogFactory(IActivationFactory** value) { return Shared<MessageDialogFactory>(value); }
}
