#include "registry.h"
#include "islands/islands.h"
#include "log.h"
#include "version.h"
#include "compat/win10/xaml.h"
#include <winstring.h>
#include <new>

extern "C" HRESULT WINAPI ShimGetFactory(HSTRING name, IActivationFactory** factory) noexcept {
    if (!factory) return E_POINTER;
    *factory = nullptr;
    const wchar_t* text = WindowsGetStringRawBuffer(name, nullptr);
    try {
        if (appx::IslandsMode()) {
            auto hr = appx::MakeIslandFactory(text, factory);
            if (hr != CLASS_E_CLASSNOTAVAILABLE) return hr;
            if (wcscmp(text, L"Windows.ApplicationModel.Core.CoreApplication") == 0)
                return appx::MakeCoreApplicationFactory(factory);
            if (wcscmp(text, L"Windows.Storage.StorageFile") == 0) return appx::MakeStorageFileFactory(factory);
            if (wcscmp(text, L"Windows.Storage.ApplicationData") != 0 &&
                wcscmp(text, L"Windows.ApplicationModel.Package") != 0 &&
                wcscmp(text, L"Windows.ApplicationModel.Store.CurrentApp") != 0 &&
                wcscmp(text, L"Windows.UI.ApplicationSettings.SettingsPane") != 0 &&
                wcscmp(text, L"Windows.UI.Notifications.TileUpdateManager") != 0 &&
                wcscmp(text, L"Windows.UI.Notifications.BadgeUpdateManager") != 0 &&
                wcscmp(text, L"Windows.UI.StartScreen.SecondaryTile") != 0 &&
                wcscmp(text, L"Windows.UI.Notifications.ToastNotificationManager") != 0 &&
                wcscmp(text, L"Windows.UI.Notifications.ScheduledToastNotification") != 0 &&
                wcsncmp(text, L"Microsoft.Xbox.", 15) != 0)
                return CLASS_E_CLASSNOTAVAILABLE;
        }
        if (wcscmp(text, L"Windows.System.MemoryManager") == 0) return appx::MakeMemoryManagerFactory(factory);
        if (wcscmp(text, L"Windows.UI.Xaml.Controls.Grid") == 0) return appx::MakeWin10GridFactory(factory);
        if (wcscmp(text, L"Windows.Globalization.Calendar") == 0) return appx::MakeCalendarFactory(factory);
        if (wcscmp(text, L"Windows.Foundation.Diagnostics.AsyncCausalityTracer") == 0)
            return appx::MakeCausalityFactory(factory);
        if (wcscmp(text, L"Windows.UI.Notifications.ToastNotificationManager") == 0)
            return appx::MakeToastFactory(factory);
        if (wcscmp(text, L"Windows.UI.Notifications.ScheduledToastNotification") == 0)
            return appx::MakeScheduledToastFactory(factory);
        if (wcscmp(text, L"Windows.System.Display.DisplayRequest") == 0) return appx::MakeDeviceFactory(true, factory);
        if (wcscmp(text, L"Windows.Security.ExchangeActiveSyncProvisioning.EasClientDeviceInformation") == 0)
            return appx::MakeDeviceFactory(false, factory);
        if (wcscmp(text, L"Windows.ApplicationModel.Store.CurrentApp") == 0) return appx::MakeStoreFactory(factory);
        if (wcscmp(text, L"Windows.Storage.Streams.DataReader") == 0) return appx::MakeDataReaderFactory(factory);
        if (wcscmp(text, L"Windows.UI.ViewManagement.ApplicationView") == 0)
            return appx::MakeApplicationViewFactory(factory);
        if (wcscmp(text, L"Windows.System.Profile.HardwareIdentification") == 0)
            return appx::MakeHardwareFactory(factory);
        if (wcscmp(text, L"Windows.Networking.Connectivity.NetworkInformation") == 0)
            return appx::MakeNetworkFactory(factory);
        if (wcscmp(text, L"Windows.Web.Http.Filters.HttpBaseProtocolFilter") == 0)
            return appx::MakeHttpFactory(true, factory);
        if (wcscmp(text, L"Windows.Web.Http.HttpClient") == 0) return appx::MakeHttpFactory(false, factory);
        if (wcscmp(text, L"VungleSDK.AdFactory") == 0) return appx::MakeAdFactory(factory);
        if (wcscmp(text, L"Windows.Storage.ApplicationData") == 0) return appx::MakeStorageFactory(factory);
        if (wcscmp(text, L"Windows.Storage.StorageFile") == 0) return appx::MakeStorageFileFactory(factory);
        if (wcscmp(text, L"Windows.UI.Input.PointerVisualizationSettings") == 0)
            return appx::MakeVisualizationFactory(factory);
        if (wcscmp(text, L"Windows.UI.Xaml.Window") == 0) return appx::MakeWindowFactory(factory);
        if (wcscmp(text, L"Windows.UI.Xaml.Media.RectangleGeometry") == 0)
            return appx::MakeRectangleGeometryFactory(factory);
        if (wcscmp(text, L"Windows.Graphics.Display.DisplayInformation") == 0 ||
            wcscmp(text, L"Windows.Graphics.Display.DisplayProperties") == 0)
            return appx::MakeDisplayFactory(factory);
        if (wcscmp(text, L"Windows.ApplicationModel.Core.CoreApplication") == 0)
            return appx::MakeCoreApplicationFactory(factory);
        if (wcscmp(text, L"Windows.UI.Xaml.Controls.Frame") == 0 ||
            wcscmp(text, L"Windows.UI.Xaml.Controls.Page") == 0 ||
            wcscmp(text, L"Windows.UI.Xaml.Controls.UserControl") == 0)
            return appx::MakeControlFactory(text, factory);
        if (wcscmp(text, L"Windows.UI.Xaml.Application") == 0) return appx::MakeApplicationFactory(factory);
        if (wcscmp(text, L"Windows.UI.Xaml.DispatcherTimer") == 0) return appx::MakeDispatcherTimerFactory(factory);
        if (wcscmp(text, L"Windows.UI.Xaml.Media.CompositionTarget") == 0)
            return appx::MakeCompositionTargetFactory(factory);
        if (wcscmp(text, L"Windows.UI.Core.CoreCursor") == 0) return appx::MakeCursorFactory(factory);
        if (wcscmp(text, L"Windows.UI.Core.CoreWindow") == 0) return appx::MakeCoreWindowFactory(factory);
        if (wcscmp(text, L"Windows.ApplicationModel.Package") == 0) return appx::MakePackageFactory(factory);
        if (wcscmp(text, L"Windows.ApplicationModel.DataTransfer.DataTransferManager") == 0)
            return appx::MakeShareFactory(factory);
        if (wcscmp(text, L"Windows.UI.ApplicationSettings.SettingsPane") == 0)
            return appx::MakeSettingsPaneFactory(factory);
        if (wcscmp(text, L"Windows.UI.Popups.MessageDialog") == 0) return appx::MakeMessageDialogFactory(factory);
        if (wcscmp(text, L"Windows.UI.Notifications.TileUpdateManager") == 0) return appx::MakeTileFactory(factory);
        if (wcscmp(text, L"Windows.UI.Notifications.BadgeUpdateManager") == 0) return appx::MakeBadgeFactory(factory);
        if (wcscmp(text, L"Windows.UI.StartScreen.SecondaryTile") == 0) return appx::MakeSecondaryTileFactory(factory);
        if (wcsncmp(text, L"Microsoft.Xbox.", 15) == 0) return appx::MakeXboxFactory(text, factory);
        if (wcsncmp(text, L"HttpClientCSharp.", 17) == 0) return appx::MakeHttpClientCSharpFactory(text, factory);
        if (wcscmp(text, L"Windows.System.Threading.ThreadPoolTimer") == 0)
            return appx::MakeThreadPoolFactory(true, factory);
        if (wcscmp(text, L"Windows.System.Threading.ThreadPool") == 0)
            return appx::MakeThreadPoolFactory(false, factory);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}
extern "C" HRESULT WINAPI DllGetActivationFactory(HSTRING name, IActivationFactory** value) noexcept {
    return ShimGetFactory(name, value);
}
extern "C" HRESULT WINAPI DllCanUnloadNow() { return S_FALSE; }
extern "C" void __cdecl ShimBootstrap() noexcept {}
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // Opens each run's log and names the build, so a log shows which DLL ran.
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        appx::Log("INFO", "==== appxportable attached version=%s commit=%s pid=%lu exe=%ls ====", APPX_PORTABLE_VERSION,
                  APPX_PORTABLE_COMMIT, GetCurrentProcessId(), path);
        appx::InstallActivationHook();
    }
    return TRUE;
}
