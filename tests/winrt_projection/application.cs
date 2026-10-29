using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Threading;

using Windows.ApplicationModel.Activation;
using Windows.UI.Xaml;
// A managed Application composed over the shim's, started the way games start:
// Application.Start(p => new App()) with OnLaunched overridden. Exit ends the
// process after suspension, as a Store app's does, so Start returns only when
// OnLaunched fails.
class LaunchApp : Application
{
    internal static string mode = "";
    internal static int launches, mainThread, activations;

    // Subscribed the way compiled games do; Exit suspends before ending the process.
    internal LaunchApp()
    {
        SuspendingEventHandler unused = (sender, e) => Console.WriteLine("UNUSED HANDLER");
        WindowsRuntimeMarshal.AddEventHandler<SuspendingEventHandler>(add_Suspending, remove_Suspending, unused);
        WindowsRuntimeMarshal.RemoveEventHandler<SuspendingEventHandler>(remove_Suspending, unused);
        WindowsRuntimeMarshal.AddEventHandler<SuspendingEventHandler>(add_Suspending, remove_Suspending, OnSuspending);
    }

    void OnSuspending(object sender, Windows.ApplicationModel.SuspendingEventArgs e)
    {
        Check(ReferenceEquals(sender, this) && e != null, "Suspending sender and arguments");
        Console.WriteLine("PASS: Suspending raised on the managed App");
    }

    // Overridden as games do; construction must accept it.
    protected override void OnActivated(IActivatedEventArgs args) { ++activations; }

    static void Check(bool value, string name)
    {
        if (!value) throw new InvalidOperationException(name);
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        ++launches;
        if (mode == "throw") throw new InvalidOperationException("launch marker");
        Check(Thread.CurrentThread.ManagedThreadId != mainThread, "OnLaunched on the view thread");
        Check(ReferenceEquals(Current, this), "Application.Current is the managed App");
        Check(args != null && args.Arguments == "", "launch arguments");
        // The shim launches from NotRunning; Kind comes through the IActivatedEventArgs view.
        Check(args.PreviousExecutionState == ApplicationExecutionState.NotRunning, "class member from a required interface");
        IActivatedEventArgs activated = args;
        Check(activated.Kind == ActivationKind.Launch && activated.PreviousExecutionState == ApplicationExecutionState.NotRunning,
            "interface view of the launch arguments");
        Check(activations == 0, "OnActivated not called for a launch");
        Console.WriteLine("PASS: managed App.OnLaunched ran on the shim view thread");
        Exit();
    }
}

static class Calls
{
    // Capturing, so the compiler does not cache the delegate in a static field.
    [MethodImpl(MethodImplOptions.NoInlining)]
    internal static void Start(WeakReference[] callback)
    {
        int created = 0;
        ApplicationInitializationCallback start = p => { ++created; new LaunchApp(); };
        callback[0] = new WeakReference(start);
        try { Application.Start(start); }
        finally { if (created != 1) Console.Error.WriteLine("callback ran " + created + " times"); }
    }
}

class ApplicationProjectionTest
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void Start(WeakReference[] callback) { Calls.Start(callback); }

    [STAThread]
    static int Main(string[] args)
    {
        LaunchApp.mode = args.Length > 0 ? args[0] : "";
        LaunchApp.mainThread = Thread.CurrentThread.ManagedThreadId;
        var callback = new WeakReference[1];
        try
        {
            try { Start(callback); }
            catch (InvalidOperationException error)
            {
                if (LaunchApp.mode != "throw" || error.Message != "launch marker" || LaunchApp.launches != 1) throw;
                GC.Collect();
                GC.WaitForPendingFinalizers();
                GC.Collect();
                if (callback[0].IsAlive) throw new InvalidOperationException("callback retained after Start");
                Console.WriteLine("PASS: OnLaunched exception crossed the native boundary; callback released");
                return 0;
            }
            Console.Error.WriteLine("Start returned in mode '" + LaunchApp.mode + "'");
            return 1;
        }
        catch (Exception error) { Console.Error.WriteLine("0x" + error.HResult.ToString("X8") + " " + error); return 1; }
    }
}
