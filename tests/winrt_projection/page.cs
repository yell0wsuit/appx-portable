using System;
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
    protected override void OnNavigatedTo(NavigationEventArgs args) { ++To; ToArgs = args; base.OnNavigatedTo(args); }
    protected override void OnNavigatedFrom(NavigationEventArgs args) { ++From; FromArgs = args; base.OnNavigatedFrom(args); }
    protected override void OnNavigatingFrom(NavigatingCancelEventArgs args) { ++Leaving; if (Failure != null) throw Failure; base.OnNavigatingFrom(args); }
}
class DerivedPage : TestPage { }
class PartialPage : Page
{
    internal int Leaving;
    protected override void OnNavigatingFrom(NavigatingCancelEventArgs args) { ++Leaving; base.OnNavigatingFrom(args); }
}
class PageApp : Application
{
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ConnectWindowContent(int id);
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
#if NATIVE_ABI
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int InvokePageOverrides(IntPtr page);
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
#endif
    [MethodImpl(MethodImplOptions.NoInlining)]
    void Verify(DerivedPage page)
    {
        Check(page.Frame == null, "unhosted Page.Frame");
        var content = new Frame(); page.Content = content;
        Check(ReferenceEquals(page.Content, content), "UserControl content identity on Page");
        page.Content = null; Check(page.Content == null, "Page content clear");
        var property = DependencyProperty.RegisterAttached("PageState", typeof(string), typeof(DerivedPage), null);
        page.SetValue(property, "composed page"); Check((string)page.GetValue(property) == "composed page", "Page native dependency store");
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
        Console.WriteLine("PASS: native Page callbacks dispatch through derived managed overrides and base methods");
#else
        Check(partial.Frame == null, "partial Page construction");
#endif
        Window.Current.Activate();
        Console.WriteLine("PASS: managed Page composes native inheritance, content and window identity");
        Exit();
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
