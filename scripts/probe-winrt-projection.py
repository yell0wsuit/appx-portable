#!/usr/bin/env python3
"""Verify metadata generation and a real Windows.dll ABI projection under Wine Mono."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import struct
import subprocess

# IIDs and vtable slots taken from the Windows SDK's ABI headers
# (windows.applicationmodel.h, windows.applicationmodel.activation.h, windows.ui.xaml.h,
# windows.ui.xaml.media.animation.h),
# independent of the metadata the generator reads. Slots follow IInspectable's six.
# WPF backs Windows.UI.Xaml for managed games under Wine: every Windows.dll built
# from the bootstrap selection compiles these, against Wine Mono's WPF.
XAML_SOURCES = ("xaml/host.cs", "xaml/peers.cs", "xaml/values.cs", "xaml/properties.cs", "xaml/tree.cs", "xaml/events.cs", "xaml/navigation.cs", "xaml/animation.cs", "xaml/loader.cs", "xaml/states.cs", "xaml/markup.cs", "xaml/controls.cs", "xaml/images.cs")
XAML_THEME, XAML_THEME_NAME = "xaml/theme.xaml", "AppxPortable.WinRT.Xaml.theme.xaml"
BOOTSTRAP_GENERATED = ("Windows.generated.cs", "Windows.interop.cs", "Windows.xbox-interop.cs", "Mutated.generated.cs")


def wpf_references(mono):
    gac = mono / "gac"
    return [gac / n / "4.0.0.0__31bf3856ad364e35" / (n + ".dll") for n in ("PresentationCore", "PresentationFramework", "WindowsBase")] + \
        [gac / "System.Xaml/4.0.0.0__b77a5c561934e089/System.Xaml.dll"]


HEADER_IIDS = {
    "IPackageStatics": "4e534bdf-2960-4878-97a4-9624deb72f2d", "IPackage": "163c792f-bd75-413c-bf23-b1fe7b95d825",
    "IPackageId": "1adb665e-37c7-4790-9980-dd7ae74e8bb2", "ILaunchActivatedEventArgs": "fbc93e26-a14a-4b4f-82b0-33bed920af52",
    "IApplication": "74b861a1-7487-46a9-9a6e-c78b512726c5", "IApplicationStatics": "06499997-f7b4-45fe-b763-7577d1d3cb4a",
    "IApplicationFactory": "93bbe361-be5a-4ee3-b4a3-95118dc97a89", "IApplicationOverrides": "25f99ff7-9347-459a-9fac-b2d0e11c1a0f",
    "IApplicationInitializationCallback": "b6351c55-c284-46e4-8310-fb0967fab76f",
    "IApplicationInitializationCallbackParams": "751b792e-5772-4488-8b87-f547faa64474",
    "IActivatedEventArgs": "cf651713-cd08-4fd8-b697-a281b6544e2e", "IWindowStatics": "93328409-4ea1-4afa-83dc-0c4e73e88bb1",
    "IWindow": "3276167d-c9f6-462d-9de2-ae4c1fd8c2e5", "IFrameFactory": "02ee93d4-448e-469e-9799-0a8a1f70f171",
    "IFrame": "68ea500b-3fe9-4735-b6a0-e7601933b089", "INavigate": "bf2195a9-f4ea-4336-977c-f8fcf78b0d9e",
    "IXamlMetadataProvider": "b3765d69-68a5-4b32-8861-fdb90c1f5836", "IXamlType": "7920eab1-a2e5-479a-bd50-6cef3c0b4970",
    "IXamlMember": "c541f58c-43a9-4216-b718-e0b11b14e93e", "IVisualTreeHelperStatics": "e75758c4-d25d-4b1d-971f-596f17f12baa",
    "IWindowSizeChangedEventHandler": "5c21c742-2ced-4fd9-ba38-7118d40e966b",
    "IDispatchedHandler": "d1f276c4-98d8-4636-bf49-eb79507548e9",
    "IDataTransferManager": "a5caee9b-8708-49d1-8d36-67d25a8da00c", "IDataTransferManagerStatics": "a9da01aa-e00e-4cfe-aa44-2dd932dca3d8",
    "TypedEventHandler<DataTransferManager, DataRequestedEventArgs>": "ec6f9cc8-46d0-5e0e-b4d2-7d7773ae37a0",
    "IWindowSizeChangedEventArgs": "5a200ec7-0426-47dc-b86c-6f475915e451",
    "IDependencyObject": "5c526665-f60e-4912-af59-5fe0680f089d",
    "IDependencyProperty": "85b13970-9bc4-4e96-acf1-30c8fd3d55c8",
    "IDependencyPropertyStatics": "49e5f28f-8259-4d5c-aae0-83d56dbb68d9",
    "IPropertyMetadata": "814ef30d-8d18-448a-8644-f2cb51e70380",
    "ICoreWindowStatic": "4d239005-3c2a-41b1-9022-536bb9cf93b1", "ICoreWindow": "79b9d5f2-879e-4b89-b798-79e47598030c",
    "ICoreDispatcher": "60db2fa8-b705-4fde-a7d6-ebbb1891d39e",
    "IApplicationDataStatics": "5612147b-e843-45e3-94d8-06169e3c8e17", "IApplicationData": "c3da6fb7-b744-4b45-b0b8-223a0938d0dc",
    "IApplicationDataContainer": "c5aefd1e-f467-40ba-8566-ab640a441e1d", "IPropertySet": "8a43ed9f-f4e6-4421-acf9-1dab2986820c",
    "ISplashScreen": "ca4d975c-d4d6-43f0-97c0-0833c6391c24",
    "IResourceDictionary": "c1ea4f24-d6de-4191-8e3a-f48601f7489c", "IResourceDictionaryFactory": "ea3639b5-31b7-4271-92c9-7c9584a91c22",
    "ITimeline": "0bc465dc-be4d-4d0d-9549-2208b715f40d", "IStoryboard": "d45c1e6e-3594-460e-981a-32271bd3aa06",
    "IComponentConnector": "f6790987-e6e5-47f2-92c6-eccce4ba159a",
    "IUriRuntimeClass": "9e365e57-48b2-4160-956f-c7385120bbfc", "IUriRuntimeClassFactory": "44a9796f-723e-4fdf-a218-033e75b0c084",
    "IViewbox": "05252c58-ba9d-4809-9ec3-fa0d16710ba1",
    "IGrid": "fd104460-2e15-4ba3-8b8f-fa693a4161e9", "IPanel": "a50a4bbd-8361-469c-90da-e9a40c7474df",
    "ITextBlock": "ae2d9271-3b4a-45fc-8468-f7949548f4d5",
    "IProgressRing": "6da5e49e-6e9d-425c-bd7c-02173e39763f",
    "IImage": "495b7402-9af3-4e50-aa90-03388f3086d2",
    "IPage": "c4b27075-e641-453e-824d-012fc7cf95cf", "IPageFactory": "dfa149ac-1849-445e-937c-40a9590cc076",
    "IPageOverrides": "230b9cb8-3419-4306-aabe-4a8e680d3627", "IUserControl": "a7a69ec9-ea35-4679-bf29-f4f09286d314",
    "IControlOverrides": "a09691df-9824-41fe-b530-b0d8990e64c1", "IKeyRoutedEventArgs": "d4cd3dfe-4079-42e9-a39a-3095d3f049c6",
    "IContentControl": "a26dd1dc-cd44-435c-be94-01d6241c231c",
    "IDispatcherTimer": "d160ce46-cd22-4f5f-8c97-40e61da3e2dc", "EventHandler<IInspectable>": "c50898f6-c536-5f47-8583-8b2c2438a13b",
    "INavigationEventArgs": "b6aa9834-6691-44d1-bdf7-58820c27b0d0", "INavigatingCancelEventArgs": "fd1d67ae-eafb-4079-be80-6dc92a03aedf",
    "INetworkInformationStatics": "5074f851-950d-4165-9c15-365619481eea",
    "IConnectionProfile": "71ba143c-598e-49d0-84eb-8febaedcc195", "INetworkStatusChangedEventHandler": "71ba143f-598e-49d0-84eb-8febaedcc195",
    "IDesignModeStatics": "2c3893cc-f81a-4e7a-b857-76a80887e185", "IRoutedEventHandler": "a856e674-b0b6-4bc3-bba8-1ba06e40d4b5",
    "IWindowActivatedEventHandler": "18026348-8619-4c7b-b534-ced45d9de219", "IWindowActivatedEventArgs": "179d65e7-4658-4cb6-aa13-41d094ea255e"}
HEADER_SLOTS = [
    "Windows.ApplicationModel.IDesignModeStatics::get_DesignModeEnabled, slot 6",
    "Windows.UI.Xaml.IFrameworkElement::get_ActualWidth, slot 13", "Windows.UI.Xaml.IFrameworkElement::get_ActualHeight, slot 14",
    "Windows.UI.Xaml.IFrameworkElement::add_Loaded, slot 43", "Windows.UI.Xaml.IFrameworkElement::remove_Loaded, slot 44",
    "Windows.UI.Xaml.IFrameworkElement::add_Unloaded, slot 45", "Windows.UI.Xaml.IFrameworkElement::remove_Unloaded, slot 46",
    "Windows.UI.Xaml.IWindow::get_Bounds, slot 6", "Windows.UI.Xaml.IWindow::get_CoreWindow, slot 10",
    "Windows.UI.Xaml.IWindow::add_Activated, slot 12", "Windows.UI.Xaml.IWindow::remove_Activated, slot 13",
    "Windows.UI.Core.IWindowActivatedEventArgs::get_WindowActivationState, slot 6",
    "Windows.UI.Xaml.Controls.IPage::get_NavigationCacheMode, slot 7", "Windows.UI.Xaml.Controls.IPage::put_NavigationCacheMode, slot 8",
    "Windows.UI.Xaml.Controls.INavigate::Navigate, slot 6", "Windows.UI.Xaml.Controls.IFrame::Navigate, slot 24",
    "Windows.UI.Xaml.IFrameworkElement::get_Style, slot 38", "Windows.UI.Xaml.IFrameworkElement::put_Style, slot 39",
    "Windows.UI.Xaml.Media.IVisualTreeHelperStatics::GetChild, slot 10",
    "Windows.UI.Xaml.Media.IVisualTreeHelperStatics::GetChildrenCount, slot 11",
    "Windows.UI.Xaml.IWindow::add_SizeChanged, slot 16",
    "Windows.UI.Xaml.IWindow::remove_SizeChanged, slot 17",
    "Windows.UI.Xaml.Controls.ITextBlock::get_Text, slot 26",
    "Windows.UI.Xaml.Controls.ITextBlock::put_Text, slot 27",
    "Windows.UI.Xaml.Controls.IProgressRing::get_IsActive, slot 6",
    "Windows.UI.Xaml.Controls.IProgressRing::put_IsActive, slot 7",
    "Windows.UI.Xaml.Media.Animation.IStoryboard::Begin, slot 9",
    "Windows.ApplicationModel.IPackageStatics::get_Current, slot 6", "Windows.ApplicationModel.IPackage::get_Id, slot 6",
    "Windows.ApplicationModel.IPackageId::get_Name, slot 6", "Windows.ApplicationModel.IPackageId::get_Architecture, slot 8",
    "Windows.ApplicationModel.IPackageId::get_ResourceId, slot 9", "Windows.ApplicationModel.IPackageId::get_Publisher, slot 10",
    "Windows.ApplicationModel.Activation.ILaunchActivatedEventArgs::get_Arguments, slot 6",
    "Windows.UI.Xaml.IApplicationFactory::CreateInstance, slot 6", "Windows.UI.Xaml.IApplicationStatics::get_Current, slot 6",
    "Windows.UI.Xaml.IApplicationStatics::LoadComponent, slot 8",
    "Windows.UI.Xaml.IApplicationStatics::LoadComponentWithResourceLocation, slot 9",
    "Windows.UI.Xaml.IFrameworkElement::FindName, slot 51",
    "Windows.UI.Xaml.IFrameworkElement::get_Resources, slot 7", "Windows.UI.Xaml.IFrameworkElement::put_Resources, slot 8",
    "Windows.UI.Xaml.IResourceDictionaryFactory::CreateInstance, slot 6",
    "Windows.UI.Xaml.IApplicationStatics::Start, slot 7", "Windows.UI.Xaml.IApplication::Exit, slot 17",
    "Windows.UI.Xaml.IApplicationOverrides::OnLaunched, slot 7", "Windows.UI.Xaml.IApplicationOverrides::OnActivated, slot 6",
    "Windows.ApplicationModel.Activation.IActivatedEventArgs::get_Kind, slot 6",
    "Windows.ApplicationModel.Activation.IActivatedEventArgs::get_PreviousExecutionState, slot 7",
    "Windows.UI.Xaml.IApplication::add_Suspending, slot 13", "Windows.UI.Xaml.IApplication::remove_Suspending, slot 14",
    "Windows.UI.Xaml.IWindowStatics::get_Current, slot 6", "Windows.UI.Xaml.IWindow::get_Content, slot 8",
    "Windows.UI.Xaml.IWindow::put_Content, slot 9", "Windows.UI.Xaml.IWindow::Activate, slot 20",
    "Windows.UI.Xaml.Controls.IFrameFactory::CreateInstance, slot 6",
    "Windows.UI.Xaml.IDependencyPropertyStatics::get_UnsetValue, slot 6",
    "Windows.UI.Xaml.IDependencyPropertyStatics::Register, slot 7",
    "Windows.UI.Xaml.IDependencyPropertyStatics::RegisterAttached, slot 8",
    "Windows.UI.Xaml.IDependencyObject::GetValue, slot 6", "Windows.UI.Xaml.IDependencyObject::SetValue, slot 7",
    "Windows.UI.Xaml.IDependencyObject::ClearValue, slot 8", "Windows.UI.Xaml.IDependencyObject::ReadLocalValue, slot 9",
    "Windows.UI.Xaml.IDependencyObject::GetAnimationBaseValue, slot 10",
    "Windows.UI.Core.ICoreWindowStatic::GetForCurrentThread, slot 6", "Windows.UI.Core.ICoreWindow::get_Dispatcher, slot 9",
    "Windows.Storage.IApplicationDataStatics::get_Current, slot 6", "Windows.Storage.IApplicationData::get_LocalSettings, slot 10",
    "Windows.Storage.IApplicationData::get_RoamingSettings, slot 11", "Windows.Storage.IApplicationData::get_LocalFolder, slot 12", "Windows.Storage.IApplicationDataContainer::get_Name, slot 6",
    "Windows.Storage.IApplicationDataContainer::get_Locality, slot 7", "Windows.Storage.IApplicationDataContainer::get_Values, slot 8",
    "Windows.Storage.IApplicationDataContainer::CreateContainer, slot 10", "Windows.Storage.IApplicationDataContainer::DeleteContainer, slot 11",
    "Windows.Networking.Connectivity.INetworkInformationStatics::GetInternetConnectionProfile, slot 7",
    "Windows.Networking.Connectivity.INetworkInformationStatics::add_NetworkStatusChanged, slot 12",
    "Windows.Networking.Connectivity.INetworkInformationStatics::remove_NetworkStatusChanged, slot 13",
    "Windows.Networking.Connectivity.IConnectionProfile::get_ProfileName, slot 6",
    "Windows.Networking.Connectivity.IConnectionProfile::GetNetworkConnectivityLevel, slot 7",
    "Windows.ApplicationModel.Activation.IActivatedEventArgs::get_SplashScreen, slot 8",
    "Windows.ApplicationModel.Activation.ISplashScreen::add_Dismissed, slot 7",
    "Windows.ApplicationModel.Activation.ISplashScreen::remove_Dismissed, slot 8",
    "Windows.UI.Xaml.Controls.IPageFactory::CreateInstance, slot 6", "Windows.UI.Xaml.Controls.IPage::get_Frame, slot 6",
    "Windows.UI.Xaml.Controls.IPageOverrides::OnNavigatedFrom, slot 6", "Windows.UI.Xaml.Controls.IPageOverrides::OnNavigatedTo, slot 7",
    "Windows.UI.Xaml.Controls.IPageOverrides::OnNavigatingFrom, slot 8", "Windows.UI.Xaml.Controls.IUserControl::get_Content, slot 6",
    "Windows.UI.Xaml.Controls.IControlOverrides::OnKeyDown, slot 24",
    "Windows.UI.Xaml.Input.IKeyRoutedEventArgs::get_Key, slot 6",
    "Windows.UI.Xaml.Input.IKeyRoutedEventArgs::get_Handled, slot 8",
    "Windows.UI.Xaml.Input.IKeyRoutedEventArgs::put_Handled, slot 9",
    "Windows.UI.Xaml.Controls.IUserControl::put_Content, slot 7",
    "Windows.UI.Xaml.Controls.IContentControl::get_Content, slot 6", "Windows.UI.Xaml.Controls.IContentControl::put_Content, slot 7",
    "Windows.UI.Xaml.IDispatcherTimer::get_Interval, slot 6", "Windows.UI.Xaml.IDispatcherTimer::put_Interval, slot 7",
    "Windows.UI.Xaml.IDispatcherTimer::add_Tick, slot 9", "Windows.UI.Xaml.IDispatcherTimer::remove_Tick, slot 10",
    "Windows.UI.Core.ICoreDispatcher::RunAsync, slot 8",
    "Windows.ApplicationModel.DataTransfer.IDataTransferManagerStatics::GetForCurrentView, slot 7",
    "Windows.ApplicationModel.DataTransfer.IDataTransferManager::add_DataRequested, slot 6",
    "Windows.ApplicationModel.DataTransfer.IDataTransferManager::remove_DataRequested, slot 7",
    "Windows.UI.Xaml.IDispatcherTimer::Start, slot 11", "Windows.UI.Xaml.IDispatcherTimer::Stop, slot 12",
    "Windows.UI.Xaml.Markup.IComponentConnector::Connect, slot 6",
    "Windows.Foundation.IUriRuntimeClass::get_AbsoluteUri, slot 6", "Windows.Foundation.IUriRuntimeClassFactory::CreateUri, slot 6",
    "Windows.UI.Xaml.Navigation.INavigationEventArgs::get_Uri, slot 10", "Windows.UI.Xaml.Navigation.INavigationEventArgs::put_Uri, slot 11"]
# The runtime's dispatcher context calls CoreWindow and CoreDispatcher directly
# (windows.ui.core.h): each IID and slot, and the call that uses the slot.
HEADER_RUNTIME = [
    "// Windows.UI.Core.ICoreWindowStatic::GetForCurrentThread, slot 6", 'Guid("4d239005-3c2a-41b1-9022-536bb9cf93b1")',
    "statics.Object(6)", "// Windows.UI.Core.ICoreWindow::get_Dispatcher, slot 9", 'Guid("79b9d5f2-879e-4b89-b798-79e47598030c")',
    "core.Object(9)", "// Windows.UI.Core.ICoreDispatcher::RunAsync, slot 8", "dispatcher.Call(8, 0, ",
    "// Windows.UI.Core.IDispatchedHandler::Invoke, slot 3", 'Guid("d1f276c4-98d8-4636-bf49-eb79507548e9"), false, invoke']
# Delegates are IUnknown-based: Invoke is their only method, at slot 3.
HEADER_DELEGATES = ['NativeInterface(new global::System.Guid("' + iid + '"), false, invoke)'
                    for iid in ("b6351c55-c284-46e4-8310-fb0967fab76f", "23429465-e36a-40e2-b139-a4704602a6e1")]


def copy_owned_file(source, destination):
    """Copy into owned output without following a copied destination symlink."""
    destination = Path(destination)
    if destination.is_symlink():
        destination.unlink()
    shutil.copyfile(source, destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("wine", "prefix", "metadata", "shim", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--page-fixture", type=Path, help="test-only native Page callback DLL (default beside shim)")
    parser.add_argument("--component-metadata", type=Path, help="validate this native component metadata against the generated Windows interop reference")
    parser.add_argument("--component-only", action="store_true", help="run generic package component projection fixtures")
    parser.add_argument("--page-only", action="store_true", help="run native Page composition and override fixtures")
    parser.add_argument("--splash-only", action="store_true", help="run the native splash activation fixture")
    parser.add_argument("--network-fixture", type=Path, help="test-only native network callback DLL (default beside shim)")
    parser.add_argument("--network-only", action="store_true", help="run the native connectivity projection fixture")
    parser.add_argument("--io-only", action="store_true", help="run the native storage/stream projection fixture")
    parser.add_argument("--async-only", action="store_true", help="run the focused WinRT task adapters fixture")
    parser.add_argument("--storage-only", action="store_true", help="run the focused persistent settings projection fixture")
    parser.add_argument("--xbox-only", action="store_true", help="run the focused Xbox projection fixture")
    parser.add_argument("--surface-only", action="store_true", help="complete the whole SDK surface and run fixtures against it")
    parser.add_argument("--xbox-metadata", type=Path, help="package Microsoft.Xbox.winmd, metadata only")
    parser.add_argument("--game", type=Path, help="copy and launch this installed game after completion")
    parser.add_argument("--expected-game-error", help="required named failure in the copied game managed.log")
    parser.add_argument("--cecil", type=Path)
    parser.add_argument("--runtime-case", choices=("inspectable", "identity", "stale", "invocation", "reentrant"))
    parser.add_argument("--consumer", type=Path, action="append", default=[])
    parser.add_argument("--architecture", choices=("x86", "x64"), default="x86")
    args = parser.parse_args()
    if sum((args.component_only, args.page_only, args.splash_only, args.network_only, args.io_only, args.async_only, args.storage_only, args.xbox_only, args.surface_only, bool(args.runtime_case))) > 1:
        parser.error("focused probe modes cannot be combined")
    if (args.component_only or args.page_only or args.splash_only or args.network_only or args.io_only or args.async_only or args.storage_only) and (args.game or args.consumer or args.xbox_metadata):
        parser.error("focused projection probes cannot include game or Xbox inputs")
    if args.surface_only and (args.game or args.xbox_metadata):
        parser.error("--surface-only takes consumers but no game or Xbox inputs")
    if args.xbox_only and (not args.xbox_metadata or args.game):
        parser.error("--xbox-only requires --xbox-metadata and cannot launch a game")
    repo = Path(__file__).resolve().parents[1]
    wine, prefix, metadata, shim, output = (getattr(args, name).resolve()
                                          for name in ("wine", "prefix", "metadata", "shim", "output"))
    network_fixture = args.network_fixture.resolve() if args.network_fixture else shim.parent / "network_projection_fixture.dll"
    if (args.network_only or not any((args.component_only, args.page_only, args.splash_only, args.io_only, args.async_only, args.storage_only, args.xbox_only, args.surface_only, args.runtime_case))) and not network_fixture.is_file():
        parser.error("build network_projection_fixture or pass --network-fixture")
    page_fixture = args.page_fixture.resolve() if args.page_fixture else shim.parent / "page_projection_fixture.dll"
    if (args.page_only or args.surface_only or not any((args.component_only, args.splash_only, args.network_only, args.io_only, args.async_only, args.storage_only, args.xbox_only, args.runtime_case))) and not page_fixture.is_file():
        parser.error("build page_projection_fixture or pass --page-fixture")
    if args.game:
        args.game = args.game.resolve()
        if not args.consumer or not args.expected_game_error:
            parser.error("--game requires --consumer inputs and --expected-game-error")
        if args.game == output or args.game in output.parents or output in args.game.parents:
            parser.error("source game and output directories must be disjoint")
    output.mkdir(parents=True, exist_ok=True)
    libraries = list((wine.parent.parent / "share/wine/mono").glob("wine-mono-*/lib/mono/4.8-api"))
    if len(libraries) != 1:
        parser.error("expected one bundled Wine Mono 4.8 API directory")
    references = libraries[0]
    mono = references.parent
    cecils = [args.cecil.resolve()] if args.cecil else list((mono / "gac/Mono.Cecil").glob("0.11.*__*/Mono.Cecil.dll"))
    if len(cecils) != 1:
        parser.error("expected one bundled Mono.Cecil 0.11 library")
    image = shim.read_bytes()
    pe = struct.unpack_from("<I", image, 0x3c)[0]
    if (image[pe:pe + 4] != b"PE\0\0" or struct.unpack_from("<H", image, pe + 4)[0]
            != (0x14c if args.architecture == "x86" else 0x8664)):
        parser.error("shim architecture does not match the requested fixture")
    copy_owned_file(cecils[0], output / "Mono.Cecil.dll")
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG="-all", MVK_CONFIG_LOG_LEVEL="0")
    for key in ("WINE_MONO_AOT", "WINEDLLOVERRIDES", "MONO_PATH", "MONO_ENV_OPTIONS"):
        env.pop(key, None)
    results = []

    def windows(path):
        return "Z:" + str(path).replace("/", "\\")

    def require(value, message):
        if not value:
            raise RuntimeError(message)

    def run(name, command, cwd=output, succeeds=True, marker=None, evidence=None, environment=None, timeout=45):
        log = output / (name + ".log")
        with log.open("w") as stream:
            child = subprocess.Popen([str(wine), *command], env=dict(env, **(environment or {})), cwd=cwd,
                                     stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = child.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()
                code = "timeout"
        text = log.read_text(errors="replace")
        if evidence is not None:
            require(evidence.is_file(), name + " did not produce " + str(evidence))
            text += evidence.read_text(errors="replace")
        passed = (code == 0 if succeeds else code not in (0, "timeout"))
        passed = passed and (marker is None or marker in text)
        row = dict(case=name, exit=code, passed=passed, log=str(log))
        results.append(row)
        print(json.dumps(row), flush=True)
        require(passed, name + " failed: " + text[-2400:])
        return text

    def compile_file(name, sources, extra=()):
        destination = output / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        command = [windows(mono / "4.5/mcs.exe"), "/nologo", "/noconfig", "/nostdlib+", "/warnaserror+",
                   "/langversion:5", "/platform:" + args.architecture, "/out:" + windows(destination)]
        command += ["/r:" + windows(references / name)
                    for name in ("mscorlib.dll", "System.dll", "System.Core.dll", "System.Xml.dll")]
        command += ["/r:" + windows(r) for r in wpf_references(mono)]
        if any(Path(path).name in BOOTSTRAP_GENERATED for path in sources):
            sources = list(sources) + [repo / "host/winrt" / n for n in XAML_SOURCES]
            command.append("/resource:" + windows(repo / "host/winrt" / XAML_THEME) + "," + XAML_THEME_NAME)
        against_windows = surface_build and any(flag.startswith("/r:") and flag.endswith("Windows.dll") for flag in extra)
        if against_windows and "/r:" + windows(surface_build["interop"]) not in extra:
            extra = list(extra) + ["/r:" + windows(surface_build["interop"])]
        run("compile-" + name.replace("/", "-"), command + list(extra) + [windows(path) for path in sources])
        # Surface mode: C# binds WinRT setters as set_X; the surface names them put_X.
        if against_windows:
            compiled = destination.with_name(destination.stem + ".compiled" + destination.suffix)
            destination.replace(compiled)
            run("rebind-" + name.replace("/", "-"), [surface_build["completer"].name, "--rebind", windows(surface_build["surface"]), windows(compiled), windows(destination)],
                marker="Rebound ")
        return destination

    # The surface mode builds an interop-enabled Windows.dll: fixtures need its
    # interop assembly, and tests compiled from generated sources its flags.
    # Assemblies compile against the bootstrap and are deployed with the surface.
    abi_flags, fixture_support, surface_build = [], [], {}

    def deploy(source, destination):
        copy_owned_file(surface_build["surface"] if surface_build and Path(source) == surface_build["bootstrap"] else source, destination)

    def fixture(name, executable, identity, publisher):
        folder = output / name
        folder.mkdir(exist_ok=True)
        copy_owned_file(shim, folder / "appxportable.dll")
        for support in fixture_support:
            copy_owned_file(support, folder / support.name)
        (folder / "AppxManifest.xml").write_text(
            '<Package><Identity Name="' + identity + '" Publisher="' + publisher +
            '" Version="1.0.0.0" ProcessorArchitecture="' + args.architecture +
            '"/><Applications><Application Id="Test" Executable="' + executable + '"/></Applications></Package>')
        return folder

    def check_storage_metadata(generator):
        generated = (output / "Windows.generated.cs").read_text(encoding="utf-8")
        for value in ('Map = new global::System.Guid("1b0d3570-0877-5ec2-8a2c-3b9539506aca")',
                      'Iterable = new global::System.Guid("fe2f3d47-5d47-5499-8374-430c7cda0204")',
                      "Lookup = 6,", "Insert = 10,", "Remove = 11,", "Clear = 12,", "Size = 7, HasKey = 8,",
                      "First = 6, Current = 6,", "HasCurrent = 7, MoveNext = 8,", "Key = 6, Value = 7"):
            require(value in generated, "collection ABI differs from the SDK header: " + value)
        # Closed IMapView<String, ApplicationDataContainer> and its iteration, from windows.storage.h.
        for value in ('View = new global::System.Guid("13624f8d-85cc-5780-a78d-64dba58f2c3c")',
                      'Iterable = new global::System.Guid("a785be1d-159e-53ad-9553-598b03dca048")',
                      "// Windows.Storage.IApplicationDataContainer::get_Containers, slot 9"):
            require(value in generated, "container map ABI differs from the SDK header: " + value)
        mutator = compile_file("mutate-collections.exe", [repo / "tests/winrt_projection/mutate_collections_metadata.cs"], ["/r:Mono.Cecil.dll"])
        for kind, method in (("insert", "IMap`2.Insert"), ("iterator", "IIterator`1.get_HasCurrent"), ("order", "IMap`2 method order"),
                             ("view-order", "IMapView`2 method order")):
            mutated = output / ("collections-" + kind)
            target = output / ("collections-" + kind + ".cs")
            run("mutate-collections-" + kind, [mutator.name, windows(metadata), windows(mutated), kind])
            run("reject-collections-" + kind, [generator.name, windows(mutated), windows(repo / "host/winrt/bootstrap.xml"), windows(target)],
                succeeds=False, marker=method)
            require(not target.exists(), "invalid collection ABI produced a usable output")

    def probe_async(projection):
        bridge = compile_file("tasks/System.Runtime.WindowsRuntime.dll", [repo / "host/winrt/task_extensions.cs", repo / "host/winrt/stream_extensions.cs"],
                              ["/target:library", "/r:" + windows(projection)])
        consumer = compile_file("async.exe", [repo / "tests/winrt_projection/async.cs"],
                                ["/r:" + windows(projection), "/r:" + windows(bridge)])
        marker = compile_file("mark-async.exe", [repo / "tests/winrt_projection/mark_reference.cs"], ["/r:Mono.Cecil.dll"])
        run("mark-async-winrt", [marker.name, windows(consumer), "async-winrt.exe"], marker="flags=WindowsRuntime")
        signed = output / "async-bound.exe"
        run("mark-async-runtime", [marker.name, "async-winrt.exe", windows(signed), "--runtime-reference"],
            marker="PublicKeyToken=b77a5c561934e089")
        folder = fixture("async", signed.name, "AsyncProjectionTest", "CN=projection")
        for source in (signed, projection):
            deploy(source, folder / source.name)
        copied_bridge = folder / bridge.name
        if copied_bridge.exists() or copied_bridge.is_symlink():
            copied_bridge.unlink()
        run("missing-task-runtime", [signed.name], cwd=folder, succeeds=False, marker="System.Runtime.WindowsRuntime")
        copy_owned_file(bridge, copied_bridge)
        run("task-adapters", [signed.name], cwd=folder,
            marker="PASS: WinRT task adapters preserve completion, failure, cancellation, progress and ownership")
        probe_redirected(projection, bridge, marker)
        return bridge

    def probe_redirected(projection, bridge, marker):
        # Games bind redirected structs to the CLR facades; ours forward them to Windows.dll.
        xaml = compile_file("tasks/System.Runtime.WindowsRuntime.UI.Xaml.dll", [repo / "host/winrt/xaml_forwards.cs"],
                            ["/target:library", "/r:" + windows(projection)])
        source = repo / "tests/winrt_projection/redirected_reference.cs"
        references = [compile_file("redirected-reference/System.Runtime.WindowsRuntime.dll", [source], ["/target:library"]),
                      compile_file("redirected-reference/System.Runtime.WindowsRuntime.UI.Xaml.dll", [source], ["/target:library", "/define:XAML"])]
        consumer = compile_file("redirected.exe", [repo / "tests/winrt_projection/redirected.cs"], ["/r:" + windows(r) for r in references])
        signed = output / "redirected-bound.exe"
        run("mark-redirected-runtime", [marker.name, windows(consumer), windows(signed), "--runtime-reference"],
            marker="System.Runtime.WindowsRuntime.UI.Xaml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089")
        folder = fixture("redirected", signed.name, "RedirectedTest", "CN=projection")
        for source in (signed, projection, bridge):
            deploy(source, folder / source.name)
        run("missing-xaml-runtime", [signed.name], cwd=folder, succeeds=False, marker="System.Runtime.WindowsRuntime.UI.Xaml")
        deploy(xaml, folder / xaml.name)
        run("redirected-structs", [signed.name], cwd=folder, marker="PASS: CLR-redirected structs bind through the facades to Windows.dll")
        return xaml

    def probe_page(projection):
        generated = (output / "Windows.generated.cs").read_text(encoding="utf-8")
        # WPF backs Windows.UI.Xaml elements; the shim-backed rest keeps SDK header ABIs.
        for name in ("IDispatcherTimer", "EventHandler<IInspectable>", "IComponentConnector", "IUriRuntimeClass", "IUriRuntimeClassFactory", "IWindowSizeChangedEventHandler", "IWindowSizeChangedEventArgs", "ICoreDispatcher", "IDispatchedHandler", "IDataTransferManager", "IDataTransferManagerStatics", "TypedEventHandler<DataTransferManager, DataRequestedEventArgs>", "IXamlMetadataProvider", "IXamlType", "IXamlMember"):
            require(HEADER_IIDS[name] in generated, "Page IID differs from SDK header: " + name)
        for slot in HEADER_SLOTS:
            if any(name in slot for name in ("IDispatcherTimer::", "IComponentConnector", "IUriRuntimeClass", "IWindow::add_SizeChanged", "IWindow::remove_SizeChanged", "ICoreDispatcher::RunAsync", "IDataTransferManager", ".IDataTransferManagerStatics::")):
                require("// " + slot in generated, "Page slot differs from SDK header: " + slot)
        require("class Grid : global::Windows.UI.Xaml.Controls.Panel" in generated and "NativeObject" not in generated.split("class Grid ")[1].split("    }")[0],
                "Grid is a WPF-backed wrapper")
        surface = (repo / "host/winrt/bootstrap.xml").read_text(encoding="utf-8")
        for kind, change, member in (
                ("native-xaml", ('<Class Name="Windows.UI.Xaml.Media.VisualTreeHelper" Adapter="Wpf" />', '<Class Name="Windows.UI.Xaml.Media.VisualTreeHelper" />'),
                 "Unsupported native XAML class: Windows.UI.Xaml.Media.VisualTreeHelper"),
                ("wpf-mapping", ('<Property Name="Text" Wpf="System.Windows.Controls.TextBlock.TextProperty" />', '<Property Name="Text" Wpf="System.Windows.Controls.TextBlock.Text" />'),
                 "Unsupported WPF mapping: Windows.UI.Xaml.Controls.TextBlock.Text"),
                ("wpf-member", ('<Property Name="Text" Wpf="System.Windows.Controls.TextBlock.TextProperty" />', '<Property Name="NoSuch" Wpf="System.Windows.Controls.TextBlock.TextProperty" />'),
                 "Unsupported WPF mapping: Windows.UI.Xaml.Controls.TextBlock.NoSuch")):
            require(change[0] in surface, "bootstrap.xml lacks the " + kind + " entry")
            changed = output / ("bootstrap-" + kind + ".xml")
            changed.write_text(surface.replace(change[0], change[1]), encoding="utf-8")
            run("reject-" + kind, ["generator.exe", windows(metadata), windows(changed), windows(output / (kind + ".cs"))], succeeds=False, marker=member)
        mutator = compile_file("mutate-component.exe", [repo / "tests/winrt_projection/mutate_collections_metadata.cs"], ["/r:Mono.Cecil.dll"])
        for kind, member in (("connector-id", "IComponentConnector.Connect"), ("uri-return", "IUriRuntimeClass.get_AbsoluteUri"), ("uri-factory", "IUriRuntimeClassFactory.CreateUri"),
                             ("xaml-type-order", "Unsupported XAML metadata ABI: Windows.UI.Xaml.Markup.IXamlType"),
                             ("xaml-member-value", "Unsupported XAML metadata ABI: Windows.UI.Xaml.Markup.IXamlMember"),
                             ("clr-rect", "Unsupported CLR-redirected struct: Windows.Foundation.Rect")):
            mutated = output / kind
            run("mutate-" + kind, [mutator.name, windows(metadata), windows(mutated), kind])
            run("reject-" + kind, ["generator.exe", windows(mutated), windows(repo / "host/winrt/bootstrap.xml"), windows(output / (kind + ".cs"))], succeeds=False, marker=member)
        marker = compile_file("mark-page.exe", [repo / "tests/winrt_projection/mark_reference.cs"], ["/r:Mono.Cecil.dll"])
        reference = compile_file("page-reference/Windows.dll", [repo / "tests/winrt_projection/reference.cs"], ["/target:library"])
        consumer = compile_file("page.exe", [repo / "tests/winrt_projection/page.cs"],
                                ["/r:" + windows(reference)] + (["/define:FULL_SURFACE"] if surface_build else []))
        run("mark-page", [marker.name, windows(consumer), "page-winrt.exe"], marker="flags=WindowsRuntime")
        folder = fixture("page", consumer.name, "PageProjectionTest", "CN=projection")
        copy_owned_file(output / "page-winrt.exe", folder / consumer.name)
        deploy(projection, folder / "Windows.dll")
        copy_owned_file(page_fixture, folder / "page_projection_fixture.dll")
        run("page", [consumer.name], cwd=folder, marker="PASS: WPF dispatcher runs on the XAML view thread's shim loop",
            environment={"APPX_PORTABLE_LOG": "1"})
        trace = (folder / "appx-portable.log").read_text(errors="replace") if (folder / "appx-portable.log").is_file() else ""
        require("managed callback threw System.InvalidOperationException: tick handler failure" in trace,
                "a managed event handler failure was not reported to the native log")
        alias = compile_file("visibility-alias.exe", [repo / "tests/winrt_projection/visibility_alias.cs"], ["/r:" + windows(reference)])
        run("mark-visibility-alias", [marker.name, windows(alias), "visibility-put.exe", "--abi-setters"], marker="flags=WindowsRuntime")
        completer = compile_file("complete-visibility.exe", [repo / "host/winrt/complete.cs"], ["/r:Mono.Cecil.dll"])
        completed = output / "visibility-completed/Windows.dll"
        completed.parent.mkdir(exist_ok=True)
        run("complete-visibility-alias", [completer.name, windows(metadata), windows(projection), windows(completed), windows(output / "visibility-put.exe")])
        alias_folder = fixture("visibility-alias", alias.name, "VisibilityAliasTest", "CN=projection")
        copy_owned_file(output / "visibility-put.exe", alias_folder / alias.name)
        copy_owned_file(completed, alias_folder / "Windows.dll")
        run("visibility-abi-alias", [alias.name], cwd=alias_folder, marker="PASS: WinRT put_Visibility forwards to the WPF peer")
        source = compile_file("source.exe", [repo / "tests/winrt_projection/source.cs"], ["/r:" + windows(reference)])
        run("mark-source", [marker.name, windows(source), "source-winrt.exe"], marker="flags=WindowsRuntime")
        folder = fixture("source", source.name, "SourceProjectionTest", "CN=projection")
        copy_owned_file(output / "source-winrt.exe", folder / source.name)
        deploy(projection, folder / "Windows.dll")
        copy_owned_file(page_fixture, folder / "page_projection_fixture.dll")
        run("source", [source.name], cwd=folder, environment={"APPX_PORTABLE_LOG": "1"},
            marker="PASS: managed loader builds WPF content with names, connections and game types; unsupported input fails explicitly")

    def probe_splash(projection, generator):
        generated = (output / "Windows.generated.cs").read_text(encoding="utf-8")
        for value in (HEADER_IIDS["ISplashScreen"], "7725b2a5-287d-5ed2-a789-2a6a2673c7fe"):
            require(value in generated, "splash IID differs from SDK header: " + value)
        for value in HEADER_SLOTS:
            if "SplashScreen" in value or "ISplashScreen" in value:
                require("// " + value in generated, "splash ABI differs from SDK header: " + value)
        mutator = compile_file("mutate-splash.exe", [repo / "tests/winrt_projection/mutate_collections_metadata.cs"], ["/r:Mono.Cecil.dll"])
        changed = output / "splash-bad-handler"
        invalid = output / "splash-bad-handler.cs"
        if invalid.exists():
            invalid.unlink()
        run("mutate-splash-handler", [mutator.name, windows(metadata), windows(changed), "typed-event"])
        run("reject-splash-handler", [generator.name, windows(changed), windows(repo / "host/winrt/bootstrap.xml"), windows(invalid)], succeeds=False, marker="Unsupported typed event delegate")
        require(not invalid.exists(), "unqualified typed event produced output")
        marker = compile_file("mark-splash.exe", [repo / "tests/winrt_projection/mark_reference.cs"], ["/r:Mono.Cecil.dll"])
        reference = compile_file("splash-reference/Windows.dll", [repo / "tests/winrt_projection/reference.cs"], ["/target:library"])
        consumer = compile_file("splash.exe", [repo / "tests/winrt_projection/splash.cs"], ["/r:" + windows(reference)])
        run("mark-splash", [marker.name, windows(consumer), "splash-winrt.exe"], marker="flags=WindowsRuntime")
        folder = fixture("splash", consumer.name, "SplashProjectionTest", "CN=projection")
        copy_owned_file(output / "splash-winrt.exe", folder / consumer.name)
        deploy(projection, folder / "Windows.dll")
        run("splash", [consumer.name], cwd=folder, marker="PASS: managed splash identity and typed dismissal follow native activation")

    def probe_network(projection, generator):
        generated = (output / "Windows.generated.cs").read_text(encoding="utf-8")
        checks = ['System.Guid("' + HEADER_IIDS[name] + '")' for name in ("INetworkInformationStatics", "IConnectionProfile", "INetworkStatusChangedEventHandler")]
        checks += ["// " + slot for slot in HEADER_SLOTS if "Networking.Connectivity" in slot]
        checks += ["self, out int result);", "public static global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_NetworkStatusChanged"]
        require(all(value in generated for value in checks), "network ABI differs from SDK header values")
        mutator = compile_file("mutate-network.exe", [repo / "tests/winrt_projection/mutate_collections_metadata.cs"], ["/r:Mono.Cecil.dll"])
        changed = output / "network-wide-enum"
        invalid = output / "network-wide-enum.cs"
        if invalid.exists():
            invalid.unlink()
        run("mutate-network-enum", [mutator.name, windows(metadata), windows(changed), "network-enum"])
        run("reject-network-enum", [generator.name, windows(changed), windows(repo / "host/winrt/bootstrap.xml"), windows(invalid)], succeeds=False, marker="Unsupported enum return")
        require(not invalid.exists(), "invalid connectivity enum left a usable output")
        marker = compile_file("mark-network.exe", [repo / "tests/winrt_projection/mark_reference.cs"], ["/r:Mono.Cecil.dll"])
        reference = compile_file("network-reference/Windows.dll", [repo / "tests/winrt_projection/reference.cs"], ["/target:library"])
        consumer = compile_file("network.exe", [repo / "tests/winrt_projection/network.cs"], ["/r:" + windows(reference)])
        run("mark-network", [marker.name, windows(consumer), "network-winrt.exe"], marker="flags=WindowsRuntime")
        folder = fixture("network", consumer.name, "NetworkProjectionTest", "CN=projection")
        copy_owned_file(output / "network-winrt.exe", folder / consumer.name)
        copy_owned_file(reference, folder / "Windows.dll")
        run("network-reference-contract", [consumer.name], cwd=folder, succeeds=False, marker="NetworkInformation.GetInternetConnectionProfile")
        deploy(projection, folder / "Windows.dll")
        for mode in ("offline", "stub"):
            run("network-" + mode, [consumer.name, mode], cwd=folder, environment={"APPX_PORTABLE_STUB_IAP": "1" if mode == "stub" else "0"},
                marker="PASS: managed network projection preserves native connectivity and event lifetime (" + mode + ")")
        runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs", "storage_abi.cs", "array_buffer.cs")]
        abi = compile_file("network-abi.exe", runtime + [output / "Windows.generated.cs", repo / "tests/winrt_projection/network_abi.cs"])
        copy_owned_file(abi, folder / abi.name)
        copy_owned_file(network_fixture, folder / "network_projection_fixture.dll")
        run("network-abi", [abi.name], cwd=folder, marker="PASS: native network delegate ABI preserves sender and callback failures")

    def check_io_metadata(generator):
        mutator = compile_file("mutate-storage-abi.exe", [repo / "tests/winrt_projection/mutate_collections_metadata.cs"], ["/r:Mono.Cecil.dll"])
        for kind, expected in (("stream-size", "IRandomAccessStream.get_Size"), ("read-count", "IInputStream.ReadAsync"),
                               ("buffer-order", "Unsupported buffer method order"), ("future-order", "Unsupported storage async method order"),
                               ("writer-string", "IDataWriter.WriteString"), ("writer-factory", "IDataWriterFactory.CreateDataWriter"),
                               ("file-open", "IStorageFile.OpenAsync"), ("reader-load", "IDataReader.LoadAsync"), ("reader-factory", "IDataReaderFactory.CreateDataReader")):
            changed = output / ("storage-" + kind)
            target = output / ("storage-" + kind + ".cs")
            if target.exists():
                target.unlink()
            run("mutate-storage-" + kind, [mutator.name, windows(metadata), windows(changed), kind])
            run("reject-storage-" + kind, [generator.name, windows(changed), windows(repo / "host/winrt/bootstrap.xml"), windows(target)], succeeds=False, marker=expected)
            require(not target.exists(), "unqualified storage ABI produced output")

    def probe_io(projection, bridge):
        generated = (output / "Windows.generated.cs").read_text(encoding="utf-8")
        # Independent closed generic IID constants from windows.storage(.streams).h.
        for iid in ("5e52f8ce-aced-5a42-95b4-f674dd84885e", "c4a57c5e-32b0-55b3-ad13-ce1c23041ed6", "d26b2819-897f-5c7d-84d6-56d796561431", "e521c894-2c26-5946-9e61-2b5e188d01ed", "3dddecf4-1d39-58e8-83b1-dbed541c7f35", "06386a7a-e009-5b0b-ab68-a8e48b516647", "bf666554-7605-5d9a-b14e-18d8c8472afe",
                    "3e1fe603-f897-5263-b328-0806426b8a79", "b79a741f-7fb5-50ae-9e99-911201ec3d41", "887411eb-7f54-4732-a5f0-5e43e3b8c2f5", "fa3f6186-4214-428c-a64c-14c9ac7315ea",
                    "5a648006-843a-4da9-865b-9d26e5dfad7b", "a4ed5c81-76c9-40bd-8be6-b1d90fb20ae7",
                    "64b89265-d341-4922-b38a-dd4af8808c4e", "338c67c2-8b84-4c2b-9c50-7b8767847a1f",
                    "905a0fe6-bc53-11df-8c49-001e4fc686da", "430ecece-1418-5d19-81b2-5ddb381603cc", "398c4183-793d-5b00-819b-4aef92485e94",
                    "d7527847-57da-4e15-914c-06806699a098", "ef60385f-be78-584b-aaef-7829ada2b0de",
                    "9343b6e7-e3d2-5e4a-ab2d-2bce4919a6a4"):
            require(iid in generated, "storage async IID differs from SDK header: " + iid)
        # Slots independently counted from the SDK ABI declarations.
        expected_slots = {"Item_get_Name": 11, "Item_get_Path": 12, "Folder_GetFileAsync": 10,
                          "File_OpenAsync": 8, "FileStatics_GetFileFromPathAsync": 6, "FileReference_OpenReadAsync": 6,
                          "Info_get_Id": 6, "Info_get_Status": 7, "Info_get_ErrorCode": 8, "Info_Cancel": 9, "Info_Close": 10,
                          "Buffer_get_Capacity": 6, "Buffer_get_Length": 7, "Buffer_put_Length": 8, "BufferFactory_Create": 6,
                          "Input_ReadAsync": 6, "Random_get_Size": 6, "Random_put_Size": 7, "Random_GetInputStreamAt": 8,
                          "Random_GetOutputStreamAt": 9, "Random_get_Position": 10, "Random_Seek": 11, "Random_CloneStream": 12,
                          "Random_get_CanRead": 13, "Random_get_CanWrite": 14, "Content_get_ContentType": 6, "Closable_Close": 6,
                          "FileIO_ReadTextAsync": 6, "FileIO_WriteBufferAsync": 19,
                          "Folder_CreateFileAsyncOverloadDefaultOptions": 6, "Folder_CreateFileAsync": 7,
                          "Action_put_Completed": 6, "Action_get_Completed": 7, "Action_GetResults": 8,
                          "Reader_get_UnconsumedBufferLength": 6, "Reader_ReadBytes": 14, "Reader_ReadString": 26,
                          "Reader_LoadAsync": 29, "Reader_DetachStream": 31, "ReaderFactory_CreateDataReader": 6,
                          "ReaderStatics_FromBuffer": 6, "Writer_WriteString": 27, "Writer_DetachBuffer": 31,
                          "WriterFactory_CreateDataWriter": 6}
        for member, slot in expected_slots.items():
            require("int " + member + " = " + str(slot) + ";" in generated, "storage ABI slot differs from SDK header: " + member)
        # windows.system.profile.h, windows.storage.streams.h and windows.foundation.h.
        for value in ('new global::System.Guid("971260e0-f170-4a42-bd55-a900b212dae2")', 'new global::System.Guid("28f6d4c0-fb12-40a4-8167-7f4e03d2724c")',
                      'new global::System.Guid("11fcbfc8-f93a-471b-b121-f379e349313c")', 'new global::System.Guid("e2b50029-b4c1-4314-a4b8-fb813a2f275e")',
                      'new global::System.Guid("30d5a829-7fa4-4026-83bb-d75bae4ea99e")',
                      "// Windows.System.Profile.IHardwareIdentificationStatics::GetPackageSpecificToken, slot 6",
                      "// Windows.System.Profile.IHardwareToken::get_Id, slot 6", "// Windows.System.Profile.IHardwareToken::get_Signature, slot 7",
                      "// Windows.System.Profile.IHardwareToken::get_Certificate, slot 8", "// Windows.Storage.Streams.IDataReaderStatics::FromBuffer, slot 6",
                      "// Windows.Storage.Streams.IDataReader::get_UnconsumedBufferLength, slot 6", "// Windows.Storage.Streams.IDataReader::ReadBytes, slot 14",
                      "// Windows.Foundation.IClosable::Close, slot 6"):
            require(value in generated, "token and reader ABI differs from the SDK header: " + value)
        # windows.security.cryptography.h and windows.security.cryptography.core.h.
        for value in ('new global::System.Guid("320b7e22-3cb0-4cdf-8663-1d28910065eb")', 'new global::System.Guid("9fac9741-5cc4-4336-ae38-6212b75a915a")',
                      'new global::System.Guid("be9b3080-b2c3-422b-bce1-ec90efb5d7b5")', 'new global::System.Guid("6b5e0516-de96-4f0a-8d57-dcc9dae36c76")',
                      "// Windows.Security.Cryptography.ICryptographicBufferStatics::EncodeToBase64String, slot 14",
                      "// Windows.Security.Cryptography.ICryptographicBufferStatics::ConvertStringToBinary, slot 15",
                      "// Windows.Security.Cryptography.Core.IHashAlgorithmProviderStatics::OpenAlgorithm, slot 6",
                      "// Windows.Security.Cryptography.Core.IHashAlgorithmProvider::get_AlgorithmName, slot 6",
                      "// Windows.Security.Cryptography.Core.IHashAlgorithmProvider::get_HashLength, slot 7",
                      "// Windows.Security.Cryptography.Core.IHashAlgorithmProvider::HashData, slot 8",
                      "// Windows.Security.Cryptography.Core.IHashAlgorithmNamesStatics::get_Sha1, slot 7"):
            require(value in generated, "cryptography ABI differs from the SDK header: " + value)
        consumer = compile_file("storage-io.exe", [repo / "tests/winrt_projection/storage_io.cs"],
                                ["/r:" + windows(projection), "/r:" + windows(bridge)])
        marker = output / "mark-async.exe"
        run("mark-storage-io-winrt", [marker.name, windows(consumer), "storage-io-winrt.exe"], marker="flags=WindowsRuntime")
        signed = output / "storage-io-bound.exe"
        run("mark-storage-io-runtime", [marker.name, "storage-io-winrt.exe", windows(signed), "--runtime-reference"], marker="PublicKeyToken=b77a5c561934e089")
        folder = fixture("storage-io", signed.name, "StorageIoProjectionTest", "CN=projection")
        for source in (signed, projection, bridge):
            deploy(source, folder / source.name)
        runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs", "storage_abi.cs", "array_buffer.cs")]
        identity = compile_file("storage-identity.exe", runtime + [output / "Windows.generated.cs", repo / "tests/winrt_projection/storage_identity.cs"], abi_flags)
        copy_owned_file(identity, folder / identity.name)
        run("storage-identity", [identity.name], cwd=folder, marker="PASS: buffer projection preserves object identity and native-owned pin lifetime")
        # Each run starts from empty app data: a reused output folder keeps files the test creates.
        shutil.rmtree(output / "io-data", ignore_errors=True)
        run("storage-io", [signed.name], cwd=folder, environment={"APPX_PORTABLE_DATA": windows(output / "io-data")},
            marker="PASS: managed storage awaits native files and reads seekable streams and buffers")

    def probe_storage(marker, projection):
        reference = compile_file("storage-reference/Windows.dll", [repo / "tests/winrt_projection/reference.cs"], ["/target:library"])
        consumer = compile_file("storage.exe", [repo / "tests/winrt_projection/storage.cs"], ["/r:" + windows(reference)])
        run("mark-storage", [marker.name, consumer.name, "storage-winrt.exe"], marker="flags=WindowsRuntime")
        folder = fixture("storage", consumer.name, "StorageProjectionTest", "CN=projection")
        copy_owned_file(output / "storage-winrt.exe", folder / consumer.name)
        deploy(projection, folder / "Windows.dll")
        # Settings persist from the write run to the read run, never from an earlier probe.
        shutil.rmtree(output / "settings-data", ignore_errors=True)
        environment = {"APPX_PORTABLE_DATA": windows(output / "settings-data")}
        for mode in ("write", "read"):
            run("storage-" + mode, [consumer.name, mode], cwd=folder, environment=environment,
                marker="PASS: managed settings " + mode + " through the native persistent map")

    def probe_xbox(marker, generator, completer):
        xbox_reference = compile_file("xbox-reference/Microsoft.Xbox.dll", [repo / "tests/winrt_projection/xbox_reference.cs"], ["/target:library"])
        xbox_consumer = compile_file("xbox.exe", [repo / "tests/winrt_projection/xbox.cs"], ["/r:" + windows(xbox_reference)])
        run("mark-xbox", [marker.name, xbox_consumer.name, "xbox-winrt.exe", "--assembly", "Microsoft.Xbox"], marker="flags=WindowsRuntime")
        xbox_folder = fixture("xbox", xbox_consumer.name, "XboxProjectionTest", "CN=projection")
        copy_owned_file(output / "xbox-winrt.exe", xbox_folder / xbox_consumer.name)
        copy_owned_file(xbox_reference, xbox_folder / "Microsoft.Xbox.dll")
        run("xbox-reference-contract", [xbox_consumer.name], cwd=xbox_folder, succeeds=False, marker="Microsoft.Xbox.User..ctor")
        xbox_metadata = output / "xbox-metadata"
        xbox_metadata.mkdir(exist_ok=True)
        xbox_winmd = xbox_metadata / "Microsoft.Xbox.winmd"
        copy_owned_file(args.xbox_metadata.resolve(), xbox_winmd)
        xbox_generated = output / "Microsoft.Xbox.generated.cs"
        run("generate-xbox", [generator.name, windows(xbox_metadata), windows(repo / "host/winrt/xbox.xml"), windows(xbox_generated)])
        xbox_text = xbox_generated.read_text(encoding="utf-8")
        for value in ("d7fba214-e744-4093-9fee-ab68795b70a0", "115ccd10-26c2-471c-b97b-5b3dac160c6e", "4b00e95d-3006-4f02-bd96-3919d984d4c7"):
            require('System.Guid("' + value + '")' in xbox_text, "Xbox IID differs from native header: " + value)
        for member in ("Microsoft.Xbox.IUser::get_Identity, slot 6", "Microsoft.Xbox.Foundation.IServiceClient::get_Token, slot 11",
                       "Microsoft.Xbox.Foundation.IServiceClient::get_SignedInUserIdentity, slot 10", "Microsoft.Xbox.IXboxLIVEService::get_ServiceClient, slot 11",
                       "Microsoft.Xbox.IXboxLIVEService::get_SignedInUserIdentity, slot 10"):
            require("// " + member in xbox_text, "Xbox member differs from native header: " + member)
        no_constructor = output / "xbox-no-default-constructor.xml"
        no_constructor.write_text('<Projection><Class Name="Microsoft.Xbox.Foundation.SignedOutEventArgs"><Constructor /></Class></Projection>')
        run("reject-xbox-constructor", [generator.name, windows(xbox_metadata), windows(no_constructor), windows(output / "xbox-no-constructor.cs")],
            succeeds=False, marker="No activation constructor")
        qualifier = compile_file("xbox-metadata.exe", [repo / "host/winrt/xbox_metadata.cs"], ["/r:Mono.Cecil.dll"])
        version = output / "Microsoft.Xbox.version.cs"
        qualification = run("qualify-xbox", [qualifier.name, windows(xbox_winmd), windows(version)], marker="Xbox IUser ABI ")
        abi_version = re.search(r"Xbox IUser ABI ([0-9.]+)", qualification).group(1)
        metadata_hash = hashlib.sha256(xbox_winmd.read_bytes()).hexdigest()
        require(metadata_hash in xbox_text and metadata_hash in qualification,
                "Xbox generation and qualification must use the same metadata snapshot")
        mutator = compile_file("mutate-xbox.exe", [repo / "tests/winrt_projection/mutate_xbox_metadata.cs"], ["/r:Mono.Cecil.dll"])
        for mutation in ("iid", "layout", "signature", "service-slot", "live-slot"):
            changed = output / ("xbox-" + mutation + ".winmd")
            invalid = output / ("xbox-" + mutation + ".cs")
            run("mutate-xbox-" + mutation, [mutator.name, windows(xbox_winmd), windows(changed), mutation])
            run("reject-xbox-" + mutation, [qualifier.name, windows(changed), windows(invalid)], succeeds=False, marker="Unsupported Xbox")
            require(not invalid.exists(), "unqualified Xbox metadata produced a version")
        runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs")]
        bootstrap = compile_file("xbox-projection/Microsoft.Xbox.dll", runtime + [xbox_generated, version], ["/target:library"])
        all_consumers = [output / "xbox-winrt.exe"] + [p.resolve() for p in args.consumer]
        xbox_projection = output / "completed/Microsoft.Xbox.dll"
        xbox_projection.parent.mkdir(exist_ok=True)
        run("complete-xbox", [completer.name, windows(xbox_metadata), windows(bootstrap), windows(xbox_projection)] + [windows(p) for p in all_consumers], marker="Completed ")
        copy_owned_file(xbox_projection, xbox_folder / "Microsoft.Xbox.dll")
        resolver = compile_file("resolve-xbox.exe", [repo / "tests/winrt_projection/resolve_references.cs"], ["/r:Mono.Cecil.dll"])
        run("resolve-xbox", [resolver.name, "--assembly", "Microsoft.Xbox", windows(xbox_projection.parent)] + [windows(p) for p in all_consumers], marker="0 unresolved")
        run("xbox-offline-contract", [xbox_consumer.name, abi_version], cwd=xbox_folder, marker="PASS: Xbox projection uses native signed-out user and service client")
        # The package's own Microsoft.Xbox.winmd projects generically at install time.
        xbox_bridge = compile_file("xbox-interop/AppxPortable.WinRT.Interop.dll", [repo / "host/winrt/interop.cs"], ["/target:library"])
        xbox_flags = ["/target:library", "/define:APPX_PORTABLE_INTEROP", "/r:" + windows(xbox_bridge)]
        xbox_windows_generated = output / "Windows.xbox-interop.cs"
        run("generate-xbox-windows-interop", [generator.name, windows(metadata), windows(repo / "host/winrt/bootstrap.xml"), windows(xbox_windows_generated), "--interop"])
        generic_runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs")]
        xbox_windows = compile_file("xbox-interop/Windows.dll", generic_runtime + [repo / "host/winrt/storage_abi.cs", repo / "host/winrt/array_buffer.cs", xbox_windows_generated], xbox_flags)
        xbox_component = output / "Microsoft.Xbox.component.cs"
        run("generate-xbox-component", [generator.name, "--component", windows(xbox_winmd), windows(xbox_component), "--reference", windows(xbox_windows)])
        # The shim reads the native ABI layout from the version resource beside the game;
        # installs keep the package's native DLL there, so the fixture borrows the qualified one.
        xbox_generic = compile_file("xbox-component/Microsoft.Xbox.dll", generic_runtime + [xbox_component, version], xbox_flags + ["/r:" + windows(xbox_windows)])
        generic_folder = fixture("xbox-generic", xbox_consumer.name, "XboxProjectionTest", "CN=projection")
        copy_owned_file(output / "xbox-winrt.exe", generic_folder / xbox_consumer.name)
        for source in (xbox_generic, xbox_windows, xbox_bridge):
            copy_owned_file(source, generic_folder / source.name)
        run("xbox-generic-contract", [xbox_consumer.name, "generic"], cwd=generic_folder,
            marker="PASS: generic Xbox projection uses native signed-out user and service client")
        run("resolve-xbox-generic", [resolver.name, "--assembly", "Microsoft.Xbox", windows(xbox_generic.parent)] + [windows(p) for p in all_consumers], marker="0 unresolved")
        return xbox_projection

    try:
        if args.page_only:
            generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs", repo / "host/winrt/component_generation.cs", repo / "host/winrt/wpf_generation.cs"], ["/r:Mono.Cecil.dll"])
            generated = output / "Windows.generated.cs"
            run("generate", [generator.name, windows(metadata), windows(repo / "host/winrt/bootstrap.xml"), windows(generated)])
            runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs", "storage_abi.cs", "array_buffer.cs")]
            projection = compile_file("projection/Windows.dll", runtime + [generated], ["/target:library"])
            probe_page(projection)
            return 0
        if args.splash_only:
            generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs", repo / "host/winrt/component_generation.cs", repo / "host/winrt/wpf_generation.cs"], ["/r:Mono.Cecil.dll"])
            generated = output / "Windows.generated.cs"
            run("generate", [generator.name, windows(metadata), windows(repo / "host/winrt/bootstrap.xml"), windows(generated)])
            runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs", "storage_abi.cs", "array_buffer.cs")]
            projection = compile_file("projection/Windows.dll", runtime + [generated], ["/target:library"])
            probe_splash(projection, generator)
            return 0
        if args.network_only:
            generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs", repo / "host/winrt/component_generation.cs", repo / "host/winrt/wpf_generation.cs"], ["/r:Mono.Cecil.dll"])
            generated = output / "Windows.generated.cs"
            run("generate", [generator.name, windows(metadata), windows(repo / "host/winrt/bootstrap.xml"), windows(generated)])
            runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs", "storage_abi.cs", "array_buffer.cs")]
            projection = compile_file("projection/Windows.dll", runtime + [generated], ["/target:library"])
            probe_network(projection, generator)
            return 0
        if args.io_only:
            generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs", repo / "host/winrt/component_generation.cs", repo / "host/winrt/wpf_generation.cs"], ["/r:Mono.Cecil.dll"])
            generated = output / "Windows.generated.cs"
            run("generate", [generator.name, windows(metadata), windows(repo / "host/winrt/bootstrap.xml"), windows(generated)])
            runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs")]
            projection = compile_file("projection/Windows.dll", runtime + [repo / "host/winrt/storage_abi.cs", repo / "host/winrt/array_buffer.cs", generated], ["/target:library"])
            check_io_metadata(generator)
            bridge = probe_async(projection)
            probe_io(projection, bridge)
            return 0
        if args.async_only:
            generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs", repo / "host/winrt/component_generation.cs", repo / "host/winrt/wpf_generation.cs"], ["/r:Mono.Cecil.dll"])
            generated = output / "Windows.generated.cs"
            run("generate", [generator.name, windows(metadata), windows(repo / "host/winrt/bootstrap.xml"), windows(generated)])
            runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs")]
            projection = compile_file("projection/Windows.dll", runtime + [repo / "host/winrt/storage_abi.cs", repo / "host/winrt/array_buffer.cs", generated], ["/target:library"])
            probe_async(projection)
            return 0
        if args.storage_only:
            marker = compile_file("mark-reference.exe", [repo / "tests/winrt_projection/mark_reference.cs"], ["/r:Mono.Cecil.dll"])
            generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs", repo / "host/winrt/component_generation.cs", repo / "host/winrt/wpf_generation.cs"], ["/r:Mono.Cecil.dll"])
            generated = output / "Windows.generated.cs"
            run("generate", [generator.name, windows(metadata), windows(repo / "host/winrt/bootstrap.xml"), windows(generated)])
            check_storage_metadata(generator)
            runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs")]
            projection = compile_file("projection/Windows.dll", runtime + [repo / "host/winrt/storage_abi.cs", repo / "host/winrt/array_buffer.cs", generated], ["/target:library"])
            probe_storage(marker, projection)
            return 0
        if args.surface_only:
            # The release Windows.dll: the interop bootstrap completed against every SDK type.
            generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs", repo / "host/winrt/component_generation.cs", repo / "host/winrt/wpf_generation.cs"], ["/r:Mono.Cecil.dll"])
            generated = output / "Windows.generated.cs"
            run("generate", [generator.name, windows(metadata), windows(repo / "host/winrt/bootstrap.xml"), windows(generated), "--interop"])
            bridge = compile_file("interop/AppxPortable.WinRT.Interop.dll", [repo / "host/winrt/interop.cs"], ["/target:library"])
            abi_flags.extend(["/define:APPX_PORTABLE_INTEROP", "/r:" + windows(bridge)])
            fixture_support.append(bridge)
            runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs", "storage_abi.cs", "array_buffer.cs")]
            bootstrap = compile_file("interop-projection/Windows.dll", runtime + [generated], ["/target:library"] + abi_flags)
            completer = compile_file("complete.exe", [repo / "host/winrt/complete.cs"], ["/r:Mono.Cecil.dll"])
            surface = output / "surface/Windows.dll"
            surface.parent.mkdir(exist_ok=True)
            copy_owned_file(bridge, surface.parent / bridge.name)
            report = output / "surface-report.txt"
            run("complete-surface", [completer.name, "--all", windows(metadata), windows(bootstrap), windows(surface), windows(report)],
                marker="Completed ", timeout=900)
            loader = compile_file("surface-load.exe", [repo / "tests/winrt_projection/surface_load.cs"])
            run("surface-load", [loader.name, windows(surface)], marker="PASS: surface loads", timeout=600)
            surface_build.update(completer=completer, bootstrap=bootstrap, surface=surface, interop=bridge)
            if args.consumer:
                # Nothing the given consumers reference may be missing from the surface.
                rechecked = output / "surface-rechecked/Windows.dll"
                rechecked.parent.mkdir(exist_ok=True)
                run("surface-consumers", [completer.name, windows(metadata), windows(surface), windows(rechecked)] + [windows(p.resolve()) for p in args.consumer],
                    marker="Completed 0 types and 0 members", timeout=300)
            unimplemented = compile_file("surface-unimplemented.exe", [repo / "tests/winrt_projection/surface_load.cs"],
                                         ["/define:UNIMPLEMENTED", "/r:" + windows(surface)])  # stubs exist only in the surface
            folder = fixture("surface-unimplemented", unimplemented.name, "SurfaceTest", "CN=projection")
            copy_owned_file(unimplemented, folder / unimplemented.name)
            copy_owned_file(surface, folder / "Windows.dll")
            run("surface-unimplemented", [unimplemented.name], cwd=folder, environment={"APPX_PORTABLE_LOG": "1"},
                marker="PASS: unimplemented surface members name themselves")
            trace = (folder / "appx-portable.log").read_text(errors="replace") if (folder / "appx-portable.log").is_file() else ""
            require("UNIMPL managed Windows.Networking.Proximity.PeerFinder.Start" in trace,
                    "an unimplemented surface member was not named in the shim log")
            # Implemented members keep working once every stub surrounds them.
            tasks = probe_async(bootstrap)
            probe_io(bootstrap, tasks)
            probe_page(bootstrap)
            return 0
        if args.component_only:
            metadata_builder = compile_file("component-metadata.exe", [repo / "tests/winrt_projection/component_metadata.cs"], ["/r:Mono.Cecil.dll"])
            component_metadata = output / "Example.Component.winmd"
            run("component-metadata", [metadata_builder.name, windows(component_metadata)])
            generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs", repo / "host/winrt/component_generation.cs", repo / "host/winrt/wpf_generation.cs"], ["/r:Mono.Cecil.dll"])
            generated = output / "Component.generated.cs"
            run("generate-component", [generator.name, "--component", windows(component_metadata), windows(generated)])
            runtime = [repo / "host/winrt" / n for n in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs")]
            projection = compile_file("component-projection/Example.Component.dll", runtime + [generated], ["/target:library"])
            consumer = compile_file("component.exe", [repo / "tests/winrt_projection/component.cs"], ["/r:" + windows(projection)])
            marker = compile_file("mark-component.exe", [repo / "tests/winrt_projection/mark_reference.cs"], ["/r:Mono.Cecil.dll"])
            run("mark-component", [marker.name, consumer.name, "component-winrt.exe", "--assembly", "Example.Component"], marker="flags=WindowsRuntime")
            host = compile_file("appx-host.exe", [repo / "host/managed.cs", repo / "host/projections.cs"], ["/nowarn:0219", "/r:" + windows(references / "System.Windows.Forms.dll")])
            folder = fixture("component-game", consumer.name, "ComponentProjectionTest", "CN=projection")
            manifest = folder / "AppxManifest.xml"
            text = manifest.read_text().replace("</Applications>", '</Applications><Extensions><Extension Category="windows.activatableClass.inProcessServer"><InProcessServer><Path>Example.Component.dll</Path><ActivatableClass ActivatableClassId="Example.Component.Engine" ThreadingModel="both"/><ActivatableClass ActivatableClassId="Example.Component.Gauge" ThreadingModel="both"/></InProcessServer></Extension></Extensions>')
            manifest.write_text(text)
            copy_owned_file(output / "component-winrt.exe", folder / consumer.name)
            copy_owned_file(host, folder / host.name)
            native = shim.parent / "component_projection_fixture.dll"
            dependency = shim.parent / "component_dependency_fixture.dll"
            require(native.is_file() and dependency.is_file(), "build component_projection_fixture first")
            copy_owned_file(native, folder / "Example.Component.dll")
            copy_owned_file(dependency, folder / dependency.name)
            projected = folder / "appx-projections"
            projected.mkdir(exist_ok=True)
            copy_owned_file(projection, projected / projection.name)
            run("component-native", [host.name, "--log"], cwd=folder, marker="PASS: generic component ABI", evidence=folder / "managed.log")
            require("component callback failure" in (folder / "appx-portable.log").read_text(), "retained callback failure was not logged")
            (output / "unsupported").mkdir(exist_ok=True)
            run("mismatched-component-reference", [metadata_builder.name, "--bad-reference", windows(output / "component-winrt.exe"), windows(output / "component-mismatch.exe")])
            copy_owned_file(output / "component-mismatch.exe", folder / consumer.name)
            run("reject-component-reference", [host.name, "--log"], cwd=folder, succeeds=False, marker="Generated component projection identity does not match", evidence=folder / "managed.log")
            copy_owned_file(output / "component-winrt.exe", folder / consumer.name)
            run("unsupported-component-metadata", [metadata_builder.name, windows(output / "unsupported/Example.Component.winmd"), "unsupported"])
            # A member the generator cannot express becomes a named stub; the component still projects.
            run("stub-component-signature", [generator.name, "--component", windows(output / "unsupported/Example.Component.winmd"), windows(output / "unsupported.cs")],
                marker="Stubbed Example.Component.Engine.Echo: Unsupported parameter:")
            require('throw global::AppxPortable.WinRT.Errors.Unimplemented("Example.Component.Engine.Echo")' in (output / "unsupported.cs").read_text(encoding="utf-8"),
                    "an unsupported component member was not stubbed with its name")
            compile_file("unsupported-projection/Example.Component.dll", runtime + [output / "unsupported.cs"], ["/target:library"])
            for identity in ("culture", "signed"):
                run("component-identity-" + identity, [metadata_builder.name, windows(output / "unsupported/Example.Component.winmd"), identity])
                run("reject-component-identity-" + identity, [generator.name, "--component", windows(output / "unsupported/Example.Component.winmd"), windows(output / "identity.cs")], succeeds=False, marker="Unsupported component assembly identity:")

            run("external-metadata", [metadata_builder.name, windows(output / "Example.External.winmd"), "external"])
            run("generate-external", [generator.name, "--component", windows(output / "Example.External.winmd"), windows(output / "External.generated.cs"), "--interop"])
            bridge = compile_file("interop/AppxPortable.WinRT.Interop.dll", [repo / "host/winrt/interop.cs"], ["/target:library"])
            interop_flags = ["/target:library", "/define:APPX_PORTABLE_INTEROP", "/r:" + windows(bridge)]
            lifetime = compile_file("interop-lifetime.exe", [repo / "tests/winrt_projection/interop_lifetime.cs"], ["/r:" + windows(bridge)])
            copy_owned_file(bridge, output / bridge.name)
            run("interop-lease-lifetime", [lifetime.name], marker="PASS: interop lease ownership")
            windows_generated = output / "Windows.interop.cs"
            run("generate-windows-interop", [generator.name, windows(metadata), windows(repo / "host/winrt/bootstrap.xml"), windows(windows_generated), "--interop"])
            windows_runtime = runtime + [repo / "host/winrt/storage_abi.cs", repo / "host/winrt/array_buffer.cs"]
            windows_interop = compile_file("windows-interop/Windows.dll", windows_runtime + [windows_generated], interop_flags)

            external_projection = compile_file("external-projection/Example.External.dll", runtime + [output / "External.generated.cs"], interop_flags)
            cross_metadata = output / "cross/Example.Component.winmd"
            cross_metadata.parent.mkdir(exist_ok=True)
            run("cross-metadata", [metadata_builder.name, windows(cross_metadata), "cross"])
            run("generate-cross", [generator.name, "--component", windows(cross_metadata), windows(output / "Cross.generated.cs"), "--reference", windows(external_projection)])
            run("reject-unmarked-reference", [generator.name, "--component", windows(cross_metadata), windows(output / "unmarked.cs"), "--reference", windows(projection)], succeeds=False, marker="Reference is not an interop projection:")
            copy_owned_file(external_projection, output / "Renamed.dll")
            run("reject-renamed-reference", [generator.name, "--component", windows(cross_metadata), windows(output / "renamed.cs"), "--reference", windows(output / "Renamed.dll")], succeeds=False, marker="Projection reference filename does not match assembly identity:")
            for identity in ("version", "culture", "token"):
                source = output / ("cross-" + identity) / "Example.Component.winmd"
                source.parent.mkdir(exist_ok=True)
                run("cross-metadata-" + identity, [metadata_builder.name, windows(source), "cross-" + identity])
                run("reject-cross-identity-" + identity, [generator.name, "--component", windows(source), windows(output / "identity-cross.cs"), "--reference", windows(external_projection)], succeeds=False, marker="External projection identity mismatch for")
            cross = compile_file("cross-projection/Example.Component.dll", runtime + [output / "Cross.generated.cs"], interop_flags + ["/r:" + windows(external_projection)])
            cross_consumer = compile_file("component-external.exe", [repo / "tests/winrt_projection/component_external.cs"], ["/r:" + windows(cross), "/r:" + windows(external_projection), "/r:" + windows(bridge)])
            run("mark-cross-component", [marker.name, cross_consumer.name, "cross-marked.exe", "--assembly", "Example.Component"], marker="flags=WindowsRuntime")
            run("mark-cross-external", [marker.name, "cross-marked.exe", "cross-winrt.exe", "--assembly", "Example.External"], marker="flags=WindowsRuntime")
            folder = fixture("cross-game", cross_consumer.name, "ExternalProjectionTest", "CN=projection")
            manifest = folder / "AppxManifest.xml"
            manifest.write_text(manifest.read_text().replace("</Applications>", '</Applications><Extensions><Extension Category="windows.activatableClass.inProcessServer"><InProcessServer><Path>Example.Component.dll</Path><ActivatableClass ActivatableClassId="Example.Component.Engine" ThreadingModel="both"/><ActivatableClass ActivatableClassId="Example.External.Node" ThreadingModel="both"/></InProcessServer></Extension></Extensions>'))
            for source in (host, cross_consumer): copy_owned_file(source, folder / source.name)
            copy_owned_file(output / "cross-winrt.exe", folder / cross_consumer.name)
            copy_owned_file(native, folder / "Example.Component.dll")
            copy_owned_file(dependency, folder / dependency.name)
            target = folder / "appx-projections"
            target.mkdir(exist_ok=True)
            for source in (bridge, cross, external_projection): copy_owned_file(source, target / source.name)
            run("external-component-native", [host.name, "--log"], cwd=folder, marker="PASS: external component projection", evidence=folder / "managed.log")
            wrong_reference = output / "wrong-reference/Example.External.dll"
            wrong_reference.parent.mkdir(exist_ok=True)
            run("mutate-external-iid", [metadata_builder.name, "--bad-projection-iid", windows(external_projection), windows(wrong_reference)])
            run("generate-wrong-external-iid", [generator.name, "--component", windows(cross_metadata), windows(output / "WrongCross.cs"), "--reference", windows(wrong_reference)])
            wrong_cross = compile_file("wrong-cross/Example.Component.dll", runtime + [output / "WrongCross.cs"], interop_flags + ["/r:" + windows(external_projection)])
            copy_owned_file(wrong_cross, target / cross.name)
            run("reject-external-iid", [host.name, "--log", "reject-iid"], cwd=folder, marker="PASS: external IID rejection", evidence=folder / "managed.log")
            copy_owned_file(cross, target / cross.name)
            if args.component_metadata:
                source = args.component_metadata.resolve()
                real_generated = output / "PackageComponent.generated.cs"
                run("generate-package-component", [generator.name, "--component", windows(source), windows(real_generated), "--reference", windows(windows_interop)])
                compile_file("package-component/" + source.stem + ".dll", runtime + [real_generated], interop_flags + ["/r:" + windows(windows_interop)])


            return 0
        if args.xbox_only:
            marker = compile_file("mark-reference.exe", [repo / "tests/winrt_projection/mark_reference.cs"], ["/r:Mono.Cecil.dll"])
            generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs", repo / "host/winrt/component_generation.cs", repo / "host/winrt/wpf_generation.cs"], ["/r:Mono.Cecil.dll"])
            completer = compile_file("complete.exe", [repo / "host/winrt/complete.cs"], ["/r:Mono.Cecil.dll"])
            probe_xbox(marker, generator, completer)
            return 0
        if args.runtime_case:
            executable = compile_file("runtime.exe", [repo / "host/winrt/abi.cs", repo / "host/winrt/ccw.cs", repo / "host/winrt/dispatcher.cs", repo / "host/winrt/property_abi.cs", repo / "host/winrt/collections.cs",
                                      repo / "tests/winrt_projection/runtime.cs"])
            run("runtime-" + args.runtime_case, [executable.name, args.runtime_case], marker="PASS: runtime " + args.runtime_case)
            return 0
        runtime = compile_file("runtime.exe", [repo / "host/winrt/abi.cs", repo / "host/winrt/ccw.cs", repo / "host/winrt/dispatcher.cs", repo / "host/winrt/property_abi.cs", repo / "host/winrt/collections.cs",
                               repo / "tests/winrt_projection/runtime.cs"])
        for case in ("inspectable", "identity", "stale", "invocation", "reentrant"):
            run("runtime-" + case, [runtime.name, case], marker="PASS: runtime " + case)
        # The earlier bridge remains an independent native ownership/HRESULT check.
        bridge = compile_file("AppxPortable.WinRT.dll", [repo / "host/winrt" / name
                              for name in ("abi.cs", "ccw.cs", "dispatcher.cs", "property_abi.cs", "collections.cs", "package.cs", "assembly.cs")], ["/target:library"])
        legacy = compile_file("managed-winrt-bridge.exe", [repo / "tests/managed_winrt_bridge.cs"], ["/r:" + windows(bridge)])
        folder = fixture("legacy", legacy.name, "ManagedWinrtBridgeTest", "CN=bridge-đá-💎")
        for path in (bridge, legacy):
            copy_owned_file(path, folder / path.name)
        run("legacy-bridge", [legacy.name], cwd=folder, marker="PASS: managed WinRT activation")
        reference = compile_file("reference/Windows.dll", [repo / "tests/winrt_projection/reference.cs"], ["/target:library"])
        consumer = compile_file("consumer.exe", [repo / "tests/winrt_projection/consumer.cs"], ["/r:" + windows(reference)])
        marker = compile_file("mark-reference.exe", [repo / "tests/winrt_projection/mark_reference.cs"], ["/r:Mono.Cecil.dll"])
        run("mark-reference", [marker.name, consumer.name, "consumer-winrt.exe"], marker="flags=WindowsRuntime")
        folder = fixture("fixture", consumer.name, "GeneratedProjectionTest", "CN=projection-đá-💎")
        copy_owned_file(output / "consumer-winrt.exe", folder / consumer.name)
        copy_owned_file(reference, folder / "Windows.dll")
        run("unimplemented-contract", [consumer.name], cwd=folder, succeeds=False,
            marker="NotImplementedException: Windows.ApplicationModel.Package.Current")
        generator = compile_file("generator.exe", [repo / "host/winrt/generator.cs", repo / "host/winrt/storage_generation.cs", repo / "host/winrt/component_generation.cs", repo / "host/winrt/wpf_generation.cs"], ["/r:Mono.Cecil.dll"])
        generated = output / "Windows.generated.cs"
        surface = repo / "host/winrt/bootstrap.xml"
        run("generate", [generator.name, windows(metadata), windows(surface), windows(generated)])
        text = generated.read_text(encoding="utf-8")
        missing = [name for name, iid in HEADER_IIDS.items() if 'System.Guid("' + iid + '")' not in text]
        missing += [slot for slot in HEADER_SLOTS if "// " + slot + "\n" not in text]
        missing += [delegate for delegate in HEADER_DELEGATES if delegate not in text]
        runtime_source = (repo / "host/winrt/dispatcher.cs").read_text(encoding="utf-8")
        missing += [value for value in HEADER_RUNTIME if value not in runtime_source]
        results.append(dict(case="header-values", missing=missing, passed=not missing))
        require(not missing, "generated IIDs or slots differ from the SDK headers: " + ", ".join(missing))
        projection = compile_file("projection/Windows.dll", [repo / "host/winrt/abi.cs", repo / "host/winrt/ccw.cs", repo / "host/winrt/dispatcher.cs", repo / "host/winrt/property_abi.cs", repo / "host/winrt/collections.cs", repo / "host/winrt/storage_abi.cs", repo / "host/winrt/array_buffer.cs", generated], ["/target:library"])
        deploy(projection, folder / "Windows.dll")
        run("generated-contract", [consumer.name], cwd=folder, marker="PASS: generated Windows projection")
        invalid = [
            ("unknown-member", '<Class Name="Windows.ApplicationModel.Package"><Unknown Name="NoOp" /></Class>', "Unsupported member kind"),
            ("duplicate-property", '<Class Name="Windows.ApplicationModel.Package"><Property Name="Current" /><Property Name="Current" /></Class>', "Duplicate property"),
            ("unsupported-return", '<Class Name="Windows.ApplicationModel.Package"><Property Name="InstalledLocation" /></Class>', "Windows.Storage.StorageFolder"),
        ]
        for label, body, expected in invalid:
            bad = output / (label + ".xml")
            bad.write_text("<Projection>" + body + "</Projection>")
            target = output / (label + ".cs")
            if target.exists():
                target.unlink()
            run(label, [generator.name, windows(metadata), windows(bad), windows(target)], succeeds=False, marker=expected)
            require(not target.exists(), "invalid generation left a usable output: " + label)
        mutator = compile_file("mutate-metadata.exe", [repo / "tests/winrt_projection/mutate_metadata.cs"], ["/r:Mono.Cecil.dll"])
        mutated = output / "mutated-metadata"
        mutation = run("mutate-metadata", [mutator.name, windows(metadata), windows(mutated)])
        changed = re.search(r"Changed IID first word=([0-9a-f]{8})", mutation).group(1)
        target = output / "Mutated.generated.cs"
        run("generate-mutated", [generator.name, windows(mutated), windows(surface), windows(target)])
        text = target.read_text()
        require(changed + "-37c7-4790-9980-dd7ae74e8bb2" in text, "IID did not follow metadata")
        require("native.String(7)" in text, "method slot did not follow metadata")
        mutant = compile_file("mutant/Windows.dll", [repo / "host/winrt/abi.cs", repo / "host/winrt/ccw.cs", repo / "host/winrt/dispatcher.cs", repo / "host/winrt/property_abi.cs", repo / "host/winrt/collections.cs", repo / "host/winrt/storage_abi.cs", repo / "host/winrt/array_buffer.cs", target], ["/target:library"])
        try:
            copy_owned_file(mutant, folder / "Windows.dll")
            run("changed-iid-rejected", [consumer.name], cwd=folder, succeeds=False, marker="80004002")
        finally:
            deploy(projection, folder / "Windows.dll")
        application = compile_file("application.exe", [repo / "tests/winrt_projection/application.cs"], ["/r:" + windows(reference)])
        run("mark-application", [marker.name, application.name, "application-winrt.exe"], marker="flags=WindowsRuntime")
        app = fixture("application", application.name, "ApplicationProjectionTest", "CN=projection")
        copy_owned_file(output / "application-winrt.exe", app / application.name)
        copy_owned_file(projection, app / "Windows.dll")
        run("application-launch", [application.name], cwd=app, marker="PASS: managed App.OnLaunched ran on the shim view thread")
        launch = (output / "application-launch.log").read_text(errors="replace")
        require("PASS: managed dependency properties use the shim value store" in launch,
                "dependency-property fixture did not finish")
        require("PASS: Suspending raised on the managed App" in launch and "UNUSED HANDLER" not in launch,
                "Suspending must reach the subscribed handler and not the removed one")
        run("mark-abi-setters", [marker.name, application.name, "application-abi.exe", "--abi-setters"], marker="flags=WindowsRuntime")
        copy_owned_file(output / "application-abi.exe", app / "application-abi.exe")
        run("abi-setter-before-completion", ["application-abi.exe"], cwd=app, succeeds=False, marker="put_Content")
        run("application-exception", [application.name, "throw"], cwd=app, marker="PASS: OnLaunched exception crossed the native boundary; callback released")
        unimplemented = compile_file("unimplemented.exe", [repo / "tests/winrt_projection/unimplemented.cs"], ["/r:" + windows(reference)])
        run("mark-unimplemented", [marker.name, unimplemented.name, "unimplemented-winrt.exe"], marker="flags=WindowsRuntime")
        missing = fixture("unimplemented", unimplemented.name, "UnimplementedProjectionTest", "CN=projection")
        copy_owned_file(output / "unimplemented-winrt.exe", missing / unimplemented.name)
        copy_owned_file(projection, missing / "Windows.dll")
        run("before-completion", [unimplemented.name], cwd=missing, succeeds=False, marker="UNBOUND:")
        resolver = compile_file("resolve-references.exe", [repo / "tests/winrt_projection/resolve_references.cs"], ["/r:Mono.Cecil.dll"])
        completer = compile_file("complete.exe", [repo / "host/winrt/complete.cs"], ["/r:Mono.Cecil.dll"])
        consumers = [output / "unimplemented-winrt.exe", output / "consumer-winrt.exe", output / "application-winrt.exe", output / "application-abi.exe"]
        consumers += [p.resolve() for p in args.consumer]
        run("resolve-before-completion", [resolver.name, windows(projection.parent)] + [windows(p) for p in consumers],
            succeeds=False, marker="UNRESOLVED ")
        completed = output / "completed/Windows.dll"
        completed.parent.mkdir(exist_ok=True)
        run("complete", [completer.name, windows(metadata), windows(projection), windows(completed)] + [windows(p) for p in consumers], marker="Completed ")
        run("resolve-completed", [resolver.name, windows(completed.parent)] + [windows(p) for p in consumers], marker="0 unresolved")
        copy_owned_file(completed, missing / "Windows.dll")
        run("after-completion", [unimplemented.name], cwd=missing, marker="UNIMPLEMENTED: Windows.Devices.Geolocation.Geolocator..ctor",
            environment={"APPX_PORTABLE_LOG": "1"})
        # A stub names itself in the native log even when the game swallows the exception.
        trace = (missing / "appx-portable.log").read_text(errors="replace") if (missing / "appx-portable.log").is_file() else ""
        require("UNIMPL managed Windows.Devices.Geolocation.Geolocator..ctor" in trace,
                "completion stub did not report itself to the native log")
        # Completion must keep implemented members native, not replace them with stubs.
        copy_owned_file(completed, folder / "Windows.dll")
        run("completed-contract", [consumer.name], cwd=folder, marker="PASS: generated Windows projection")
        copy_owned_file(completed, app / "Windows.dll")
        run("completed-application", [application.name], cwd=app, marker="PASS: managed App.OnLaunched ran on the shim view thread")
        run("completed-abi-setter", ["application-abi.exe"], cwd=app, marker="PASS: managed App.OnLaunched ran on the shim view thread")
        check_storage_metadata(generator)
        probe_storage(marker, projection)
        probe_page(projection)
        probe_splash(projection, generator)
        probe_network(projection, generator)
        check_io_metadata(generator)
        task_bridge = probe_async(projection)
        probe_io(projection, task_bridge)
        xbox_projection = probe_xbox(marker, generator, completer) if args.xbox_metadata else None
        if args.game:
            source = args.game
            paths = [source / "appx-host.exe", source / "AppxManifest.xml"] + [p.resolve() for p in args.consumer]
            paths += [source / name for name in ("appxportable.dll", "Windows.dll", "Microsoft.Xbox.dll", "System.Runtime.WindowsRuntime.dll",
                                                 "System.Runtime.WindowsRuntime.UI.Xaml.dll") if (source / name).is_file()]
            def hashes():
                return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
            before = hashes()
            game = output / "game"
            require(not game.exists(), "game output already exists; choose a fresh --output directory")
            # Keep links as links: an installed game's own Wine prefix maps z: to /.
            # The launch uses --prefix, so that prefix is not copied at all.
            shutil.copytree(source, game, symlinks=True, ignore=shutil.ignore_patterns("*.log", ".wine-prefix"))
            copy_owned_file(shim, game / "appxportable.dll")
            copy_owned_file(completed, game / "Windows.dll")
            copy_owned_file(task_bridge, game / task_bridge.name)
            xaml_facade = task_bridge.with_name("System.Runtime.WindowsRuntime.UI.Xaml.dll")
            copy_owned_file(xaml_facade, game / xaml_facade.name)
            if xbox_projection:
                copy_owned_file(xbox_projection, game / "Microsoft.Xbox.dll")
            run("game-launch", ["appx-host.exe", "--trace"], cwd=game, succeeds=False,
                marker=args.expected_game_error, evidence=game / "managed.log")
            require(before == hashes(), "source game files changed during probe")
            (output / "game-inputs.json").write_text(json.dumps(before, indent=2) + "\n")
        return 0
    except (OSError, RuntimeError) as error:
        print(error)
        return 1
    finally:
        (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
