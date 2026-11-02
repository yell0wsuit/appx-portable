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
    static extern int ExerciseAnimationClock(int stage);
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ValidateSourceRootFields();
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ValidateGridChildren(int stage);
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ValidateBrushData(int stage);
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ValidateImageData(int stage);
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ValidateProgressData(int stage);
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ValidateTextData(int stage);
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ValidateViewboxData(int stage);
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
    [DllImport("page_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int ValidateApplicationTheme(int expected);
    static void BadApplication(string body, int expected)
    {
        var app = Application.Current;
        var before = app.Resources;
        File.WriteAllText("Fixtures/application-bad.xaml", body);
        try { Application.LoadComponent(app, new Uri("ms-appx:///Fixtures/application-bad.xaml")); throw new Exception("bad application accepted"); }
        catch (Exception error) { if (error.Message == "bad application accepted" || (expected != 0 && Marshal.GetHRForException(error) != expected)) throw; }
        Check(ReferenceEquals(before, app.Resources), "invalid application source changed resources");
        Check(ValidateApplicationTheme(1) == 0, "invalid application source changed theme");
    }
    static void VerifyApplicationSource()
    {
        var app = Application.Current;
        var head = "<Application" + Ns + " x:Class=\"SourceApp\" RequestedTheme=\"Light\">";
        File.WriteAllText("App.xaml", head + "<Application.Resources><ResourceDictionary><Grid x:Key=\"startup Ω\"/></ResourceDictionary></Application.Resources></Application>");
        WrongThread(() => { var ignored = app.Resources; });
        var resources = app.Resources;
        var map = (IDictionary<object, object>)(object)resources;
        Check(map.Count == 1 && map["startup Ω"] is Grid, "App.xaml initializes typed application resources");
        Check(ReferenceEquals(resources, app.Resources) && ValidateApplicationTheme(1) == 0, "lazy application identity and theme");
        File.WriteAllText("App.xaml", "<invalid/>");
        Check(ReferenceEquals(resources, app.Resources), "App.xaml was reloaded after successful initialization");
        File.WriteAllText("Fixtures/application.xaml", head + "<Application.Resources><Storyboard x:Key=\"transition\"/></Application.Resources></Application>");
        Application.LoadComponent(app, new Uri("ms-appx:///Fixtures/application.xaml"));
        var replacement = app.Resources;
        Check(!ReferenceEquals(resources, replacement), "explicit application resource replacement");
        Check(((IDictionary<object, object>)(object)replacement)["transition"] is Storyboard && map.ContainsKey("startup Ω"), "application replacement retains old dictionaries");
        WrongThread(() => Application.LoadComponent(app, new Uri("ms-appx:///Fixtures/application.xaml")));
        WrongThread(() => app.Resources = resources);
        BadApplication("<Application" + Ns + " x:Class=\"WrongApp\"/>", unchecked((int)0x80070057));
        BadApplication("<Application" + Ns + " RequestedTheme=\"Wrong\"/>", unchecked((int)0x80070057));
        BadApplication("<Application" + Ns + " Width=\"20\"/>", unchecked((int)0x80004001));
        BadApplication("<Application" + Ns + "><Grid/></Application>", unchecked((int)0x80004001));
        BadApplication("<Application" + Ns + " RequestedTheme=\"Dark\"><Application.Resources><Grid x:Key=\"same\"/><Grid x:Key=\"same\"/></Application.Resources></Application>", unchecked((int)0x80070057));
        BadApplication("<Application" + Ns + "><Application.Resources><ResourceDictionary Source=\"missing.xaml\"/></Application.Resources></Application>", unchecked((int)0x80004001));
        BadApplication("<Application" + Ns + "><Application.Resources><Grid x:Name=\"named\"/></Application.Resources></Application>", unchecked((int)0x80004001));
        BadApplication("<Application" + Ns + "><Application.Resources><ResourceDictionary><ResourceDictionary.ThemeDictionaries/></ResourceDictionary></Application.Resources></Application>", unchecked((int)0x80004001));
        BadApplication("<Application" + Ns + "><Application.Resources><Grid x:Key=\"one\"/></Application.Resources>", 0);
        BadApplication("<Application" + Ns + "><Application.Resources/>", 0);
        BadApplication("<Application" + Ns + ">", 0);
        try { Application.LoadComponent(app, new Uri("ms-appx:///Fixtures/application-missing.xaml")); throw new Exception("missing application accepted"); }
        catch (FileNotFoundException) { }
        Check(ReferenceEquals(replacement, app.Resources), "missing application changed resources");
        app.Resources = resources;
        Check(ReferenceEquals(resources, app.Resources), "explicit assignment must survive invalid App.xaml");
        File.Delete("App.xaml");
        Console.WriteLine("PASS: application source initializes lazily, replaces resources transactionally and rejects unsupported roots");
    }
    static string Merged(string sources, string entries)
    {
        return "<Application" + Ns + " RequestedTheme=\"Light\"><Application.Resources><ResourceDictionary><ResourceDictionary.MergedDictionaries>" +
            sources + "</ResourceDictionary.MergedDictionaries>" + entries + "</ResourceDictionary></Application.Resources></Application>";
    }
    static string Dictionary(string body)
    {
        return "<ResourceDictionary" + Ns + " xmlns:d=\"http://schemas.microsoft.com/expression/blend/2008\" xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\" mc:Ignorable=\"d\">" + body + "</ResourceDictionary>";
    }
    static void VerifyMergedSources()
    {
        var app = Application.Current;
        var original = app.Resources;
        Directory.CreateDirectory("Fixtures/Merged");
        // Unsupported entries stay unbuilt until looked up; theme keys are indexed but not resolved yet.
        File.WriteAllText("Fixtures/Merged/a.xaml", Dictionary(
            "<ResourceDictionary.ThemeDictionaries><ResourceDictionary x:Key=\"Default\"><x:String x:Key=\"themed\">glyph</x:String></ResourceDictionary></ResourceDictionary.ThemeDictionaries>" +
            "<Grid x:Key=\"shared\"/><Storyboard x:Key=\"a-only\"/><Style x:Key=\"unsupported\" TargetType=\"Frame\"><Setter Property=\"Template\" Value=\"{StaticResource missing}\"/></Style>" +
            "<x:String x:Key=\"text\">hello</x:String><Grid x:Name=\"named\" d:Width=\"2\"/><Grid x:Key=\"texty\">oops</Grid>" +
            "<Style x:Key=\"valued\" TargetType=\"TextBlock\"><Setter Property=\"FontWeight\"><Setter.Value><FontWeight>SemiLight</FontWeight></Setter.Value></Setter></Style>"));
        File.WriteAllText("Fixtures/Merged/b.xaml", Dictionary(
            "<ResourceDictionary.MergedDictionaries><ResourceDictionary Source=\"Deep/c.xaml\"/></ResourceDictionary.MergedDictionaries><Storyboard x:Key=\"shared\"/>"));
        Directory.CreateDirectory("Fixtures/Merged/Deep");
        File.WriteAllText("Fixtures/Merged/Deep/c.xaml", Dictionary("<Grid x:Key=\"deep\"/><Grid x:Key=\"a-only\"/>"));
        File.WriteAllText("Fixtures/merged.xaml", Merged("<ResourceDictionary Source=\"Merged/a.xaml\"/><ResourceDictionary Source=\"ms-appx:///Fixtures/Merged/b.xaml\"/>", "<Grid x:Key=\"own\"/>"));
        Application.LoadComponent(app, new Uri("ms-appx:///Fixtures/merged.xaml"));
        var map = (IDictionary<object, object>)(object)app.Resources;
        Check(map.Count == 1 && map["own"] is Grid, "merged entries counted as own entries");
        Check(map["shared"] is Storyboard, "later merged dictionary must win");
        Check(map["deep"] is Grid, "nested relative merged source");
        Check(map["a-only"] is Grid, "a later merged dictionary's nested merge must win over an earlier merged entry");
        Check(ReferenceEquals(map["a-only"], map["a-only"]) && map["named"] is Grid, "deferred entry identity");
        Check(map.ContainsKey("unsupported") && map.ContainsKey("text") && !map.ContainsKey("missing"), "deferred key index");
        foreach (var key in new[] { "unsupported", "unsupported", "texty", "text" })
        {
            try { var ignored = map[key]; throw new Exception("unsupported entry built: " + key); }
            catch (NotImplementedException) { }
        }
        try { var ignored = map["themed"]; throw new Exception("theme entry resolved"); }
        catch (NotImplementedException) { }
        try { var ignored = map["missing"]; throw new Exception("missing entry found"); }
        catch (KeyNotFoundException) { }
        WrongThread(() => { var ignored = map["deep"]; });
        var loaded = app.Resources;
        File.WriteAllText("Fixtures/Merged/cycle.xaml", Dictionary("<ResourceDictionary.MergedDictionaries><ResourceDictionary Source=\"cycle.xaml\"/></ResourceDictionary.MergedDictionaries>"));
        File.WriteAllText("Fixtures/Merged/duplicate.xaml", Dictionary("<Grid x:Key=\"same\"/><Storyboard x:Key=\"same\"/>"));
        File.WriteAllText("Fixtures/Merged/keyless.xaml", Dictionary("<Grid/>"));
        File.WriteAllText("Fixtures/Merged/root.xaml", "<Grid" + Ns + "/>");
        File.WriteAllText("Fixtures/Merged/attribute.xaml", Dictionary("").Replace("<ResourceDictionary ", "<ResourceDictionary Source=\"a.xaml\" "));
        File.WriteAllText("Fixtures/Merged/broken.xaml", Dictionary("<Grid x:Key=\"open\">"));
        BadApplication(Merged("<ResourceDictionary Source=\"Merged/missing.xaml\"/>", ""), unchecked((int)0x80070002));
        BadApplication(Merged("<ResourceDictionary Source=\"Merged/cycle.xaml\"/>", ""), unchecked((int)0x80070057));
        BadApplication(Merged("<ResourceDictionary Source=\"Merged/duplicate.xaml\"/>", ""), unchecked((int)0x80070057));
        BadApplication(Merged("<ResourceDictionary Source=\"Merged/keyless.xaml\"/>", ""), unchecked((int)0x80070057));
        BadApplication(Merged("<ResourceDictionary Source=\"Merged/root.xaml\"/>", ""), unchecked((int)0x80070057));
        BadApplication(Merged("<ResourceDictionary Source=\"Merged/attribute.xaml\"/>", ""), unchecked((int)0x80004001));
        BadApplication(Merged("<ResourceDictionary Source=\"Merged/broken.xaml\"/>", ""), 0);
        BadApplication(Merged("<ResourceDictionary Source=\"../Merged/a.xaml\"/>", ""), unchecked((int)0x80004001));
        BadApplication(Merged("<ResourceDictionary Source=\"http://example.com/a.xaml\"/>", ""), unchecked((int)0x80070057));
        BadApplication(Merged("<ResourceDictionary Source=\"Merged/a.xaml\" x:Key=\"keyed\"/>", ""), unchecked((int)0x80004001));
        BadApplication(Merged("<ResourceDictionary><Grid x:Key=\"inline\"/></ResourceDictionary>", ""), unchecked((int)0x80004001));
        BadApplication(Merged("<Grid/>", ""), unchecked((int)0x80070057));
        Check(ReferenceEquals(loaded, app.Resources), "invalid merged source replaced resources");
        app.Resources = original;
        Directory.Delete("Fixtures/Merged", true);
        File.Delete("Fixtures/merged.xaml");
        Console.WriteLine("PASS: merged source dictionaries resolve in order and build entries on first lookup");
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
        var map = (IDictionary<object, object>)(object)page.Resources;
        Check(map.Count == 3 && ReferenceEquals(map["named"], page.Targets[5]), "managed lookup sees source-loaded native resources");
        Check(!map.ContainsKey("visible") && map["distinct"] is Grid, "resource keys differ from names");
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
    static Grid RetainGridChild(out WeakReference root)
    {
        var page = new SourcePage();
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/grid.xaml"));
        root = new WeakReference(page);
        return (Grid)page.FindName("first");
    }
    static void VerifyViewbox(SourcePage page)
    {
        File.WriteAllText("Fixtures/viewbox.xaml", "<Page" + Ns + " x:Name=\"root\"><Grid x:Name=\"leaf\"><Viewbox x:Name=\"box\" x:ConnectionId=\"29\" HorizontalAlignment=\"Left\" Stretch=\"None\" StretchDirection=\"DownOnly\" Width=\"320\" Visibility=\"Collapsed\"><Grid x:Name=\"inside\" x:ConnectionId=\"30\"/></Viewbox><Viewbox x:Name=\"explicitBox\"><Viewbox.Child><TextBlock x:Name=\"insideText\" Text=\"caption\"/></Viewbox.Child></Viewbox><Viewbox x:Name=\"emptyBox\"/></Grid></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/viewbox.xaml"));
        Check(page.FindName("box") is Viewbox && ReferenceEquals(page.FindName("box"), page.Targets[29]) && ReferenceEquals(page.FindName("inside"), page.Targets[30]), "Viewbox names and connection identities");
        Check(page.FindName("insideText") is TextBlock, "explicit Viewbox child namescope");
        Check(ValidateViewboxData(0) == 0, "native Viewbox child and stretch data");
        Bad(page, "<Page" + Ns + " Width=\"999\"><Viewbox><Grid/><Grid/></Viewbox></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Viewbox><Viewbox.Child/><Grid/></Viewbox></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Viewbox><Storyboard/></Viewbox></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Viewbox Stretch=\"Bad\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Viewbox StretchDirection=\"Bad\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Viewbox Stretch=\"{Binding Scale}\"/></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><Viewbox><Viewbox.Child Width=\"1\"><Grid/></Viewbox.Child></Viewbox></Page>", unchecked((int)0x80004001));
        page.Targets.Remove(29); page.Targets.Remove(30);
        File.WriteAllText("Fixtures/viewbox-empty.xaml", "<Page" + Ns + " x:Name=\"root\"><Grid/></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/viewbox-empty.xaml"));
        Check(ValidateViewboxData(1) == 0, "Viewbox child survives tree replacement");
        Console.WriteLine("PASS: Viewbox source children retain names, ownership and validated stretch data");
    }
    static void VerifyText(SourcePage page)
    {
        File.WriteAllText("Fixtures/text.xaml", "<Page" + Ns + " x:Name=\"root\"><Grid x:Name=\"leaf\"><TextBlock x:Name=\"tips\" x:ConnectionId=\"28\" Text=\"Éclair &amp; Ω\" FontFamily=\"/Fonts/MetaOT-Bold.otf#Meta OT\" FontSize=\"40\" Foreground=\"White\" TextWrapping=\"Wrap\" TextAlignment=\"Center\" HorizontalAlignment=\"Center\" VerticalAlignment=\"Bottom\" Width=\"1300\" Height=\"65\" Margin=\"0,40\"/><TextBlock TextAlignment=\"Start\"/><TextBlock TextAlignment=\"End\"/></Grid></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/text.xaml"));
        var tips = page.FindName("tips") as TextBlock;
        Check(tips != null && ReferenceEquals(tips, page.Targets[28]) && tips.Text == "Éclair & Ω", "typed TextBlock and Unicode text");
        Check(ValidateTextData(0) == 0, "SDK text, font descriptor and explicit style data");
        tips.Text = "updated\0tail Ω"; Check(tips.Text == "updated\0tail Ω", "length-preserving HSTRING text setter");
        tips.Text = null; Check(tips.Text == "", "null Text becomes empty HSTRING");
        tips.Text = "restored";
        WrongThread(() => tips.Text = "wrong thread");
        Check(tips.Text == "restored", "wrong thread changed Text");
        Bad(page, "<Page" + Ns + " Width=\"999\"><TextBlock FontSize=\"-1\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><TextBlock FontFamily=\"\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><TextBlock TextWrapping=\"Bad\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><TextBlock TextAlignment=\"Bad\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><TextBlock Foreground=\"{Binding Brush}\"/></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><TextBlock Text=\"{Binding Caption}\"/></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><TextBlock><Grid/></TextBlock></Page>", unchecked((int)0x80004001));
        File.WriteAllText("Fixtures/text-empty.xaml", "<Page" + Ns + " x:Name=\"root\"><TextBlock/></Page>");
        page.Targets.Remove(28);
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/text-empty.xaml"));
        Check(ValidateTextData(1) == 0, "retained font descriptor and explicit unavailable font metrics");
        Console.WriteLine("PASS: TextBlock text and explicit font/style data preserve HSTRING values and ownership");
    }
    static void VerifyProgress(SourcePage page)
    {
        File.WriteAllText("Fixtures/progress.xaml", "<Page" + Ns + " x:Name=\"root\"><Page.Resources><SolidColorBrush x:Name=\"transparentBrush\" Color=\"Transparent\"/><SolidColorBrush x:Name=\"defaultSolid\"/><SolidColorBrush x:Name=\"slate\" Color=\"DarkSlateGray\"/></Page.Resources><Grid x:Name=\"leaf\"><ProgressRing x:Name=\"ring\" x:ConnectionId=\"25\" IsActive=\"True\" Foreground=\" white \" Margin=\"0,420,0,0\" Width=\"100\" Height=\"100\"/><ProgressRing x:Name=\"inactive\" IsActive=\"false\"><ProgressRing.Foreground><SolidColorBrush x:Name=\"paint\" x:ConnectionId=\"26\" Color=\"#80123456\" Opacity=\"0.5\"/></ProgressRing.Foreground></ProgressRing><ProgressRing x:Name=\"themeDefault\"/></Grid></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/progress.xaml"));
        var ring = page.FindName("ring") as ProgressRing;
        Check(ring != null && ReferenceEquals(ring, page.Targets[25]) && ring.IsActive, "typed ProgressRing and active property");
        Check(ReferenceEquals(page.FindName("paint"), page.Targets[26]), "foreground brush name and callback identity");
        ring.IsActive = false; Check(!ring.IsActive, "managed ProgressRing false round trip");
        ring.IsActive = true;
        WrongThread(() => ring.IsActive = false);
        Check(ValidateProgressData(0) == 0, "native progress and solid foreground ABI");
        Bad(page, "<Page" + Ns + " Width=\"999\"><ProgressRing IsActive=\"Maybe\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><ProgressRing IsActive=\"{Binding Active}\"/></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><ProgressRing Foreground=\"{StaticResource Brush}\"/></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><ProgressRing Foreground=\"UnknownColor\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><ProgressRing><ProgressRing.Foreground><Grid/></ProgressRing.Foreground></ProgressRing></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><ProgressRing Foreground=\"Red\"><ProgressRing.Foreground><SolidColorBrush/></ProgressRing.Foreground></ProgressRing></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><ProgressRing><ProgressRing.Foreground/><ProgressRing.Foreground><SolidColorBrush/></ProgressRing.Foreground></ProgressRing></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><ProgressRing Foreground=\"sc#1,0,0,0\"/></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Background><SolidColorBrush><Grid/></SolidColorBrush></Grid.Background></Grid></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><ProgressRing><Grid/></ProgressRing></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><Grid><SolidColorBrush/></Grid></Page>", unchecked((int)0x80070057));
        File.WriteAllText("Fixtures/progress-empty.xaml", "<Page" + Ns + " x:Name=\"root\"><Grid/></Page>");
        page.Targets.Remove(25);
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/progress-empty.xaml"));
        Check(ValidateProgressData(1) == 0, "foreground survives ring and tree replacement");
        Console.WriteLine("PASS: ProgressRing active state and solid foreground data preserve identity, colors and ownership");
    }
    static void VerifyImage(SourcePage page)
    {
        File.WriteAllBytes("Fixtures/É pixels.png", Convert.FromBase64String("iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAYAAAD0In+KAAAAD0lEQVR4nGP4z8DwHwgbABB5A359Y87XAAAAAElFTkSuQmCC"));
        File.WriteAllText("Fixtures/image.xaml", "<Page" + Ns + " x:Name=\"root\"><Grid x:Name=\"leaf\"><Image x:Name=\"picture\" x:ConnectionId=\"24\" Source=\"ms-appx:///Fixtures/%C3%89%20pixels.png\" Width=\"620\" Height=\"300\" VerticalAlignment=\"Center\" HorizontalAlignment=\"Right\" Margin=\"1,2,3,4\"/><Image x:Name=\"emptyImage\" Stretch=\"None\" Margin=\"3,5\"/><Image Margin=\"3\"/></Grid></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/image.xaml"));
        Check(page.FindName("picture") is Image && ReferenceEquals(page.FindName("picture"), page.Targets[24]), "typed Image and callback identity");
        Check(ValidateImageData(0) == 0, "SDK Image data and decoded premultiplied pixels");
        Bad(page, "<Page" + Ns + " Width=\"999\"><Image Source=\"ms-appx:///missing.png\"/></Page>", unchecked((int)0x80070002));
        File.WriteAllText("Fixtures/corrupt.png", "invalid PNG");
        Bad(page, "<Page" + Ns + " Width=\"999\"><Image Source=\"ms-appx:///Fixtures/corrupt.png\"/></Page>", 0);
        Bad(page, "<Page" + Ns + "><Image Source=\"https://example.test/image.png\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Image Source=\"ms-appx:///Fixtures/%2e%2e%5c%2e%2e%5cfile.png\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Image Source=\"{Binding Picture}\"/></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><Image Source=\"ms-appx:///Fixtures/file.png?query=1\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Image Source=\"ms-appx:///Fixtures/file.png#part\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Image Source=\"ms-appx:///Fixtures/%00.png\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Image Stretch=\"Bad\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Image Margin=\"1,2,3\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Image VerticalAlignment=\"Bad\"/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Image><Grid/></Image></Page>", unchecked((int)0x80004001));
        page.Targets.Remove(24);
        File.Delete("Fixtures/É pixels.png");
        File.WriteAllText("Fixtures/image-empty.xaml", "<Page" + Ns + " x:Name=\"root\"><Grid/></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/image-empty.xaml"));
        Check(ValidateImageData(1) == 0, "decoded source survives Image, tree and file replacement");
        Console.WriteLine("PASS: source Images retain decoded package PNG pixels, dimensions and layout data");
    }
    static void VerifyBrush(SourcePage page)
    {
        File.WriteAllText("Fixtures/brush.xaml", "<Page" + Ns + " x:Name=\"root\"><Page.Resources><GradientStop x:Name=\"shortRGB\" Color=\"#aBc\"/><GradientStop x:Name=\"shortARGB\" Color=\"#1234\"/><LinearGradientBrush x:Name=\"resourceBrush\"><LinearGradientBrush.GradientStops><GradientStop Color=\"#F00\"/></LinearGradientBrush.GradientStops></LinearGradientBrush></Page.Resources><Grid x:Name=\"leaf\"><Grid.Background><LinearGradientBrush x:Name=\"brush\" x:ConnectionId=\"23\" StartPoint=\"0.5,0\" EndPoint=\"0.5,1\" Opacity=\"0.75\"><GradientStop Color=\"#ED1C24\" Offset=\"1\"/><GradientStop x:Name=\"stop\" Color=\"#80123456\" Offset=\"-0.25\"/></LinearGradientBrush></Grid.Background><Grid/></Grid></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/brush.xaml"));
        Check(ReferenceEquals(page.FindName("brush"), page.Targets[23]) && page.FindName("stop") != null, "brush names and callback identity");
        Check(ValidateBrushData(0) == 0, "native gradient brush ABI and ownership");
        Bad(page, "<Page" + Ns + " Width=\"999\"><Grid><Grid.Background><Grid/></Grid.Background></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Background/><Grid/></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Background><LinearGradientBrush StartPoint=\"NaN,0\"/></Grid.Background></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Background><LinearGradientBrush><GradientStop Color=\"#12ZZ34\"/></LinearGradientBrush></Grid.Background></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Background><LinearGradientBrush><Grid/></LinearGradientBrush></Grid.Background></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Background><LinearGradientBrush Opacity=\"{Binding Opacity}\"/></Grid.Background></Grid></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Background><LinearGradientBrush EndPoint=\"1e300,0\"/></Grid.Background></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Background><LinearGradientBrush><GradientStop/><LinearGradientBrush.GradientStops/></LinearGradientBrush></Grid.Background></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Background><LinearGradientBrush><LinearGradientBrush.GradientStops/><GradientStop/></LinearGradientBrush></Grid.Background></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Background><LinearGradientBrush/></Grid.Background><Grid.Background><LinearGradientBrush/></Grid.Background></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><LinearGradientBrush/></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><GradientStop/></Grid></Page>", unchecked((int)0x80070057));
        File.WriteAllText("Fixtures/brush-empty.xaml", "<Page" + Ns + " x:Name=\"root\"><Grid><Grid.Background><LinearGradientBrush><LinearGradientBrush.GradientStops/></LinearGradientBrush></Grid.Background></Grid></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/brush-empty.xaml"));
        Check(ValidateBrushData(1) == 0, "retained brush and empty explicit stops");
        Console.WriteLine("PASS: source gradient brushes preserve data, stop order, names and ownership");
    }
    static void VerifyGrid(SourcePage page)
    {
        int connectionStart = page.Connections.Count;
        File.WriteAllText("Fixtures/grid.xaml", "<Page" + Ns + " x:Name=\"root\"><Grid x:Name=\"container\"><Grid x:Name=\"first\" x:ConnectionId=\"21\"><Grid.Children><Grid x:Name=\"leaf\"/></Grid.Children></Grid><Frame x:Name=\"second\" x:ConnectionId=\"22\"><Grid/></Frame></Grid></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/grid.xaml"));
        var container = (Grid)page.Content;
        Panel panel = container;
        Check(ReferenceEquals(container, page.FindName("container")) && ReferenceEquals(panel.FindName("first"), page.Targets[21]), "Grid and Panel inherited namescope identity");
        var first = (Grid)page.FindName("first");
        Check(first is Panel && first is FrameworkElement && first is UIElement && first is DependencyObject, "Grid managed inheritance");
        Check(ReferenceEquals(first.FindName("container"), container) && first.FindName("leaf") is Grid, "typed nested Grid namescope");
        var state = DependencyProperty.RegisterAttached("GridState", typeof(string), typeof(SourcePage), null);
        first.SetValue(state, "native grid state");
        Check((string)((Grid)page.Targets[21]).GetValue(state) == "native grid state", "Grid wrapper shares native dependency values");
        Check(ReferenceEquals(page.FindName("first"), page.Targets[21]) && ReferenceEquals(page.FindName("second"), page.Targets[22]), "Grid child connection identities");
        Check(page.Connections.Count == connectionStart + 2 && page.Connections[connectionStart] == 21 && page.Connections[connectionStart + 1] == 22, "Grid child callback order");
        Check(ValidateGridChildren(0) == 0, "ordered native Grid children and collection ABI");
        Bad(page, "<Page" + Ns + " Width=\"999\"><Grid><Storyboard/></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><Grid/><Grid.Children/></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Children/><Grid/></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Children/><Grid.Children/></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Children Width=\"1\"/></Grid></Page>", unchecked((int)0x80004001));
        Bad(page, "<Page" + Ns + "><Grid><Grid.Background><Grid/></Grid.Background></Grid></Page>", unchecked((int)0x80070057));
        Bad(page, "<Page" + Ns + "><Grid><Grid x:Name=\"same\"/><Grid x:Name=\"same\"/></Grid></Page>", unchecked((int)0x80070057));
        File.WriteAllText("Fixtures/grid-empty.xaml", "<Page" + Ns + " x:Name=\"root\"><Grid><Grid.Children/></Grid></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/grid-empty.xaml"));
        Check(ValidateGridChildren(1) == 0 && page.FindName("first") == null, "Grid replacement preserves retained children and clears names");
        WeakReference root;
        var retained = RetainGridChild(out root);
        for (int i = 0; i < 3; ++i) { GC.Collect(); GC.WaitForPendingFinalizers(); }
        Check(!root.IsAlive, "retained Grid child kept the Page alive");
        Check(retained.FindName("root") == null, "retained typed Grid resolved expired root");
        GC.KeepAlive(retained);
        Console.WriteLine("PASS: typed Grid and Panel preserve native identity, inheritance, names, callbacks and retained collection lifetime");
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
        storyboard.Begin();
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
        Console.WriteLine("PASS: storyboard data loads with typed names, timeline properties and owned children");
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void VerifyClock(SourcePage page)
    {
        File.WriteAllText("Fixtures/clock.xaml", "<Page" + Ns + " x:Name=\"splashRoot\"><Page.Resources>" +
            "<Storyboard x:Name=\"enter\"><FadeInThemeAnimation Storyboard.TargetName=\"splashRoot\" Duration=\"0:0:0.2\"/></Storyboard>" +
            "<Storyboard x:Name=\"inherited\" Storyboard.TargetName=\"leaf\"><FadeOutThemeAnimation Duration=\"0:0:0.05\"/>" +
            "<FadeInThemeAnimation TargetName=\"leaf\" Storyboard.TargetName=\"nobody\" Duration=\"0:0:0.15\"/></Storyboard>" +
            "<Storyboard x:Name=\"automatic\"><FadeOutThemeAnimation Storyboard.TargetName=\"leaf\"/></Storyboard>" +
            "<Storyboard x:Name=\"missing\"><FadeInThemeAnimation Storyboard.TargetName=\"nobody\" Duration=\"0:0:1\"/></Storyboard>" +
            "<Storyboard x:Name=\"untargeted\"><FadeInThemeAnimation Duration=\"0:0:1\"/></Storyboard>" +
            "<Storyboard x:Name=\"nonvisual\"><FadeInThemeAnimation Storyboard.TargetName=\"enter\" Duration=\"0:0:1\"/></Storyboard>" +
            "<Storyboard x:Name=\"clipped\" Duration=\"0:0:0.05\"><FadeInThemeAnimation Storyboard.TargetName=\"leaf\" Duration=\"0:0:10\"/></Storyboard>" +
            "<Storyboard x:Name=\"forever\"><FadeInThemeAnimation Storyboard.TargetName=\"leaf\" Duration=\"Forever\"/></Storyboard>" +
            "<Storyboard x:Name=\"empty\"/>" +
            "</Page.Resources><Grid x:Name=\"leaf\"/></Page>");
        Application.LoadComponent(page, new Uri("ms-appx:///Fixtures/clock.xaml"));
        for (int stage = 0; stage < 5; ++stage)
        {
            int hr = ExerciseAnimationClock(stage);
            Check(hr == 0, "native storyboard clock stage " + stage + " failed at fixture line " + (hr & 0xffff));
        }
        // The game's splash shape: a theme fade targeting its own named Page root.
        var enter = (Storyboard)page.FindName("enter");
        enter.Begin();
        try { ((Storyboard)page.FindName("missing")).Begin(); throw new Exception("unresolved animation target accepted"); }
        catch (Exception error) { if (Marshal.GetHRForException(error) != unchecked((int)0x800F1000)) throw; }
        WrongThread(() => enter.Begin());
        Console.WriteLine("PASS: storyboard clocks resolve theme targets, complete on the UI thread, restart, stop and keep running storyboards alive");
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
        Bad(page, "<Page" + Ns + " Width=\"999\"><RichTextBlock/></Page>", unchecked((int)0x80004001));
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
        VerifyApplicationSource();
        VerifyMergedSources();
        VerifyResources(page);
        VerifyGrid(page);
        VerifyBrush(page);
        VerifyImage(page);
        VerifyProgress(page);
        VerifyText(page);
        VerifyViewbox(page);
        VerifyAnimations(page);
        VerifyClock(page);
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
