using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Navigation;
#if NATIVE_ABI
using AppxPortable.WinRT;
#endif
class TestPage : Page
{
    internal int To, From, Leaving;
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
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
#if NATIVE_ABI
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int InvokePageOverrides(IntPtr page);
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
        var partial = new PartialPage();
#if NATIVE_ABI
        Invoke(page);
        Check(page.To == 1 && page.From == 1 && page.Leaving == 1, "native Page override dispatch");
        Check(page.ToArgs != null && ReferenceEquals(page.ToArgs, page.FromArgs), "native navigation argument identity");
        Invoke(partial); Check(partial.Leaving == 1, "inherited native base callbacks");
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
