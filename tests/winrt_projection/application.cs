using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Threading;
using System.Threading.Tasks;

using Windows.ApplicationModel.Activation;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
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
        // As games start: an empty window gets a composed Frame as its content.
        Window window = Window.Current;
        Check(window != null && window.Content == null, "empty Window.Current");
        var frame = new Frame();
        var property = DependencyProperty.RegisterAttached("Session", typeof(string), typeof(LaunchApp), null);
        Check(frame.GetValue(property) == null, "attached property default");
        Check(ReferenceEquals(frame.ReadLocalValue(property), DependencyProperty.UnsetValue), "missing local sentinel");
        const string text = "session-đá-💎\0tail";
        frame.SetValue(property, text);
        Check((string)frame.GetValue(property) == text, "boxed string property round trip");
        frame.SetValue(property, null);
        Check(frame.ReadLocalValue(property) == null, "explicit null local value");
        frame.ClearValue(property);
        Check(ReferenceEquals(frame.ReadLocalValue(property), DependencyProperty.UnsetValue), "clear removes local value");
        var count = DependencyProperty.Register("Count", typeof(int), typeof(LaunchApp), null);
        Check((int)frame.GetValue(count) == 0, "Int32 default");
        frame.SetValue(count, 42);
        Check((int)frame.GetValue(count) == 42, "Int32 local value");
        try { frame.SetValue(count, "wrong type"); throw new Exception("wrong property type accepted"); }
        catch (ArgumentException) { }
        frame.ClearValue(count);
        Check((int)frame.GetValue(count) == 0, "Int32 default after clear");
        var data = new global::System.Collections.Generic.Dictionary<string, object>();
        var state = DependencyProperty.RegisterAttached("State", data.GetType(), typeof(LaunchApp), null);
        frame.SetValue(state, data);
        Check(ReferenceEquals(frame.GetValue(state), data), "managed reference property retains identity");
        var derived = new StateDictionary();
        frame.SetValue(state, derived);
        Check(ReferenceEquals(frame.GetValue(state), derived), "managed reference property accepts derived values");
        try { frame.SetValue(state, new object()); throw new Exception("wrong reference type accepted"); }
        catch (ArgumentException) { }
        int collecting = 1;
        var collector = new Thread(() => { while (Interlocked.CompareExchange(ref collecting, 1, 1) == 1) { GC.Collect(); Thread.Sleep(1); } });
        collector.Start();
        try
        {
            for (int index = 0; index < 128; ++index)
            {
                frame.SetValue(state, new StateDictionary { { "iteration", index } });
                var restored = (StateDictionary)frame.GetValue(state);
                Check((int)restored["iteration"] == index, "temporary custom object survives native ownership and GC");
            }
        }
        finally { Interlocked.Exchange(ref collecting, 0); collector.Join(); }
        frame.ClearValue(state);
        Check(frame.GetValue(state) == null, "managed reference property clears");
        Console.WriteLine("PASS: managed dependency properties use the shim value store");
        window.Content = frame;
        Check(ReferenceEquals(window.Content, frame), "Window.Content returns the managed Frame");
        Check(ReferenceEquals(Window.Current, window), "one wrapper per native Window");
        window.Activate();
        Check(SynchronizationContext.Current != null, "the view thread has a synchronization context");
        Continue(Thread.CurrentThread.ManagedThreadId);
    }

    // An await completed on the thread pool resumes on the view thread, as XAML code expects.
    async void Continue(int view)
    {
        await Task.Run(() => Thread.Sleep(10));
        if (Thread.CurrentThread.ManagedThreadId != view)
        {
            Console.WriteLine("FAIL: continuation left the view thread");
            Environment.Exit(1);
        }
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

class StateDictionary : global::System.Collections.Generic.Dictionary<string, object> { }
