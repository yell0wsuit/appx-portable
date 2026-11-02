using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Navigation;
#if NATIVE_ABI
using AppxPortable.WinRT;
#endif
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
    protected override void OnNavigatingFrom(NavigatingCancelEventArgs args) { ++Leaving; if (Failure != null) throw Failure; base.OnNavigatingFrom(args); }
}
class DerivedPage : TestPage { }
#if FULL_SURFACE
class UnsupportedKeyPage : Page
{
    protected override void OnKeyUp(Windows.UI.Xaml.Input.KeyRoutedEventArgs args) { }
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
        Check(ReferenceEquals(map["Éclair\0Ω"], child), "resource native object identity");
        object value;
        Check(map.TryGetValue("nullable", out value) && value == null, "null resource is present");
        Check(!map.TryGetValue("missing", out value) && value == null, "missing resource");
        Expect<KeyNotFoundException>(() => { var ignored = map["missing"]; });
        Expect<ArgumentException>(() => map.Add("nullable", "duplicate"));
        Expect<ArgumentNullException>(() => map.ContainsKey(null));
        Expect<ArgumentNullException>(() => map[null] = child);
        Expect<NotImplementedException>(() => map.ContainsKey(7));
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
            Check(count == 2 && map.Count == 0 && keys.Count == 0, "native snapshot enumeration survives clear");
        }
        var weak = AddManagedResource(map);
        GC.Collect(); GC.WaitForPendingFinalizers();
        var marker = map["managed"];
        Check(marker != null && ReferenceEquals(marker, weak.Target), "native map retains managed resource identity");
        Check(pairs.Remove(new KeyValuePair<object, object>("managed", marker)) && !map.Remove("managed"), "pair and missing removal");
        Exception failure = null;
        var worker = new global::System.Threading.Thread(() => { try { var ignored = map.Count; } catch (Exception e) { failure = e; } });
        worker.Start(); worker.Join();
        Check(failure != null && Marshal.GetHRForException(failure) == unchecked((int)0x8001010e), "resource map thread affinity");
        Check(dictionary.GetType().BaseType == typeof(DependencyObject), "resource dictionary inheritance");
        Console.WriteLine("PASS: managed resource dictionaries use native maps for mutation, lookup, snapshots and identity");
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
#if NATIVE_ABI
        GC.Collect(); GC.WaitForPendingFinalizers();
        using (var args = NativeObject.Argument(retained, new Guid("5a200ec7-0426-47dc-b86c-6f475915e451")))
            Check(ValidateWindowSizeArgs(args.DangerousGetHandle()) == 0, "retained Window arguments lost native size data");
#endif
        GC.KeepAlive(retained);
        Console.WriteLine("PASS: Window SizeChanged subscriptions preserve typed callbacks, identity, lifetime and removal");
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
            Exit();
        }));
        GC.Collect(); GC.WaitForPendingFinalizers();
        timer.Start();
    }
    // The game's splash shape: a Frame already in the Window gets a managed Page as Content.
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int CheckElementVisibility(int child, int value);
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int SetElementVisibility(int child, int value);
    static void VerifyVisibility(DerivedPage page)
    {
        Check((int)Visibility.Visible == 0 && (int)Visibility.Collapsed == 1, "Visibility SDK values");
        var frame = new Frame(); Window.Current.Content = frame; frame.Content = page;
        UIElement inherited = page;
        Check(inherited.Visibility == Visibility.Visible, "default Page visibility");
        inherited.Visibility = Visibility.Collapsed;
        Check(CheckElementVisibility(1, 1) == 0 && frame.Visibility == Visibility.Visible, "native Page visibility or independent Frame state");
        frame.Visibility = Visibility.Collapsed;
        Check(CheckElementVisibility(0, 1) == 0, "native Frame visibility");
        Check(SetElementVisibility(1, 0) == 0 && inherited.Visibility == Visibility.Visible, "native visibility update was cached");
        try { inherited.Visibility = (Visibility)(-1); throw new Exception("invalid enum accepted"); }
        catch (ArgumentException error) { Check(error.HResult == unchecked((int)0x80070057), "native enum setter HRESULT"); }
        Check(inherited.Visibility == Visibility.Visible && CheckElementVisibility(1, 0) == 0, "invalid setter changed visibility");
        frame.Visibility = Visibility.Visible;
        Console.WriteLine("PASS: inherited Visibility properties preserve native enum state and validation");
    }
    static void VerifyFrameContent(DerivedPage page)
    {
        var frame = new Frame();
        Check(frame.Content == null, "empty Frame content");
        Window.Current.Content = frame;
        frame.Content = page;
        Check(ReferenceEquals(frame.Content, page), "Frame preserves derived Page content identity");
        Check(page.Frame == null, "direct Frame content does not navigate");
        Check(ConnectFrameContent(41) == 0, "native Frame content callback HRESULT");
        Check(page.ConnectionId == 41 && ReferenceEquals(page.Connected, page), "native Frame content is the managed Page identity");
        page.Connected = null;
        frame.Content = "frame text";
        Check((string)frame.Content == "frame text", "Frame boxed content");
        frame.Content = null;
        Check(frame.Content == null, "Frame content clear");
        Console.WriteLine("PASS: Frame content preserves managed Page identity, boxed values and clearing");
    }
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
#if NATIVE_ABI
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ValidateWindowSizeArgs(IntPtr args);
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int InvokePageOverrides(IntPtr page);
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int InvokeKeyDown(IntPtr page, byte withArgs);
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int InvokeComponentConnector(IntPtr page, int id, IntPtr target);
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ConnectNavigationArgs(IntPtr page, int id);
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ConnectNamelessTarget(IntPtr page, int id, int nameResult);
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int CheckNamelessTarget(IntPtr target);
    static void Connect(Page page, int id, object target)
    {
        using (var source = NativeObject.Argument(page, new Guid("676d0be9-b65c-41c6-ba40-58cf87f201c1")))
        using (var value = NativeObject.Box(target))
            Errors.Check(InvokeComponentConnector(source.DangerousGetHandle(), id, value == null ? IntPtr.Zero : value.DangerousGetHandle()));
    }
    static void Invoke(Page page)
    {
        // An ordinary UI interface delegates QI to the controlling outer, as
        // native Frame navigation does. The inner itself bypasses overrides.
        using (var pointer = NativeObject.Argument(page, new Guid("676d0be9-b65c-41c6-ba40-58cf87f201c1")))
            Errors.Check(InvokePageOverrides(pointer.DangerousGetHandle()));
    }
    static void InvokeKey(Page page)
    {
        using (var pointer = NativeObject.Argument(page, new Guid("676d0be9-b65c-41c6-ba40-58cf87f201c1")))
            Errors.Check(InvokeKeyDown(pointer.DangerousGetHandle(), 1));
    }
