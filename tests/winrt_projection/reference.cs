using System.Reflection;
[assembly: AssemblyVersion("255.255.255.255")]
namespace Windows.System
{
    public enum ProcessorArchitecture { X86 = 0, X64 = 9, Unknown = 65535 }
}
namespace Windows.ApplicationModel
{
    public sealed class SuspendingEventArgs { }
    public sealed class Package
    {
        public static Package Current { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.Package.Current"); } }
        public PackageId Id { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.Package.Id"); } }
    }
    public sealed class PackageId
    {
        public string Name { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.PackageId.Name"); } }
        public string Publisher { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.PackageId.Publisher"); } }
        public string ResourceId { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.PackageId.ResourceId"); } }
        public global::Windows.System.ProcessorArchitecture Architecture { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.PackageId.Architecture"); } }
    }
}
namespace Windows.UI.Xaml
{
    public sealed class ApplicationInitializationCallbackParams { }
    public delegate void ApplicationInitializationCallback(ApplicationInitializationCallbackParams p);
    public delegate void SuspendingEventHandler(object sender, global::Windows.ApplicationModel.SuspendingEventArgs e);
    public class Application
    {
        public Application() { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application..ctor"); }
        public static void LoadComponent(object component, global::System.Uri uri) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.LoadComponent"); }
        public static void LoadComponent(object component, global::System.Uri uri, global::Windows.UI.Xaml.Controls.Primitives.ComponentResourceLocation location) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.LoadComponent"); }
        public static Application Current { get { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.Current"); } }
        public static void Start(ApplicationInitializationCallback callback) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.Start"); }
        public void Exit() { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.Exit"); }
        // A WinRT event as consumers bind it: token-returning add, token-taking remove.
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_Suspending(SuspendingEventHandler handler) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.add_Suspending"); }
        public void remove_Suspending(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.remove_Suspending"); }
        protected virtual void OnLaunched(global::Windows.ApplicationModel.Activation.LaunchActivatedEventArgs args) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.OnLaunched"); }
        protected virtual void OnActivated(global::Windows.ApplicationModel.Activation.IActivatedEventArgs args) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.OnActivated"); }
    }
}
namespace Windows.UI.Xaml
{
    public sealed class Window
    {
        public static Window Current { get { throw new global::System.NotImplementedException("Windows.UI.Xaml.Window.Current"); } }
        public UIElement Content
        {
            get { throw new global::System.NotImplementedException("Windows.UI.Xaml.Window.Content"); }
            set { throw new global::System.NotImplementedException("Windows.UI.Xaml.Window.Content"); }
        }
        public void Activate() { throw new global::System.NotImplementedException("Windows.UI.Xaml.Window.Activate"); }
    }
    public sealed class PropertyMetadata { }
    public sealed class DependencyProperty
    {
        public static DependencyProperty RegisterAttached(string name, global::System.Type type, global::System.Type owner, PropertyMetadata metadata) { throw new global::System.NotImplementedException("Windows.UI.Xaml.DependencyProperty.RegisterAttached"); }
        public static DependencyProperty Register(string name, global::System.Type type, global::System.Type owner, PropertyMetadata metadata) { throw new global::System.NotImplementedException("Windows.UI.Xaml.DependencyProperty.Register"); }
        public static object UnsetValue { get { throw new global::System.NotImplementedException("Windows.UI.Xaml.DependencyProperty.UnsetValue"); } }
    }
    public class DependencyObject
    {
        internal DependencyObject() { }
        public object GetValue(DependencyProperty property) { throw new global::System.NotImplementedException("Windows.UI.Xaml.DependencyObject.GetValue"); }
        public void SetValue(DependencyProperty property, object value) { throw new global::System.NotImplementedException("Windows.UI.Xaml.DependencyObject.SetValue"); }
        public void ClearValue(DependencyProperty property) { throw new global::System.NotImplementedException("Windows.UI.Xaml.DependencyObject.ClearValue"); }
        public object ReadLocalValue(DependencyProperty property) { throw new global::System.NotImplementedException("Windows.UI.Xaml.DependencyObject.ReadLocalValue"); }
    }
    public class UIElement : DependencyObject { internal UIElement() { } }
    public class FrameworkElement : UIElement
    {
        internal FrameworkElement() { }
        public ResourceDictionary Resources { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public object FindName(string name) { throw new global::System.NotImplementedException(); }
    }
}
namespace Windows.UI.Xaml.Controls
{
    public class Image : global::Windows.UI.Xaml.FrameworkElement { internal Image() { } }
    public class Control : global::Windows.UI.Xaml.FrameworkElement { internal Control() { } }
    public class ProgressRing : Control
    {
        internal ProgressRing() { }
        public bool IsActive { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
    public class ContentControl : Control { internal ContentControl() { } }
    public class UserControl : Control
    {
        internal UserControl() { }
        public global::Windows.UI.Xaml.UIElement Content { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
    public class Page : UserControl
    {
        public Page() { throw new global::System.NotImplementedException("Windows.UI.Xaml.Controls.Page..ctor"); }
        public Frame Frame { get { throw new global::System.NotImplementedException(); } }
        protected virtual void OnNavigatedTo(global::Windows.UI.Xaml.Navigation.NavigationEventArgs args) { throw new global::System.NotImplementedException(); }
        protected virtual void OnNavigatedFrom(global::Windows.UI.Xaml.Navigation.NavigationEventArgs args) { throw new global::System.NotImplementedException(); }
        protected virtual void OnNavigatingFrom(global::Windows.UI.Xaml.Navigation.NavigatingCancelEventArgs args) { throw new global::System.NotImplementedException(); }
    }
    public class Frame : ContentControl
    {
        public Frame() { throw new global::System.NotImplementedException("Windows.UI.Xaml.Controls.Frame..ctor"); }
    }
}
namespace Windows.Devices.Geolocation
{
    // Outside the implemented surface: completion must stub it, naming itself.
    public sealed class Geolocator
    {
        public Geolocator() { throw new global::System.NotImplementedException("Windows.Devices.Geolocation.Geolocator..ctor"); }
    }
}
namespace Windows.ApplicationModel.Activation
{
    // Values match WinMD: they are compiled into consumers as constants.
    public enum ActivationKind { Launch = 0, Search = 1 }
    public enum ApplicationExecutionState { NotRunning = 0, Running = 1 }
    public interface IActivatedEventArgs
    {
        ActivationKind Kind { get; }
        ApplicationExecutionState PreviousExecutionState { get; }
        SplashScreen SplashScreen { get; }
    }
    public sealed class LaunchActivatedEventArgs : IActivatedEventArgs
    {
        public string Arguments { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.Activation.LaunchActivatedEventArgs.Arguments"); } }
        public ActivationKind Kind { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.Activation.LaunchActivatedEventArgs.Kind"); } }
        public ApplicationExecutionState PreviousExecutionState { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.Activation.LaunchActivatedEventArgs.PreviousExecutionState"); } }
        public SplashScreen SplashScreen { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.Activation.IActivatedEventArgs.get_SplashScreen"); } }
    }
}
namespace Windows.UI.Core
{
    public sealed class CoreWindow
    {
        public static CoreWindow GetForCurrentThread() { throw new global::System.NotImplementedException("Windows.UI.Core.CoreWindow.GetForCurrentThread"); }
        public CoreDispatcher Dispatcher { get { throw new global::System.NotImplementedException("Windows.UI.Core.CoreWindow.Dispatcher"); } }
    }
    public sealed class CoreDispatcher { internal CoreDispatcher() { } }
}
namespace Windows.Foundation.Collections
{
    public interface IPropertySet : global::System.Collections.Generic.IDictionary<string, object> { }
}
namespace Windows.Storage
{
    public enum ApplicationDataLocality { Local = 0, Roaming = 1, Temporary = 2 }
    public enum ApplicationDataCreateDisposition { Always = 0, Existing = 1 }
    public sealed class ApplicationData
    {
        public static ApplicationData Current { get { throw new global::System.NotImplementedException("Windows.Storage.ApplicationData.Current"); } }
        public ApplicationDataContainer LocalSettings { get { throw new global::System.NotImplementedException("Windows.Storage.ApplicationData.LocalSettings"); } }
        public ApplicationDataContainer RoamingSettings { get { throw new global::System.NotImplementedException("Windows.Storage.ApplicationData.RoamingSettings"); } }
    }
    public sealed class ApplicationDataContainer
    {
        public string Name { get { throw new global::System.NotImplementedException("Windows.Storage.ApplicationDataContainer.Name"); } }
        public ApplicationDataLocality Locality { get { throw new global::System.NotImplementedException("Windows.Storage.ApplicationDataContainer.Locality"); } }
        public global::Windows.Foundation.Collections.IPropertySet Values { get { throw new global::System.NotImplementedException("Windows.Storage.ApplicationDataContainer.Values"); } }
        public ApplicationDataContainer CreateContainer(string name, ApplicationDataCreateDisposition disposition) { throw new global::System.NotImplementedException("Windows.Storage.ApplicationDataContainer.CreateContainer"); }
        public void DeleteContainer(string name) { throw new global::System.NotImplementedException("Windows.Storage.ApplicationDataContainer.DeleteContainer"); }
    }
}
namespace Windows.Networking.Connectivity
{
    public enum NetworkConnectivityLevel { None, LocalAccess, ConstrainedInternetAccess, InternetAccess }
    public delegate void NetworkStatusChangedEventHandler(object sender);
    public sealed class ConnectionProfile
    {
        public string ProfileName { get { throw new global::System.NotImplementedException(); } }
        public NetworkConnectivityLevel GetNetworkConnectivityLevel() { throw new global::System.NotImplementedException(); }
    }
    public static class NetworkInformation
    {
        public static ConnectionProfile GetInternetConnectionProfile() { throw new global::System.NotImplementedException("Windows.Networking.Connectivity.NetworkInformation.GetInternetConnectionProfile"); }
        public static global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_NetworkStatusChanged(NetworkStatusChangedEventHandler handler) { throw new global::System.NotImplementedException(); }
        public static void remove_NetworkStatusChanged(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
}

namespace Windows.Foundation
{
    public delegate void TypedEventHandler<TSender, TArgs>(TSender sender, TArgs args);
}
namespace Windows.ApplicationModel.Activation
{
    public sealed class SplashScreen
    {
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_Dismissed(global::Windows.Foundation.TypedEventHandler<SplashScreen, object> handler) { throw new global::System.NotImplementedException(); }
        public void remove_Dismissed(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
}

namespace Windows.UI.Xaml.Navigation
{
    public sealed class NavigationEventArgs
    {
        public global::System.Uri Uri { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
    public sealed class NavigatingCancelEventArgs { }
}

namespace Windows.UI.Xaml.Markup
{
    public interface IComponentConnector { void Connect(int connectionId, object target); }
}

namespace Windows.UI.Xaml.Controls.Primitives
{
    public enum ComponentResourceLocation { Application, Nested }
}

namespace Windows.UI.Xaml
{
    public class ResourceDictionary : DependencyObject
    {
        public ResourceDictionary() { throw new global::System.NotImplementedException(); }
    }
}

namespace Windows.UI.Xaml.Media.Animation
{
    public class Timeline : global::Windows.UI.Xaml.DependencyObject { internal Timeline() { } }
    public class Storyboard : Timeline
    {
        public Storyboard() { throw new global::System.NotImplementedException(); }
        public void Begin() { throw new global::System.NotImplementedException(); }
    }
    public class FadeInThemeAnimation : Timeline { internal FadeInThemeAnimation() { } }
    public class FadeOutThemeAnimation : Timeline { internal FadeOutThemeAnimation() { } }
}
