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
    public class DependencyObject { internal DependencyObject() { } }
    public class UIElement : DependencyObject { internal UIElement() { } }
    public class FrameworkElement : UIElement { internal FrameworkElement() { } }
}
namespace Windows.UI.Xaml.Controls
{
    public class Control : global::Windows.UI.Xaml.FrameworkElement { internal Control() { } }
    public class ContentControl : Control { internal ContentControl() { } }
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
    }
    public sealed class LaunchActivatedEventArgs : IActivatedEventArgs
    {
        public string Arguments { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.Activation.LaunchActivatedEventArgs.Arguments"); } }
        public ActivationKind Kind { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.Activation.LaunchActivatedEventArgs.Kind"); } }
        public ApplicationExecutionState PreviousExecutionState { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.Activation.LaunchActivatedEventArgs.PreviousExecutionState"); } }
    }
}
