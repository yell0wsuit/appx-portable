using System.Reflection;
[assembly: AssemblyVersion("255.255.255.255")]
namespace Windows.System
{
    public enum ProcessorArchitecture { X86 = 0, X64 = 9, Unknown = 65535 }
}
namespace Windows.ApplicationModel
{
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
    public class Application
    {
        public Application() { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application..ctor"); }
        public static Application Current { get { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.Current"); } }
        public static void Start(ApplicationInitializationCallback callback) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.Start"); }
        public void Exit() { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.Exit"); }
        protected virtual void OnLaunched(global::Windows.ApplicationModel.Activation.LaunchActivatedEventArgs args) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.OnLaunched"); }
    }
}
namespace Windows.UI.Xaml
{
    public sealed class Window
    {
        public static Window Current { get { throw new global::System.NotImplementedException("Windows.UI.Xaml.Window.Current"); } }
    }
}
namespace Windows.ApplicationModel.Activation
{
    public sealed class LaunchActivatedEventArgs
    {
        public string Arguments { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.Activation.LaunchActivatedEventArgs.Arguments"); } }
    }
}
