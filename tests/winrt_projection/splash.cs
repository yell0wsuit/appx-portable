using System;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Threading;
using System.Threading.Tasks;

using Windows.ApplicationModel.Activation;
using Windows.Foundation;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
class SplashApp : Application
{
    SplashScreen splash;
    int view, calls, removedCalls;
    bool launched;
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        view = Thread.CurrentThread.ManagedThreadId;
        splash = ((IActivatedEventArgs)args).SplashScreen;
        Check(splash != null && ReferenceEquals(splash, ((IActivatedEventArgs)args).SplashScreen), "launch splash identity");
        TypedEventHandler<SplashScreen, object> removed = (sender, data) => ++removedCalls;
        WindowsRuntimeMarshal.AddEventHandler<TypedEventHandler<SplashScreen, object>>(splash.add_Dismissed, splash.remove_Dismissed, removed);
        WindowsRuntimeMarshal.RemoveEventHandler<TypedEventHandler<SplashScreen, object>>(splash.remove_Dismissed, removed);
        WindowsRuntimeMarshal.AddEventHandler<TypedEventHandler<SplashScreen, object>>(splash.add_Dismissed, splash.remove_Dismissed, Dismissed);
        Window.Current.Content = new Frame();
        Window.Current.Activate();
        Check(calls == 0 && removedCalls == 0, "dismissal fired inside activation");
        launched = true;
    }
    void Dismissed(SplashScreen sender, object data)
    {
        Check(launched && ReferenceEquals(sender, splash) && data == null, "dismissal sender, arguments and ordering");
        Check(Thread.CurrentThread.ManagedThreadId == view && SynchronizationContext.Current != null, "dismissal on managed view thread");
        ++calls;
        Window.Current.Activate();
        Finish();
    }
    async void Finish()
    {
        await Task.Delay(20);
        Check(calls == 1 && removedCalls == 0, "dismissal count and removed handler");
        Check(Thread.CurrentThread.ManagedThreadId == view, "dismissal continuation left view thread");
        Console.WriteLine("PASS: managed splash identity and typed dismissal follow native activation");
        Exit();
    }
}
class SplashProjectionTest
{
    [STAThread]
    static int Main()
    {
        try { Application.Start(p => new SplashApp()); return 1; }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
