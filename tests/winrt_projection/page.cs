using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Navigation;
class TestPage : Page, Windows.UI.Xaml.Markup.IComponentConnector
{
    internal int To, From, Leaving;
    internal int ConnectionId;
    internal object Connected;
    void Windows.UI.Xaml.Markup.IComponentConnector.Connect(int id, object target)
    {
        if (Failure != null) throw Failure;
        ConnectionId = id; Connected = target;
    }
    internal NavigationEventArgs ToArgs, FromArgs;
    internal Exception Failure = null;
    internal int KeyDowns;
    internal Windows.UI.Xaml.Input.KeyRoutedEventArgs KeyArgs;
    protected override void OnKeyDown(Windows.UI.Xaml.Input.KeyRoutedEventArgs args)
    {
        ++KeyDowns; KeyArgs = args;
        if (Failure != null) throw Failure;
        if (args != null) args.Handled = true;
        base.OnKeyDown(args);
    }
    protected override void OnNavigatedTo(NavigationEventArgs args) { ++To; ToArgs = args; base.OnNavigatedTo(args); }
    protected override void OnNavigatedFrom(NavigationEventArgs args) { ++From; FromArgs = args; base.OnNavigatedFrom(args); }
    internal bool Cancel;
    protected override void OnNavigatingFrom(NavigatingCancelEventArgs args) { ++Leaving; if (Failure != null) throw Failure; args.Cancel = Cancel; base.OnNavigatingFrom(args); }
}
class DerivedPage : TestPage { }
#if FULL_SURFACE
class UnsupportedFocusPage : Page
{
    protected override void OnGotFocus(RoutedEventArgs args) { }
}
#endif
class TestXamlMember : Windows.UI.Xaml.Markup.IXamlMember
{
    internal Windows.UI.Xaml.Markup.IXamlType Owner;
    public bool IsAttachable { get { return false; } }
    public bool IsDependencyProperty { get { return false; } }
    public bool IsReadOnly { get { return true; } }
    public string Name { get { return "Content"; } }
    public Windows.UI.Xaml.Markup.IXamlType TargetType { get { return Owner; } }
    public Windows.UI.Xaml.Markup.IXamlType Type { get { return null; } }
    public object GetValue(object instance) { throw new NotSupportedException(); }
    public void SetValue(object instance, object value) { throw new NotSupportedException(); }
}
class TestXamlType : Windows.UI.Xaml.Markup.IXamlType
{
    internal readonly Type Type;
    internal readonly TestXamlMember Content = new TestXamlMember();
    internal int Activations, Initialized;
    internal Exception Failure;
    internal TestXamlType(Type type) { Type = type; Content.Owner = this; }
    public Windows.UI.Xaml.Markup.IXamlType BaseType { get { return null; } }
    public Windows.UI.Xaml.Markup.IXamlMember ContentProperty { get { return Content; } }
    public string FullName { get { return Type.FullName; } }
    public bool IsArray { get { return false; } }
    public bool IsCollection { get { return false; } }
    public bool IsConstructible { get { return true; } }
    public bool IsDictionary { get { return false; } }
    public bool IsMarkupExtension { get { return false; } }
    public bool IsBindable { get { return true; } }
    public Windows.UI.Xaml.Markup.IXamlType ItemType { get { return null; } }
    public Windows.UI.Xaml.Markup.IXamlType KeyType { get { return null; } }
    public Type UnderlyingType { get { return Type; } }
    public object ActivateInstance() { ++Activations; if (Failure != null) throw Failure; return Activator.CreateInstance(Type, true); }
    public object CreateFromString(string value) { throw new NotSupportedException(); }
    public Windows.UI.Xaml.Markup.IXamlMember GetMember(string name) { return name == "Content" ? Content : null; }
    public void AddToVector(object instance, object value) { throw new NotSupportedException(); }
    public void AddToMap(object instance, object key, object value) { throw new NotSupportedException(); }
    public void RunInitializer() { ++Initialized; }
}
class PartialPage : Page
{
    internal int Leaving;
    protected override void OnNavigatingFrom(NavigatingCancelEventArgs args) { ++Leaving; base.OnNavigatingFrom(args); }
}
class PageApp : Application, Windows.UI.Xaml.Markup.IXamlMetadataProvider
{
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int InvokeWindowKeyDown(byte withArgs);
    internal readonly Dictionary<Type, TestXamlType> Types = new Dictionary<Type, TestXamlType>();
    internal readonly List<Type> Requested = new List<Type>();
    internal readonly List<string> RequestedNames = new List<string>();
    public Windows.UI.Xaml.Markup.IXamlType GetXamlType(Type type)
    {
        Requested.Add(type);
        TestXamlType value;
        return type != null && Types.TryGetValue(type, out value) ? value : null;
    }
    public Windows.UI.Xaml.Markup.IXamlType GetXamlType(string fullName)
    {
        RequestedNames.Add(fullName);
        foreach (var value in Types.Values) if (value.FullName == fullName) return value;
        return null;
    }
    public Windows.UI.Xaml.Markup.XmlnsDefinition[] GetXmlnsDefinitions() { return new Windows.UI.Xaml.Markup.XmlnsDefinition[0]; }
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ExerciseXamlMetadata();
    static void VerifyNavigation()
    {
        var app = (PageApp)Application.Current;
        var type = new TestXamlType(typeof(DerivedPage));
        app.Types[typeof(DerivedPage)] = type;
        var frame = new Frame();
        Check(frame.Navigate(typeof(DerivedPage)), "Frame.Navigate result");
        Check(app.Requested.Count == 1 && app.Requested[0] == typeof(DerivedPage) && type.Activations == 1, "Navigate resolves the managed page type once");
        var page = frame.Content as DerivedPage;
        Check(page != null && page.To == 1 && ReferenceEquals(page.Frame, frame), "navigated page is the activated managed instance");
        try { frame.Navigate(typeof(PartialPage)); throw new Exception("unknown page navigated"); }
        catch (Exception error) { if (error.Message == "unknown page navigated") throw; }
        Check(app.Requested[app.Requested.Count - 1] == typeof(PartialPage) && ReferenceEquals(frame.Content, page), "failed navigation kept the page");
        type.Failure = new InvalidOperationException("activation failure");
        try { frame.Navigate(typeof(DerivedPage)); throw new Exception("failed activation navigated"); }
        catch (InvalidOperationException) { }
        type.Failure = null;
        Check(ReferenceEquals(frame.Content, page) && page.From == 0, "failed activation kept the page");
        Check(frame.Navigate(typeof(DerivedPage), "parameter") && frame.Content is DerivedPage && !ReferenceEquals(frame.Content, page) && page.From == 1, "Navigate with a parameter");
        app.Requested.Clear();
        int hr = ExerciseXamlMetadata();
        Check(hr == 0, "native IXamlMetadataProvider/IXamlType/IXamlMember calls: 0x" + hr.ToString("x8"));
        Check(app.RequestedNames.Count == 2 && app.RequestedNames[0] == "DerivedPage" && app.RequestedNames[1] == "Missing", "native full-name lookups");
        Check(app.Requested.Count == 3 && app.Requested[0] == typeof(int) && app.Requested[1] == typeof(Page) && app.Requested[2] == null, "native TypeName kinds map to CLR types");
        Check(type.Initialized == 1, "native RunInitializer");
        Check(page.ToArgs.Parameter == null && page.ToArgs.SourcePageType == typeof(DerivedPage) && ReferenceEquals(page.ToArgs.Content, page) &&
              page.ToArgs.NavigationMode == NavigationMode.New && page.ToArgs.Uri == null, "navigation arguments");
        var next = (DerivedPage)frame.Content;
        Check(page.From == 1 && ReferenceEquals(page.FromArgs, next.ToArgs) && (string)next.ToArgs.Parameter == "parameter" && page.Leaving == 1, "leaving page sees the new navigation");
        next.Cancel = true;
        Check(!frame.Navigate(typeof(DerivedPage)) && ReferenceEquals(frame.Content, next), "OnNavigatingFrom cancellation keeps the page");
        Console.WriteLine("PASS: Frame.Navigate activates managed pages through the app's XAML metadata provider");
    }
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int SendWindowSize();
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int FlushWindowEvents();
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int SetApplicationViewOrientation(int portrait);
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int CheckApplicationViewState(int value);
    static void VerifyApplicationResources()
    {
        var app = Application.Current;
        // Explicit initialization without resource entries must suppress an unrelated
        // conventional App.xaml, just as explicit resource assignment does.
        ResourceDictionary original;
        global::System.IO.File.WriteAllText("App.xaml", "<invalid/>");
        global::System.IO.File.WriteAllText("explicit-empty-app.xaml", "<Application xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\"/>");
        try
        {
            Application.LoadComponent(app, new Uri("ms-appx:///explicit-empty-app.xaml"));
            original = app.Resources;
        }
        finally
        {
            global::System.IO.File.Delete("App.xaml");
            global::System.IO.File.Delete("explicit-empty-app.xaml");
        }
        Check(original != null && ReferenceEquals(original, app.Resources), "default application resource dictionary identity");
        var replacement = new ResourceDictionary();
        app.Resources = replacement;
        GC.Collect(); GC.WaitForPendingFinalizers();
        Check(ReferenceEquals(app.Resources, replacement), "assigned application resource dictionary identity");
        app.Resources = original;
        Check(ReferenceEquals(app.Resources, original), "restored application resource dictionary identity");
        Console.WriteLine("PASS: Application.Resources retains a default dictionary and assigned managed dictionaries");
    }
    static void Expect<T>(Action action) where T : Exception
    {
        try { action(); } catch (T) { return; }
        throw new Exception("Expected " + typeof(T).Name);
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static WeakReference AddManagedResource(IDictionary<object, object> map)
    {
        var marker = new object();
        map["managed"] = marker;
        return new WeakReference(marker);
    }
    static void VerifyResourceMap()
    {
        var dictionary = new ResourceDictionary();
        var map = (IDictionary<object, object>)(object)dictionary;
        Check(map.Count == 0 && !map.IsReadOnly, "empty resource map");
        var pairs = (ICollection<KeyValuePair<object, object>>)map;
        var keys = map.Keys;
        var values = map.Values;
        var child = new Frame();
        map.Add("Éclair\0Ω", child);
        map.Add("nullable", null);
        Check(map.Count == 2 && keys.Count == 2 && values.Count == 2, "live map views");
        Check(ReferenceEquals(map["Éclair\0Ω"], child), "resource object identity");
        object value;
        Check(map.TryGetValue("nullable", out value) && value == null, "null resource is present");
        Check(!map.TryGetValue("missing", out value) && value == null, "missing resource");
        Expect<KeyNotFoundException>(() => { var ignored = map["missing"]; });
        Expect<ArgumentException>(() => map.Add("nullable", "duplicate"));
        Expect<ArgumentNullException>(() => map.ContainsKey(null));
        Expect<ArgumentNullException>(() => map[null] = child);
        Check(!map.ContainsKey(typeof(Page)), "type keys are valid resource keys");
        Expect<NotSupportedException>(() => keys.Clear());
        Expect<NotSupportedException>(() => values.Add("no"));
        map["nullable"] = "updated Ω";
        Check((string)map["nullable"] == "updated Ω", "resource replacement and boxed string");
        Check(map.Contains(new KeyValuePair<object, object>("nullable", "updated Ω")), "pair membership");
        Check(!pairs.Remove(new KeyValuePair<object, object>("nullable", "wrong")), "mismatched pair removal");
        var copy = new KeyValuePair<object, object>[3];
        map.CopyTo(copy, 1);
        Check(copy[0].Key == null && copy[1].Key != null && copy[2].Key != null, "map CopyTo offset");
        Expect<ArgumentException>(() => map.CopyTo(new KeyValuePair<object, object>[1], 0));
        Expect<ArgumentOutOfRangeException>(() => keys.CopyTo(new object[2], -1));
        using (var snapshot = map.GetEnumerator())
        {
            Check(snapshot.MoveNext(), "resource enumerator starts");
            map.Clear();
            int count = 1;
            while (snapshot.MoveNext()) ++count;
            Check(count == 2 && map.Count == 0 && keys.Count == 0, "snapshot enumeration survives clear");
        }
        var weak = AddManagedResource(map);
        GC.Collect(); GC.WaitForPendingFinalizers();
        var marker = map["managed"];
        Check(marker != null && ReferenceEquals(marker, weak.Target), "map retains managed resource identity");
        Check(pairs.Remove(new KeyValuePair<object, object>("managed", marker)) && !map.Remove("managed"), "pair and missing removal");
        Exception failure = null;
        var worker = new global::System.Threading.Thread(() => { try { var ignored = map.Count; } catch (Exception e) { failure = e; } });
        worker.Start(); worker.Join();
        Check(failure != null && Marshal.GetHRForException(failure) == unchecked((int)0x8001010e), "resource map thread affinity");
        Check(dictionary.GetType().BaseType == typeof(DependencyObject), "resource dictionary inheritance");
        Console.WriteLine("PASS: resource dictionaries use WPF dictionaries for mutation, lookup, snapshots and identity");
    }
    static void VerifyApplicationView()
    {
        Check((int)Windows.UI.ViewManagement.ApplicationViewState.FullScreenLandscape == 0 &&
              (int)Windows.UI.ViewManagement.ApplicationViewState.Filled == 1 &&
              (int)Windows.UI.ViewManagement.ApplicationViewState.Snapped == 2 &&
              (int)Windows.UI.ViewManagement.ApplicationViewState.FullScreenPortrait == 3, "ApplicationViewState SDK values");
        Check(SetApplicationViewOrientation(0) == 0, "landscape host resize");
        var landscape = Windows.UI.ViewManagement.ApplicationView.Value;
        Check(landscape == Windows.UI.ViewManagement.ApplicationViewState.FullScreenLandscape &&
              CheckApplicationViewState((int)landscape) == 0, "managed landscape Value differs from native SDK call");
        Check(SetApplicationViewOrientation(1) == 0, "portrait host resize");
        var portrait = Windows.UI.ViewManagement.ApplicationView.Value;
        Check(portrait == Windows.UI.ViewManagement.ApplicationViewState.FullScreenPortrait &&
              CheckApplicationViewState((int)portrait) == 0, "managed portrait Value differs from native SDK call");
        Check(SetApplicationViewOrientation(0) == 0, "restore landscape host");
        Check(Windows.UI.ViewManagement.ApplicationView.Value == landscape, "ApplicationView.Value was cached");
        Console.WriteLine("PASS: ApplicationView.Value uses the native SDK enum and follows host orientation");
    }
    static void VerifyWindowSize()
    {
        var window = Window.Current;
        Check(FlushWindowEvents() == 0, "initial Window queue did not drain");
        int calls = 0;
        object sender = null;
        Windows.UI.Core.WindowSizeChangedEventArgs retained = null;
        int thread = global::System.Threading.Thread.CurrentThread.ManagedThreadId;
        WindowSizeChangedEventHandler handler = (s, e) =>
        {
            Check(global::System.Threading.Thread.CurrentThread.ManagedThreadId == thread, "Window callback thread");
            sender = s; retained = e; ++calls;
        };
        var token = window.add_SizeChanged(handler);
        handler = null;
        GC.Collect(); GC.WaitForPendingFinalizers();
        Check(SendWindowSize() == 0 && calls == 1, "Window SizeChanged native delivery and handler lifetime");
        Check(ReferenceEquals(sender, window) && retained != null, "Window sender identity and typed arguments");
        window.remove_SizeChanged(token);
        Check(SendWindowSize() == 0 && calls == 1, "removed Window handler was called");
        GC.KeepAlive(retained);
        Console.WriteLine("PASS: Window SizeChanged subscriptions preserve typed callbacks, identity, lifetime and removal");
    }
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int SetHostMinimized(int minimized);
    // LayoutAwarePage's setup: Window bounds, CoreWindow, activation and cache mode.
    static void VerifyWindowActivation(DerivedPage page)
    {
        var window = Window.Current;
        var bounds = window.Bounds;
        Check(bounds.X == 0 && bounds.Y == 0 && bounds.Width > 0 && bounds.Height > 0, "Window bounds");
        Check(window.CoreWindow != null && window.CoreWindow.Dispatcher != null, "Window CoreWindow");
        Check((int)Windows.UI.Core.CoreWindowActivationState.CodeActivated == 0 &&
              (int)Windows.UI.Core.CoreWindowActivationState.Deactivated == 1 &&
              (int)Windows.UI.Core.CoreWindowActivationState.PointerActivated == 2, "CoreWindowActivationState SDK values");
        Check((int)Windows.UI.Xaml.Navigation.NavigationCacheMode.Disabled == 0 &&
              (int)Windows.UI.Xaml.Navigation.NavigationCacheMode.Required == 1 &&
              (int)Windows.UI.Xaml.Navigation.NavigationCacheMode.Enabled == 2, "NavigationCacheMode SDK values");
        page.NavigationCacheMode = Windows.UI.Xaml.Navigation.NavigationCacheMode.Required;
        Check(page.NavigationCacheMode == Windows.UI.Xaml.Navigation.NavigationCacheMode.Required, "Page NavigationCacheMode");
        var frame = new Frame(); window.Content = frame; frame.Content = page;
        window.Activate();
        Check(FlushWindowEvents() == 0, "Window activation events");
        var states = new global::System.Collections.Generic.List<Windows.UI.Core.CoreWindowActivationState>();
        object sender = null;
        var activated = window.add_Activated((s, e) => { sender = s; states.Add(e.WindowActivationState); });
        Check(SetHostMinimized(1) == 0 && SetHostMinimized(0) == 0, "host minimize and restore");
        Check(states.Count == 2 && states[0] == Windows.UI.Core.CoreWindowActivationState.Deactivated &&
              states[1] == Windows.UI.Core.CoreWindowActivationState.CodeActivated && ReferenceEquals(sender, window),
              "Window Activated states and sender");
        window.remove_Activated(activated);
        Check(SetHostMinimized(1) == 0 && SetHostMinimized(0) == 0 && states.Count == 2, "removed Activated handler was called");
        Console.WriteLine("PASS: Window bounds, activation and Page cache mode reach managed handlers");
    }
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ConnectWindowContent(int id);
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ConnectFrameContent(int id);
    // The game's share setup: the view's manager accepts DataRequested handlers,
    // which no Share charm outside Windows 8 ever raises.
    void VerifySharing()
    {
        var manager = global::Windows.ApplicationModel.DataTransfer.DataTransferManager.GetForCurrentView();
        Check(ReferenceEquals(manager, global::Windows.ApplicationModel.DataTransfer.DataTransferManager.GetForCurrentView()), "DataTransferManager identity");
        bool raised = false;
        var first = manager.add_DataRequested((sender, args) => raised = true);
        var second = manager.add_DataRequested((sender, args) => raised = true);
        Check(!first.Equals(second), "DataRequested tokens are distinct");
        manager.remove_DataRequested(first);
        manager.remove_DataRequested(second);
        Check(!raised, "DataRequested is not raised without a share request");
    }
    // The game's splash continuation: RunAsync queues work for the UI thread and
    // completes the returned action once the handler has run.
    void VerifyDispatcher()
    {
        global::Windows.UI.Core.CoreDispatcher dispatcher = global::Windows.UI.Core.CoreWindow.GetForCurrentThread().Dispatcher;
        int thread = global::System.Threading.Thread.CurrentThread.ManagedThreadId;
        bool ran = false;
        global::Windows.Foundation.IAsyncAction action = dispatcher.RunAsync(global::Windows.UI.Core.CoreDispatcherPriority.Normal, () =>
        {
            Check(global::System.Threading.Thread.CurrentThread.ManagedThreadId == thread, "RunAsync handler thread");
            ran = true;
        });
        Check(!ran && action.Status == global::Windows.Foundation.AsyncStatus.Started, "RunAsync queues the handler");
        action.Completed = (info, status) =>
        {
            try
            {
                Check(ReferenceEquals(info, action) && ran && status == global::Windows.Foundation.AsyncStatus.Completed, "RunAsync completes after the handler");
                VerifyTimer();
            }
            catch (Exception error) { Console.Error.WriteLine(error); Environment.Exit(1); }
        };
    }
    // The game's tip rotation: ticks arrive from the host's own message loop
    // after OnLaunched returns, so this test exits only when they do.
    void VerifyTimer()
    {
        var timer = new DispatcherTimer();
        Check(timer.Interval == TimeSpan.Zero, "default DispatcherTimer interval");
        var large = TimeSpan.FromDays(3) + TimeSpan.FromTicks(7);
        timer.Interval = large;
        Check(timer.Interval == large, "64-bit TimeSpan interval round trip");
        try { timer.Interval = TimeSpan.FromTicks(-1); throw new Exception("negative interval accepted"); }
        catch (ArgumentException) { }
        Check(timer.Interval == large, "rejected interval preserves value");
        timer.Interval = TimeSpan.FromMilliseconds(20);
        int thread = global::System.Threading.Thread.CurrentThread.ManagedThreadId;
        int early = 0, late = 0;
        var earlyToken = default(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken);
        Action<object, object> observe = (sender, args) =>
        {
            Check(ReferenceEquals(sender, timer) && args == null, "Tick sender identity and null arguments");
            Check(global::System.Threading.Thread.CurrentThread.ManagedThreadId == thread, "Tick thread");
        };
        Func<Action, EventHandler<object>> guarded = body => (sender, args) =>
        {
            try { observe(sender, args); body(); }
            catch (Exception error) { Console.Error.WriteLine(error); Environment.Exit(1); }
        };
        earlyToken = timer.add_Tick(guarded(() => { if (++early == 2) timer.remove_Tick(earlyToken); }));
        // A handler failure has no managed caller to rethrow it; the shim log must name it.
        bool failed = false;
        timer.add_Tick((sender, args) => { if (!failed) { failed = true; throw new InvalidOperationException("tick handler failure"); } });
        timer.add_Tick(guarded(() =>
        {
            if (++late < 4) return;
            Check(early == 2, "removed Tick handler was called");
            timer.Stop();
            Console.WriteLine("PASS: DispatcherTimer ticks managed handlers on the UI thread with TimeSpan intervals and removal");
            CheckWpfDispatcher();
            Exit();
        }));
        GC.Collect(); GC.WaitForPendingFinalizers();
        timer.Start();
    }
    // The game's splash shape: a Frame already in the Window gets a managed Page as Content.
    static void VerifyVisibility(DerivedPage page)
    {
        Check((int)Visibility.Visible == 0 && (int)Visibility.Collapsed == 1, "Visibility SDK values");
        var frame = new Frame(); Window.Current.Content = frame; frame.Content = page;
        UIElement inherited = page;
        var pagePeer = (global::System.Windows.UIElement)PeerOf(page);
        Check(inherited.Visibility == Visibility.Visible, "default Page visibility");
        inherited.Visibility = Visibility.Collapsed;
        Check(pagePeer.Visibility == global::System.Windows.Visibility.Collapsed && frame.Visibility == Visibility.Visible, "Page visibility reaches its peer; Frame keeps its own");
        frame.Visibility = Visibility.Collapsed;
        Check(((global::System.Windows.UIElement)PeerOf(frame)).Visibility == global::System.Windows.Visibility.Collapsed, "Frame visibility reaches its peer");
        pagePeer.Visibility = global::System.Windows.Visibility.Visible;
        Check(inherited.Visibility == Visibility.Visible, "peer visibility reads back");
        try { inherited.Visibility = (Visibility)(-1); throw new Exception("invalid enum accepted"); }
        catch (ArgumentException error) { Check(error.HResult == unchecked((int)0x80070057), "invalid enum HRESULT"); }
        Check(inherited.Visibility == Visibility.Visible, "invalid setter changed visibility");
        frame.Visibility = Visibility.Visible;
        Console.WriteLine("PASS: inherited Visibility properties reach WPF peers and reject invalid values");
    }
    static void VerifyFrameContent(DerivedPage page)
    {
        var frame = new Frame();
        Check(frame.Content == null, "empty Frame content");
        Window.Current.Content = frame;
        frame.Content = page;
        Check(ReferenceEquals(frame.Content, page), "Frame preserves derived Page content identity");
        Check(page.Frame == null, "direct Frame content does not navigate");
        frame.Content = "frame text";
        Check((string)frame.Content == "frame text", "Frame boxed content");
        frame.Content = null;
        Check(frame.Content == null, "Frame content clear");
        Console.WriteLine("PASS: Frame content preserves managed Page identity, boxed values and clearing");
    }
    // Key input arrives as WPF routed events; Control overrides see UWP arguments.
    static void RaiseKey(UIElement target, global::System.Windows.Input.Key key, global::System.Windows.PresentationSource source)
    {
        var args = new global::System.Windows.Input.KeyEventArgs(global::System.Windows.Input.Keyboard.PrimaryDevice, source, 0, key);
        args.RoutedEvent = global::System.Windows.Input.Keyboard.KeyDownEvent;
        ((global::System.Windows.UIElement)PeerOf(target)).RaiseEvent(args);
    }
    static bool ShimLogContains(string text)
    {
        if (!global::System.IO.File.Exists("appx-portable.log")) return false;
        using (var stream = new global::System.IO.FileStream("appx-portable.log", global::System.IO.FileMode.Open, global::System.IO.FileAccess.Read, global::System.IO.FileShare.ReadWrite | global::System.IO.FileShare.Delete))
        using (var reader = new global::System.IO.StreamReader(stream)) return reader.ReadToEnd().Contains(text);
    }
    static void VerifyKeys(DerivedPage page)
    {
        var source = new global::System.Windows.Interop.HwndSource(new global::System.Windows.Interop.HwndSourceParameters("keys")) { RootVisual = (global::System.Windows.Media.Visual)PeerOf(page) };
        RaiseKey(page, global::System.Windows.Input.Key.Escape, source);
        GC.Collect(); GC.WaitForPendingFinalizers();
        Check(page.KeyDowns == 1 && page.KeyArgs != null && page.KeyArgs.Key == Windows.System.VirtualKey.Escape && page.KeyArgs.Handled, "key override sees UWP arguments and handling");
        page.Failure = new InvalidOperationException("key override failure");
        try { RaiseKey(page, global::System.Windows.Input.Key.Escape, source); throw new Exception("key override error accepted"); }
        catch (InvalidOperationException error) { Check(ReferenceEquals(error, page.Failure), "key override exception identity"); }
        page.Failure = null;
        Check(ShimLogContains("managed callback threw System.InvalidOperationException: key override failure"), "key override failure logged");
        var partial = new PartialPage();
        source.RootVisual = (global::System.Windows.Media.Visual)PeerOf(partial);
        RaiseKey(partial, global::System.Windows.Input.Key.Escape, source);
        source.RootVisual = null; source.Dispose();
        Console.WriteLine("PASS: Control key overrides receive WPF key input with arguments, handling and failures");
    }
    static void VerifyAnimation()
    {
        var target = new Windows.UI.Xaml.Controls.Grid();
        var fade = new Windows.UI.Xaml.Media.Animation.FadeOutThemeAnimation();
        var fadePeer = (global::System.Windows.Media.Animation.DoubleAnimation)PeerOf(fade);
        Check(fadePeer.To == 0 && global::System.Windows.Media.Animation.Storyboard.GetTargetProperty(fadePeer).PathParameters[0] == global::System.Windows.UIElement.OpacityProperty, "fade-out animates Opacity to 0");
        fade.TargetName = "target";
        Check(global::System.Windows.Media.Animation.Storyboard.GetTargetName(fadePeer) == "target" && fade.TargetName == "target", "TargetName maps to Storyboard.TargetName");
        global::System.Windows.Media.Animation.Storyboard.SetTarget(fadePeer, (global::System.Windows.DependencyObject)PeerOf(target));
        var storyboard = new Windows.UI.Xaml.Media.Animation.Storyboard();
        ((global::System.Windows.Media.Animation.Storyboard)PeerOf(storyboard)).Children.Add(fadePeer);
        storyboard.Begin();
        Check(((global::System.Windows.UIElement)PeerOf(target)).HasAnimatedProperties, "Storyboard.Begin applies its animations");
        // Clock state follows WPF's timing ticks, which run once the shim loop pumps.
        var clock = (global::System.Windows.Media.Animation.Storyboard)PeerOf(storyboard);
        deferred.Add(() =>
        {
            Check(clock.GetCurrentState() != global::System.Windows.Media.Animation.ClockState.Stopped, "Storyboard.Begin runs its clock");
            storyboard.Stop();   // takes effect on WPF's next tick
            Console.WriteLine("PASS: storyboards and theme fades run as WPF animations");
        });
    }
    // WPF backs Windows.UI.Xaml under Wine: each wrapper owns a WPF peer.
    static object PeerOf(object wrapper)
    {
        var field = typeof(DependencyObject).GetField("peer", global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.NonPublic);
        Check(field != null, "wrapper has a WPF peer");
        return field.GetValue(wrapper);
    }
    class VerifyOwner { }
    static void VerifyWpfElements()
    {
        var text = new Windows.UI.Xaml.Controls.TextBlock(); text.Text = "Ω"; text.Visibility = Visibility.Collapsed;
        var peer = (global::System.Windows.Controls.TextBlock)PeerOf(text);
        Check(peer.Text == "Ω" && peer.Visibility == global::System.Windows.Visibility.Collapsed, "mapped properties reach the WPF peer");
        peer.Text = "back"; Check(text.Text == "back", "WPF changes read back through the wrapper");
        Check(ReferenceEquals(Windows.UI.Xaml.Controls.TextBlock.TextProperty, Windows.UI.Xaml.Controls.TextBlock.TextProperty), "DependencyProperty identity");
        Check((string)text.GetValue(Windows.UI.Xaml.Controls.TextBlock.TextProperty) == "back", "GetValue through a mapped property");
        Windows.UI.Xaml.Controls.Grid.SetRow(text, 2);
        Check(Windows.UI.Xaml.Controls.Grid.GetRow(text) == 2 && global::System.Windows.Controls.Grid.GetRow(peer) == 2, "attached accessors");
        int changes = 0; object seenOld = null, seenNew = null;
        DependencyProperty custom = null;
        custom = DependencyProperty.RegisterAttached("Score", typeof(int), typeof(VerifyOwner), new PropertyMetadata(7, (d, e) =>
        {
            ++changes; seenOld = e.OldValue; seenNew = e.NewValue;
            Check(ReferenceEquals(d, text) && ReferenceEquals(e.Property, custom), "callback sender and property are the wrappers");
        }));
        Check((int)text.GetValue(custom) == 7, "game property default");
        text.SetValue(custom, 9); Check(changes == 1 && (int)seenOld == 7 && (int)seenNew == 9, "game property change callback");
        Check(text.ReadLocalValue(custom).Equals(9) && ReferenceEquals(new Windows.UI.Xaml.Controls.TextBlock().ReadLocalValue(custom), DependencyProperty.UnsetValue), "ReadLocalValue");
        text.ClearValue(custom); Check((int)text.GetValue(custom) == 7 && changes == 2, "ClearValue restores the default");
        var grid = new Windows.UI.Xaml.Controls.Grid();
        Check(PeerOf(grid) is global::System.Windows.Controls.Grid && grid.Visibility == Visibility.Visible, "Grid wraps a WPF Grid");
        Exception wrong = null;
        var thread = new global::System.Threading.Thread(() => { try { var x = text.Text; } catch (Exception e) { wrong = e; } });
        thread.Start(); thread.Join();
        Check(wrong != null && Marshal.GetHRForException(wrong) == unchecked((int)0x8001010e), "wrapper thread affinity");
        Console.WriteLine("PASS: WPF-backed elements map properties, dependency properties and thread affinity");
    }
    static void VerifyWpfTree()
    {
        var root = new Windows.UI.Xaml.Controls.Grid(); var child = new Windows.UI.Xaml.Controls.TextBlock();
        root.Children.Add(child);
        Check(root.Children.Count == 1 && ReferenceEquals(root.Children[0], child) && root.Children.IndexOf(child) == 0, "Children identity");
        Check(ReferenceEquals(child.Parent, root), "Parent is the wrapper");
        var rootPeer = (global::System.Windows.FrameworkElement)PeerOf(root);
        rootPeer.Measure(new global::System.Windows.Size(800, 600)); rootPeer.Arrange(new global::System.Windows.Rect(0, 0, 800, 600));
        Check(Windows.UI.Xaml.Media.VisualTreeHelper.GetChildrenCount(root) == 1 && ReferenceEquals(Windows.UI.Xaml.Media.VisualTreeHelper.GetChild(root, 0), child) &&
              ReferenceEquals(Windows.UI.Xaml.Media.VisualTreeHelper.GetParent(child), root), "visual tree uses the same wrappers");
        var toggle = new global::System.Windows.Controls.Primitives.ToggleButton();   // a WPF type with no registered wrapper
        ((global::System.Windows.Controls.Grid)rootPeer).Children.Add(toggle);
        var fallback = root.Children[1];
        Check(fallback is Windows.UI.Xaml.Controls.ContentControl && ReferenceEquals(fallback, root.Children[1]), "unregistered peer wraps as nearest registered base, stably");
        root.Children.RemoveAt(1); Check(root.Children.Count == 1 && toggle.Parent == null, "Children removal");
        global::System.Windows.NameScope.SetNameScope(rootPeer, new global::System.Windows.NameScope());
        rootPeer.RegisterName("label", PeerOf(child));
        Check(ReferenceEquals(root.FindName("label"), child) && root.FindName("missing") == null, "FindName returns wrappers");
        child.Margin = new Thickness(1, 2, 3, 4); child.HorizontalAlignment = HorizontalAlignment.Center; child.Width = 120; child.Opacity = 0.5;
        var childPeer = (global::System.Windows.FrameworkElement)PeerOf(child);
        Check(childPeer.Margin == new global::System.Windows.Thickness(1, 2, 3, 4) && child.Margin == new Thickness(1, 2, 3, 4), "Margin converts Thickness both ways");
        Check(childPeer.HorizontalAlignment == global::System.Windows.HorizontalAlignment.Center && child.Width == 120 && childPeer.Opacity == 0.5, "layout properties reach the peer");
        Check(double.IsNaN(root.Height), "unset Height is NaN");
        var model = new object(); root.DataContext = model; root.Tag = "tag";
        Check(ReferenceEquals(child.DataContext, model) && (string)root.Tag == "tag", "DataContext inherits through WPF");
        root.Resources["k"] = child;
        Check(ReferenceEquals(root.Resources["k"], child) && rootPeer.Resources["k"] == PeerOf(child), "resources store peers, return wrappers");
        var style = new Style(); root.Style = style;
        Check(ReferenceEquals(root.Style, style) && rootPeer.Style == PeerOf(style), "Style identity");
        var content = new Windows.UI.Xaml.Controls.ContentControl(); content.Content = child.Parent == null ? (object)child : "text";
        Check((string)content.Content == "text", "ContentControl boxed content");
        var user = new Windows.UI.Xaml.Controls.UserControl(); var inner = new Windows.UI.Xaml.Controls.Grid(); user.Content = inner;
        Check(ReferenceEquals(user.Content, inner) && ((global::System.Windows.Controls.UserControl)PeerOf(user)).Content == PeerOf(inner), "UserControl content identity");
        var app = Application.Current; var dictionary = app.Resources;
        Check(dictionary != null && ReferenceEquals(dictionary, app.Resources), "default Application.Resources identity");
        var replacement = new ResourceDictionary(); app.Resources = replacement;
        Check(ReferenceEquals(app.Resources, replacement), "assigned Application.Resources identity");
        app.Resources = dictionary;
        int loaded = 0, removed = 0; object sender = null; RoutedEventArgs args = null;
        var skipped = child.add_Loaded((s, e) => ++removed); child.remove_Loaded(skipped);
        child.add_Loaded((s, e) => { ++loaded; sender = s; args = e; });
        var window = Window.Current; var previous = window.Content;
        window.Content = root;
        Check(ReferenceEquals(window.Content, root) && ReferenceEquals(HostRoot(), PeerOf(root)), "Window.Content identity and compositor root");
        // A test-only presentation source raises Loaded; the compositor owns the real one (Plan 2).
        var source = new global::System.Windows.Interop.HwndSource(new global::System.Windows.Interop.HwndSourceParameters("probe")) { RootVisual = rootPeer };
        deferred.Add(() =>
        {
            Check(loaded == 1 && removed == 0 && ReferenceEquals(sender, child) && args != null && ReferenceEquals(args.OriginalSource, child), "Loaded reaches managed handlers with wrappers");
            source.RootVisual = null; source.Dispose();
            Console.WriteLine("PASS: WPF-backed tree keeps wrapper identity for children, names, resources and Loaded");
        });
        window.Content = previous;
    }
    static object HostRoot()
    {
        var host = typeof(DependencyObject).Assembly.GetType("AppxPortable.WinRT.Xaml.Host");
        return host.GetProperty("Root", global::System.Reflection.BindingFlags.Static | global::System.Reflection.BindingFlags.NonPublic).GetValue(null, null);
    }
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    [MethodImpl(MethodImplOptions.NoInlining)]
    void Verify(DerivedPage page)
    {
        VerifyWpfElements();
        VerifyWpfTree();
#if FULL_SURFACE
        new UnsupportedFocusPage();
        Check(ShimLogContains("UNIMPL managed Windows.UI.Xaml.Controls.Page.OnGotFocus override"), "unimplemented override is named");
        Console.WriteLine("PASS: unimplemented inherited overrides are named at construction");
#endif
        Check(page.Frame == null, "unhosted Page.Frame");
        var content = new Frame(); page.Content = content;
        Check(ReferenceEquals(page.Content, content), "UserControl content identity on Page");
        page.Content = null; Check(page.Content == null, "Page content clear");
        var property = DependencyProperty.RegisterAttached("PageState", typeof(string), typeof(DerivedPage), null);
        page.SetValue(property, "composed page"); Check((string)page.GetValue(property) == "composed page", "Page dependency store");
        // Projection-owned interfaces use TypeKind.Metadata, including the
        // IObservableMap<string, object> property in LayoutAwarePage.
        var providerProperty = DependencyProperty.Register("Provider", typeof(Windows.UI.Xaml.Markup.IXamlMetadataProvider), typeof(DerivedPage), null);
        Check(page.GetValue(providerProperty) == null, "metadata interface default");
        page.SetValue(providerProperty, this);
        Check(ReferenceEquals(page.GetValue(providerProperty), this), "metadata interface property identity");
        try { page.SetValue(providerProperty, new object()); throw new Exception("wrong metadata interface accepted"); }
        catch (ArgumentException) { }
        Check(ReferenceEquals(page.GetValue(providerProperty), this), "rejected metadata value preserves local value");
        page.ClearValue(providerProperty);
        Check(page.GetValue(providerProperty) == null, "metadata interface clear");
        var observable = typeof(Application).Assembly.GetType("Windows.Foundation.Collections.IObservableMap`2", false);
        if (observable != null)
        {
            var mapType = observable.MakeGenericType(typeof(string), typeof(object));
            var mapProperty = DependencyProperty.Register("ObservableState", mapType, typeof(DerivedPage), null);
            Check(page.GetValue(mapProperty) == null, "closed metadata generic default");
            try { page.SetValue(mapProperty, new object()); throw new Exception("wrong metadata generic accepted"); }
            catch (ArgumentException) { }
        }
        VerifyKeys(page);
        Window.Current.Content = page;
        Check(ReferenceEquals(Window.Current.Content, page), "Window preserves derived Page identity");
        VerifyVisibility(page);
        VerifyFrameContent(page);
        VerifyWindowSize();
#if NATIVE_XAML
        VerifyApplicationResources();
#endif
        VerifyResourceMap();
        VerifyApplicationView();
        VerifyNavigation();
        VerifyWindowActivation(page);
        VerifyAnimation();
        Console.WriteLine("PASS: managed Page composes WPF-backed inheritance, content and window identity");
        VerifySharing();
        VerifyDispatcher();
    }
    // WPF backs XAML under Wine: its dispatcher lives on the shim's view thread and
    // the shim's loop pumps it; the queued work is checked once ticks have run.
    static int wpfThread, wpfRan;
    static void VerifyWpfDispatcher()
    {
        wpfThread = global::System.Threading.Thread.CurrentThread.ManagedThreadId;
        var grid = new global::System.Windows.Controls.Grid();
        Check(grid.Dispatcher == global::System.Windows.Threading.Dispatcher.CurrentDispatcher, "WPF object on the view thread");
        grid.Dispatcher.BeginInvoke(new Action(() => { wpfRan = global::System.Threading.Thread.CurrentThread.ManagedThreadId; }));
    }
    static readonly List<Action> deferred = new List<Action>();
    static void CheckWpfDispatcher()
    {
        foreach (var check in deferred) check();
        Check(wpfRan == wpfThread, "WPF dispatcher work runs on the shim loop");
        Console.WriteLine("PASS: WPF dispatcher runs on the XAML view thread's shim loop");
    }
    protected override void OnLaunched(Windows.ApplicationModel.Activation.LaunchActivatedEventArgs args) { VerifyWpfDispatcher(); Verify(new DerivedPage()); }
}
class PageProjectionTest
{
    [STAThread]
    static int Main()
    {
        try { Application.Start(p => new PageApp()); return 1; }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
