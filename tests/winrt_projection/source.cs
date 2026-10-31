using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;

using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Controls.Primitives;
using Windows.UI.Xaml.Markup;
using Windows.UI.Xaml.Media.Animation;
class SourcePage : Page, IComponentConnector
{
    internal readonly Dictionary<int, object> Targets = new Dictionary<int, object>();
    internal readonly List<int> Connections = new List<int>();
    internal Exception Failure;
    void IComponentConnector.Connect(int id, object target)
    {
        if (id == 99) throw Failure;
        Targets[id] = target;
        Connections.Add(id);
        if (FindName("leaf") == null) throw new Exception("names not registered before Connect");
    }
}
class SourceApp : Application
{
    const string Ns = " xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\"";
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ValidateLoadedSource();
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ExerciseResourceMap();
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ExerciseAnimationData();
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ValidateSourceRootFields();
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ValidateGridChildren(int stage);
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ValidateLoadedResources(int stage);
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    static void Bad(SourcePage page, string body, int hr)
    {
        File.WriteAllText("Fixtures/bad.xaml", body);
        var content = page.Content;
        var resources = page.Resources;
        try { Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/bad.xaml")); throw new Exception("bad source accepted"); }
        catch (Exception error) { if (error.Message == "bad source accepted" || (hr != 0 && Marshal.GetHRForException(error) != hr)) throw; }
        Check(ValidateSourceRootFields() == 0, "invalid source changed dimensions or name");
        Check(ReferenceEquals(content, page.Content), "invalid source changed content");
        Check(ReferenceEquals(resources, page.Resources), "invalid source changed resources");
        Check(ReferenceEquals(page.FindName("root"), page), "invalid source changed namescope");
    }
    static void WrongThread(Action action)
    {
        Exception failure = null;
        var worker = new Thread(() => { try { action(); } catch (Exception error) { failure = error; } });
        worker.Start(); worker.Join();
        Check(failure != null && Marshal.GetHRForException(failure) == unchecked((int)0x8001010e), "source API did not reject wrong thread");
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static Frame RetainChild(out WeakReference root)
    {
        var page = new SourcePage();
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/Éntry%20space.xaml"));
        root = new WeakReference(page);
        return (Frame)page.Content;
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void Lookup(Frame child) { child.FindName("root"); }
    static void VerifyRootLifetime()
    {
        WeakReference root;
        var child = RetainChild(out root);
        var collector = new Thread(() => { for (int i = 0; i < 4; ++i) { GC.Collect(); GC.WaitForPendingFinalizers(); } });
        collector.Start();
        for (int i = 0; i < 100; ++i) Lookup(child);
        collector.Join();
        for (int i = 0; i < 3; ++i) { GC.Collect(); GC.WaitForPendingFinalizers(); }
        Check(!root.IsAlive, "namescope retained its root");
        Check(child.FindName("root") == null, "expired root namescope resolved a root");
        GC.KeepAlive(child);
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void VerifyResources(SourcePage page)
    {
        var initial = page.Resources;
        Check(initial != null && ReferenceEquals(initial, page.Resources), "default dictionary identity");
        var replacement = new ResourceDictionary();
        page.Resources = replacement;
        Check(ReferenceEquals(page.Resources, replacement), "managed dictionary property identity");
        var property = DependencyProperty.RegisterAttached("ResourceState", typeof(string), typeof(SourcePage), null);
        replacement.SetValue(property, "resource state");
        Check((string)replacement.GetValue(property) == "resource state", "dictionary dependency values");
        Check(ExerciseResourceMap() == 0, "resource dictionary native map contract");
        page.Resources = initial;
        File.WriteAllText("Fixtures/resources.xaml", "<Page" + Ns + " x:Name=\"root\"><Page.Resources>" +
            "<Grid x:Key=\"first\" Width=\"11\"/><Grid x:Name=\"named\" x:ConnectionId=\"5\" Width=\"22\"><Grid.Resources><Grid x:Key=\"first\" Width=\"55\"/></Grid.Resources></Grid>" +
            "<Grid x:Key=\"distinct\" x:Name=\"visible\" Width=\"33\"/></Page.Resources><Grid x:Name=\"leaf\"/></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/resources.xaml"));
        Check(ReferenceEquals(page.Targets[5], page.FindName("named")), "named resource connection identity");
        Check(page.FindName("first") == null && page.FindName("distinct") == null, "keys were registered as names");
        Check(ValidateLoadedResources(1) == 0, "implicit resources and named keys");
        var previous = page.Resources;
        File.WriteAllText("Fixtures/resources.xaml", "<Page" + Ns + " x:Name=\"root\"><Page.Resources><ResourceDictionary>" +
            "<Grid x:Key=\"explicit\" Width=\"44\"/></ResourceDictionary></Page.Resources><Grid x:Name=\"leaf\"/></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/resources.xaml"));
        Check(!ReferenceEquals(previous, page.Resources) && page.FindName("named") == null, "resource replacement namescope");
        Check(ValidateLoadedResources(2) == 0, "explicit dictionary and retained previous resources");
        File.WriteAllText("Fixtures/resources.xaml", "<Page" + Ns + " x:Name=\"root\"><Page.Resources><ResourceDictionary/></Page.Resources><Grid x:Name=\"leaf\"/></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/resources.xaml"));
        Check(ValidateLoadedResources(3) == 0, "empty explicit dictionary");
        WrongThread(() => GC.KeepAlive(page.Resources));
        Bad(page, "<Page" + Ns + "><Page.Resources><DoubleAnimation x:Name=\"unsupportedAnimation\"/></Page.Resources></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><Page.Resources><Grid x:Key=\"same\"/><Grid x:Key=\"same\"/></Page.Resources></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Page.Resources><Grid/></Page.Resources></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid x:Key=\"outside\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Page.Resources/><Page.Resources/></Page>", unchecked((int)0x80070057));
        Console.WriteLine("PASS: native resource dictionaries preserve map semantics, source keys, names and ownership");
        GC.KeepAlive(previous);
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static Storyboard RetainAnimation(out WeakReference root)
    {
        var page = new SourcePage();
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/animations.xaml"));
        root = new WeakReference(page);
        return (Storyboard)page.FindName("unconnected");
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static object RetainGridChild(out WeakReference root)
    {
        var page = new SourcePage();
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/grid.xaml"));
        root = new WeakReference(page);
        return page.FindName("first");
    }
    static void VerifyGrid(SourcePage page)
    {
        int connectionStart = page.Connections.Count;
        File.WriteAllText("Fixtures/grid.xaml", "<Page" + Ns + " x:Name=\"root\"><Grid x:Name=\"container\"><Grid x:Name=\"first\" x:ConnectionId=\"21\"><Grid.Children><Grid x:Name=\"leaf\"/></Grid.Children></Grid><Frame x:Name=\"second\" x:ConnectionId=\"22\"><Grid/></Frame></Grid></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/grid.xaml"));
        Check(ReferenceEquals(page.FindName("first"), page.Targets[21]) && ReferenceEquals(page.FindName("second"), page.Targets[22]), "Grid child connection identities");
        Check(page.Connections.Count == connectionStart + 2 && page.Connections[connectionStart] == 21 && page.Connections[connectionStart + 1] == 22, "Grid child callback order");
        Check(ValidateGridChildren(0) == 0, "ordered native Grid children and collection ABI");
        Bad(page, "<Page" + Ns + " Width=\"999\"><Grid><Storyboard/></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><Grid/><Grid.Children/></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Children/><Grid/></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Children/><Grid.Children/></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Children Width=\"1\"/></Grid></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Background><Grid/></Grid.Background></Grid></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><Grid><Grid x:Name=\"same\"/><Grid x:Name=\"same\"/></Grid></Page>", unchecked((int)0x80070057));
        File.WriteAllText("Fixtures/grid-empty.xaml", "<Page" + Ns + " x:Name=\"root\"><Grid><Grid.Children/></Grid></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/grid-empty.xaml"));
        Check(ValidateGridChildren(1) == 0 && page.FindName("first") == null, "Grid replacement preserves retained children and clears names");
        WeakReference root;
        var retained = RetainGridChild(out root);
        for (int i = 0; i < 3; ++i) { GC.Collect(); GC.WaitForPendingFinalizers(); }
        Check(!root.IsAlive, "retained Grid child kept the Page alive");
        GC.KeepAlive(retained);
        Console.WriteLine("PASS: source Grid children preserve order, names, callbacks and retained collection lifetime");
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void VerifyAnimations(SourcePage page)
    {
        File.WriteAllText("Fixtures/animations.xaml", "<Page" + Ns + " x:Name=\"root\"><Page.Resources>" +
            "<Storyboard x:Name=\"animation\" x:ConnectionId=\"7\"><FadeInThemeAnimation x:Name=\"fade\" Storyboard.TargetName=\"leaf\" Duration=\"0:0:4.5000001\"/>" +
            "<FadeOutThemeAnimation Storyboard.TargetName=\"leaf\"/></Storyboard>" +
            "<Storyboard x:Name=\"unconnected\"><Storyboard.Children><FadeOutThemeAnimation TargetName=\"inner target\" Duration=\"Automatic\"/></Storyboard.Children></Storyboard>" +
            "</Page.Resources><Grid x:Name=\"leaf\"/></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/animations.xaml"));
        var storyboard = page.FindName("animation") as Storyboard;
        Check(storyboard != null && ReferenceEquals(storyboard, page.Targets[7]), "named storyboard projection identity");
        Check(page.FindName("unconnected") is Storyboard, "uncached named storyboard projection");
        Check(page.FindName("fade") is FadeInThemeAnimation, "named theme animation projection");
        Bad(page, "<Page" + Ns + " x:Name=\"changed\" Width=\"999\"><Page.Resources><Grid x:Key=\"new\"/></Page.Resources><Storyboard/></Page>", unchecked((int)0x80070057));
        Check(ExerciseAnimationData() == 0, "native timeline properties, children and collection lifetime");
        try { storyboard.Begin(); throw new Exception("unimplemented storyboard clock accepted"); }
        catch (NotImplementedException) { }
        WrongThread(() => storyboard.Begin());
        Bad(page, "<Page" + Ns + "><Page.Resources><Storyboard x:Name=\"invalid\"><FadeInThemeAnimation Duration=\"0:0:-1\"/></Storyboard></Page.Resources></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Page.Resources><Storyboard x:Name=\"invalid\"><Grid/></Storyboard></Page.Resources></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Page.Resources><Storyboard x:Name=\"invalid\"><DoubleAnimation/></Storyboard></Page.Resources></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><Page.Resources><Storyboard x:Name=\"invalid\"><FadeInThemeAnimation TargetName=\"{Binding Name}\"/></Storyboard></Page.Resources></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><Page.Resources><Storyboard x:Name=\"invalid\"><FadeInThemeAnimation Storyboard.TargetName=\"{Binding Name}\"/></Storyboard></Page.Resources></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><Page.Resources><Storyboard x:Name=\"invalid\"><FadeInThemeAnimation Storyboard.TargetProperty=\"{StaticResource Property}\"/></Storyboard></Page.Resources></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><Page.Resources><Storyboard x:Name=\"invalid\"><FadeInThemeAnimation Duration=\"256204778:48:05.4775808\"/></Storyboard></Page.Resources></Page>", unchecked((int)0x80070057));
        WeakReference root;
        var retained = RetainAnimation(out root);
        for (int i = 0; i < 3; ++i) { GC.Collect(); GC.WaitForPendingFinalizers(); }
        Check(!root.IsAlive, "retained storyboard kept the component root alive");
        GC.KeepAlive(retained);
        File.WriteAllText("Fixtures/frame-data.xaml", "<Page" + Ns + " x:Name=\"root\"><Frame><Storyboard x:Name=\"contentData\"/></Frame></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/frame-data.xaml"));
        Check(page.Content is Frame && page.FindName("contentData") is Storyboard, "Frame object-typed timeline content");
        Console.WriteLine("PASS: storyboard data loads with typed names, timeline properties and owned children; unsupported clocks fail explicitly");
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    void Verify(SourcePage page)
    {
        Directory.CreateDirectory("Fixtures");
        File.WriteAllText("Fixtures/Éntry space.xaml", "<Page" + Ns + " xmlns:design=\"http://schemas.microsoft.com/expression/blend/2008\" xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\" mc:Ignorable=\"design\" design:DesignWidth=\"999\" x:Class=\"SourcePage\" x:Name=\"root\" x:ConnectionId=\"1\" Width=\"321\" Height=\"123\">" +
            "<Frame x:Name=\"frame\" x:ConnectionId=\"2\" Width=\"80.5\" Height=\"90\" Visibility=\"Collapsed\"><Frame.Content>" +
            "<Page x:Name=\"inner\" x:ConnectionId=\"3\"><Grid x:Name=\"leaf\" x:ConnectionId=\"4\" Width=\"42\" /></Page>" +
            "</Frame.Content></Frame></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/Éntry%20space.xaml"));
        Check(page.Content is Frame, "source root content type");
        Check(ReferenceEquals(page.Content, page.FindName("frame")), "content and namescope identity");
        Check(ReferenceEquals(page.FindName("root"), page), "named managed root identity");
        for (int id = 1; id <= 4; ++id) Check(page.Targets.ContainsKey(id), "connection target " + id);
        Check(ReferenceEquals(page.Targets[1], page) && ReferenceEquals(page.Targets[2], page.Content), "connection identities");
        Check(ReferenceEquals(page.Targets[4], page.FindName("leaf")), "native leaf connection identity");
        GC.Collect(); GC.WaitForPendingFinalizers();
        Check(ValidateLoadedSource() == 0, "native tree, names, dimensions and visibility");
        Check(page.FindName("SwapChainPanel") == null, "loaded namescope fabricated a missing element");
        WrongThread(() => page.FindName("root"));
        WrongThread(() => Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/Éntry%20space.xaml")));
        VerifyRootLifetime();
        Bad(page, "<Page" + Ns + " Width=\"999\"><TextBlock/></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + " Opacity=\"0.5\"/>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><Grid Width=\"-1\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + " x:Name=\"same\"><Frame x:Name=\"same\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Frame/><Frame/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Frame" + Ns + "/>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + " x:Class=\"WrongPage\"/>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + ">text</Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><Frame></Page>", 0);
        Bad(page, "<!DOCTYPE Page [<!ENTITY value 'bad'>]><Page" + Ns + "/>", 0);
        Check(ValidateLoadedSource() == 0, "invalid source changed properties or tree");
        try { Application.LoadComponent(page, new Uri("ms-appx:///missing.xaml")); throw new Exception("missing file accepted"); }
        catch (FileNotFoundException) { }
        try { Application.LoadComponent(page, new Uri("https://example.test/source.xaml")); throw new Exception("remote source accepted"); }
        catch (ArgumentException) { }
        try { Application.LoadComponent(page, new Uri("ms-appx:///missing.xaml"), ComponentResourceLocation.Nested); throw new Exception("nested resources accepted"); }
        catch (NotImplementedException) { }
        VerifyResources(page);
        VerifyGrid(page);
        VerifyAnimations(page);
        File.WriteAllText("Fixtures/error.xaml", "<Page" + Ns + " x:ConnectionId=\"99\"/>");
        page.Failure = new InvalidOperationException("source callback failure");
        try { Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/error.xaml")); throw new Exception("callback error accepted"); }
        catch (InvalidOperationException error) { Check(ReferenceEquals(error, page.Failure), "source callback exception identity"); }
        File.WriteAllText("Fixtures/empty.xaml", "<Page" + Ns + " x:Name=\"replacement\"/>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/empty.xaml"), ComponentResourceLocation.Application);
        Check(page.Content == null && page.FindName("leaf") == null && page.FindName("root") == null, "replacement tree and namescope");
        Check(ReferenceEquals(page.FindName("replacement"), page), "replacement root name");
        Console.WriteLine("PASS: source XAML builds native content, properties, names and managed connections; unsupported input fails explicitly");
        Exit();
    }
    protected override void OnLaunched(Windows.ApplicationModel.Activation.LaunchActivatedEventArgs args)
    {
        var page = new SourcePage(); Window.Current.Content = page; Verify(page);
    }
}
class SourceProjectionTest
{
    [STAThread]
    static int Main()
    {
        try { Application.Start(p => new SourceApp()); return 1; }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