#endif
    [MethodImpl(MethodImplOptions.NoInlining)]
    void Verify(DerivedPage page)
    {
#if FULL_SURFACE
        try { new UnsupportedKeyPage(); throw new Exception("unsupported key override accepted"); }
        catch (NotImplementedException error) { Check(error.Message.Contains("OnKeyUp override"), "unsupported key override is named"); }
        Console.WriteLine("PASS: unselected inherited key overrides still fail construction by name");
#endif
        Check(page.Frame == null, "unhosted Page.Frame");
        var content = new Frame(); page.Content = content;
        Check(ReferenceEquals(page.Content, content), "UserControl content identity on Page");
        page.Content = null; Check(page.Content == null, "Page content clear");
        var property = DependencyProperty.RegisterAttached("PageState", typeof(string), typeof(DerivedPage), null);
        page.SetValue(property, "composed page"); Check((string)page.GetValue(property) == "composed page", "Page native dependency store");
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
        Window.Current.Content = page;
        Check(InvokeWindowKeyDown(1) == 0 && page.KeyDowns == 1, "native inherited key override dispatch");
        GC.Collect(); GC.WaitForPendingFinalizers();
        Check(page.KeyArgs != null && page.KeyArgs.Key == Windows.System.VirtualKey.Escape && page.KeyArgs.Handled,
            "key arguments retain native state after callback and GC");
        Check(InvokeWindowKeyDown(0) == 0 && page.KeyDowns == 2 && page.KeyArgs == null, "null key argument");
        Window.Current.Content = new PartialPage();
        Check(InvokeWindowKeyDown(1) == 0, "unoverridden inherited key callback uses native base");
        Window.Current.Content = page;
        Check(ReferenceEquals(Window.Current.Content, page), "Window preserves derived Page identity");
        Check(ConnectWindowContent(31) == 0, "external component callback HRESULT");
        Check(page.ConnectionId == 31 && ReferenceEquals(page.Connected, page), "external component callback identity");
        page.Connected = null;
        var partial = new PartialPage();
#if NATIVE_ABI
        Invoke(page);
        Check(page.To == 1 && page.From == 1 && page.Leaving == 1, "native Page override dispatch");
        Check(page.ToArgs != null && ReferenceEquals(page.ToArgs, page.FromArgs), "native navigation argument identity");
        Invoke(partial); Check(partial.Leaving == 1, "inherited native base callbacks");
        Check(page.ToArgs.Uri == null, "null native Uri");
        var uri = new Uri("ms-appx:///Assets/Splash%20screen.xaml");
        page.ToArgs.Uri = uri;
        Check(page.ToArgs.Uri.AbsoluteUri == uri.AbsoluteUri, "System.Uri native round trip");
        var unicode = new Uri("https://example.test/é?q=hello%20world#part");
        page.ToArgs.Uri = unicode;
        GC.Collect(); GC.WaitForPendingFinalizers();
        Check(page.ToArgs.Uri.AbsoluteUri == unicode.AbsoluteUri, "retained Unicode Uri");
        try { page.ToArgs.Uri = new Uri("relative.xaml", UriKind.Relative); throw new Exception("relative Uri accepted"); }
        catch (ArgumentException) { }
        Check(page.ToArgs.Uri.AbsoluteUri == unicode.AbsoluteUri, "rejected Uri preserves value");
        page.ToArgs.Uri = null; Check(page.ToArgs.Uri == null, "clear native Uri");
        using (var source = NativeObject.Argument(page, new Guid("676d0be9-b65c-41c6-ba40-58cf87f201c1")))
            Errors.Check(ConnectNavigationArgs(source.DangerousGetHandle(), 6));
        Check(page.Connected is NavigationEventArgs, "native-created component target has projected runtime type");
        GC.Collect(); GC.WaitForPendingFinalizers();
        Check(((NavigationEventArgs)page.Connected).Uri == null, "callback target remains valid after native release");
        using (var source = NativeObject.Argument(page, new Guid("676d0be9-b65c-41c6-ba40-58cf87f201c1")))
        using (var nativeUri = AppxPortable.WinRT.Projected.Windows_Foundation_Uri.ToNative(uri))
            Errors.Check(InvokeComponentConnector(source.DangerousGetHandle(), 7, nativeUri.DangerousGetHandle()));
        Check(page.Connected is Uri && ((Uri)page.Connected).AbsoluteUri == uri.AbsoluteUri, "native component target uses System.Uri redirect");
        using (var source = NativeObject.Argument(page, new Guid("676d0be9-b65c-41c6-ba40-58cf87f201c1")))
            Errors.Check(ConnectNamelessTarget(source.DangerousGetHandle(), 8, unchecked((int)0x80004001)));
        Check(page.ConnectionId == 8 && page.Connected != null, "unnamed native target reaches connector");
        GC.Collect(); GC.WaitForPendingFinalizers();
        using (var target = NativeObject.Box(page.Connected))
            Errors.Check(CheckNamelessTarget(target.DangerousGetHandle()));
        using (var scope = Errors.Begin())
        using (var source = NativeObject.Argument(page, new Guid("676d0be9-b65c-41c6-ba40-58cf87f201c1")))
        {
            try { Errors.Check(ConnectNamelessTarget(source.DangerousGetHandle(), 99, unchecked((int)0x80004005))); throw new Exception("unexpected class-name error accepted"); }
            catch (COMException error) { Check(error.ErrorCode == unchecked((int)0x80004005), "unexpected native failure propagated"); }
        }
        Check(page.ConnectionId == 8, "unexpected native failure stops connection");
        Connect(page, 9, "boxed target");
        Check((string)page.Connected == "boxed target", "boxed native string unboxed");
        Connect(page, -7, content);
        Check(page.ConnectionId == -7 && ReferenceEquals(page.Connected, content), "native connector signed ID and target identity");
        Connect(page, 9, page);
        Check(ReferenceEquals(page.Connected, page), "native connector composed self identity");
        Connect(page, 10, null);
        Check(page.ConnectionId == 10 && page.Connected == null, "native connector null target");
        using (var scope = Errors.Begin())
        {
            page.Failure = new InvalidOperationException("connector failure");
            try { Connect(page, 11, content); throw new Exception("connector failure accepted"); }
            catch (InvalidOperationException error) { Check(ReferenceEquals(error, page.Failure), "connector exception identity"); }
            page.Failure = null;
        }
        Connect(page, 12, content);
        Check(page.ConnectionId == 12, "connector recovers after exception");
        Console.WriteLine("PASS: System.Uri marshals through native navigation and managed component connections retain identity");
        page.Failure = new InvalidOperationException("page override failure");
        using (var scope = Errors.Begin())
        {
            try { Invoke(page); throw new Exception("Page override error accepted"); }
            catch (InvalidOperationException error) { Check(ReferenceEquals(error, page.Failure), "Page override exception identity"); }
        }
        page.Failure = null;
        Console.WriteLine("PASS: native Page callbacks dispatch through derived managed overrides and base methods");
        page.Failure = new InvalidOperationException("key override failure");
        using (var scope = Errors.Begin())
        {
            try { InvokeKey(page); throw new Exception("key override error accepted"); }
            catch (InvalidOperationException error) { Check(ReferenceEquals(error, page.Failure), "key override exception identity"); }
        }
        page.Failure = null;
        InvokeKey(page);
        Check(page.KeyArgs != null && page.KeyArgs.Handled, "key override recovers after failure");
        Console.WriteLine("PASS: inherited Control key overrides preserve arguments, base calls and exception identity");
#else
        Check(partial.Frame == null, "partial Page construction");
#endif
        VerifyVisibility(page);
        VerifyFrameContent(page);
        VerifyWindowSize();
        VerifyApplicationResources();
        VerifyResourceMap();
        VerifyApplicationView();
        VerifyNavigation();
        Window.Current.Activate();
        Console.WriteLine("PASS: managed Page composes native inheritance, content and window identity");
        VerifySharing();
        VerifyDispatcher();
    }
    protected override void OnLaunched(Windows.ApplicationModel.Activation.LaunchActivatedEventArgs args) { Verify(new DerivedPage()); }
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
