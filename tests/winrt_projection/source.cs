using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Markup;
namespace Fixture
{
    class SourcePage : Page, IComponentConnector
    {
        internal readonly List<KeyValuePair<int, object>> Connections = new List<KeyValuePair<int, object>>();
        internal Exception Failure;
        internal bool NamedBeforeConnect = true;
        void IComponentConnector.Connect(int id, object target)
        {
            if (Failure != null) throw Failure;
            if (FindName("title") == null) NamedBeforeConnect = false;
            Connections.Add(new KeyValuePair<int, object>(id, target));
        }
    }
    // A game control with its own dependency property, as generated code registers it.
    // Frame journal pages: MainPage keeps itself with NavigationCacheMode.Required.
    class CachedPage : Page
    {
        internal static int Made;
        public CachedPage() { ++Made; NavigationCacheMode = Windows.UI.Xaml.Navigation.NavigationCacheMode.Required; }
    }
    class PlainPage : Page { internal static int Made; public PlainPage() { ++Made; } }
    class Badge : UserControl
    {
        internal static int Made;
        public static readonly DependencyProperty ScoreProperty = DependencyProperty.Register("Score", typeof(int), typeof(Badge), new PropertyMetadata(0));
        public Badge() { ++Made; }
        public int Score { get { return (int)GetValue(ScoreProperty); } set { SetValue(ScoreProperty, value); } }
    }
    // LayoutAwarePage's DefaultViewModel: an observable string-keyed map.
    class MapChange : EventArgs { public string Key { get; set; } }
    class TestMap : Dictionary<string, object>
    {
        public event EventHandler<MapChange> MapChanged;
        public void Put(string key, object value) { this[key] = value; if (MapChanged != null) MapChanged(this, new MapChange { Key = key }); }
    }
    class ModelPage : SourcePage
    {
        readonly TestMap model = new TestMap();
        public TestMap DefaultViewModel { get { return model; } }
    }
    // A UWP converter: language is a string, target types are UWP's.
    class Fmt : Windows.UI.Xaml.Data.IValueConverter
    {
        internal static Type Target; internal static object Parameter; internal static string Language;
        public object Convert(object value, Type targetType, object parameter, string language)
        {
            Target = targetType; Parameter = parameter; Language = language;
            return value + " " + parameter;
        }
        public object ConvertBack(object value, Type targetType, object parameter, string language) { throw new NotSupportedException(); }
    }
    class Boom : Windows.UI.Xaml.Data.IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, string language) { throw new InvalidOperationException("converter failure"); }
        public object ConvertBack(object value, Type targetType, object parameter, string language) { throw new NotSupportedException(); }
    }
    // An app type as generated XamlTypeInfo describes it.
    class SourceXamlType : IXamlType
    {
        readonly Type type;
        internal int Activations;
        internal SourceXamlType(Type type) { this.type = type; }
        public IXamlType BaseType { get { return null; } }
        public IXamlMember ContentProperty { get { return null; } }
        public string FullName { get { return type.FullName; } }
        public bool IsArray { get { return false; } }
        public bool IsCollection { get { return false; } }
        public bool IsConstructible { get { return true; } }
        public bool IsDictionary { get { return false; } }
        public bool IsMarkupExtension { get { return false; } }
        public bool IsBindable { get { return false; } }
        public IXamlType ItemType { get { return null; } }
        public IXamlType KeyType { get { return null; } }
        public Type UnderlyingType { get { return type; } }
        public object ActivateInstance() { ++Activations; return Activator.CreateInstance(type, true); }
        public object CreateFromString(string value) { throw new NotSupportedException(); }
        public IXamlMember GetMember(string member) { return null; }
        public void AddToVector(object instance, object value) { throw new NotSupportedException(); }
        public void AddToMap(object instance, object key, object value) { throw new NotSupportedException(); }
        public void RunInitializer() { }
    }
    class SourceApp : Application, IXamlMetadataProvider
    {
        internal readonly Dictionary<string, SourceXamlType> Types = new Dictionary<string, SourceXamlType>();
        public SourceApp()
        {
            foreach (var type in new[] { typeof(SourcePage), typeof(Badge), typeof(ModelPage), typeof(CachedPage), typeof(PlainPage) }) Types.Add(type.FullName, new SourceXamlType(type));
        }
        public IXamlType GetXamlType(Type type) { return type == null ? null : GetXamlType(type.FullName); }
        public IXamlType GetXamlType(string fullName)
        {
            SourceXamlType value;
            return Types.TryGetValue(fullName, out value) ? value : null;
        }
        public XmlnsDefinition[] GetXmlnsDefinitions() { return new XmlnsDefinition[0]; }

        const string Ns = " xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'";
        const string PageXaml = "<local:SourcePage x:Class='Fixture.SourcePage' x:Name='self'" + Ns + " xmlns:local='using:Fixture'" +
            " xmlns:d='http://schemas.microsoft.com/expression/blend/2008' xmlns:mc='http://schemas.openxmlformats.org/markup-compatibility/2006' mc:Ignorable='d' d:DesignWidth='1366'>" +
            "<Grid x:Name='layout'><TextBlock x:ConnectionId='1' x:Name='title' Text='Ω' Grid.Row='1'/>" +
            "<local:Badge x:ConnectionId='2' x:Name='badge' Score='5' Visibility='Collapsed'/></Grid></local:SourcePage>";

        static void Check(bool value, string message) { if (!value) throw new Exception(message); }
        static bool ShimLogContains(string text) { return ShimLogOccurrences(text) != 0; }
        static int ShimLogOccurrences(string text)
        {
            if (!File.Exists("appx-portable.log")) return 0;
            using (var stream = new FileStream("appx-portable.log", FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
            using (var reader = new StreamReader(stream)) return reader.ReadToEnd().Split(new[] { text }, StringSplitOptions.None).Length - 1;
        }
        static string Fails(string uri, object component)
        {
            try { Application.LoadComponent(component, new Uri(uri)); }
            catch (Exception error) { return error.GetType().Name + ": " + error.Message; }
            throw new Exception("accepted " + uri);
        }

        void Verify()
        {
            // Windows loads the package's App.xaml itself; apps' InitializeComponent need not.
            Directory.CreateDirectory("Common");
            File.WriteAllText("Common/Styles.xaml", StylesXaml);
            File.WriteAllText("App.xaml", AppXaml);
            Directory.CreateDirectory("Pages");
            File.WriteAllText("Pages/early.xaml", "<Page" + Ns + "><TextBlock x:Name='early' Style='{StaticResource Big}'/></Page>");
            var early = new SourcePage();
            Application.LoadComponent(early, new Uri("ms-appx:///Pages/early.xaml"));
            Check(((TextBlock)early.FindName("early")).FontSize == 42 && Resources.ContainsKey("Big"), "the conventional App.xaml loads before the first page");
            File.WriteAllText("Pages/My Page.xaml", PageXaml);
            var page = new SourcePage();
            Application.LoadComponent(page, new Uri("ms-appx:///Pages/My%20Page.xaml"));
            var title = page.FindName("title") as TextBlock;
            Check(title != null && title.Text == "Ω" && Grid.GetRow(title) == 1, "named TextBlock with properties and attached row");
            Check(page.FindName("layout") is Grid && ReferenceEquals(page.Content, page.FindName("layout")), "root content is the named Grid");
            Check(ReferenceEquals(page.FindName("self"), page), "root name is the loading component");
            var badge = page.FindName("badge") as Badge;
            Check(badge != null && Badge.Made == 1 && Types["Fixture.Badge"].Activations == 1, "using: types are created once through the app's XAML metadata");
            Check(badge.Score == 5 && badge.Visibility == Visibility.Collapsed, "game and system properties set from markup");
            Check(page.Connections.Count == 2 && page.Connections[0].Key == 1 && ReferenceEquals(page.Connections[0].Value, title) &&
                  page.Connections[1].Key == 2 && ReferenceEquals(page.Connections[1].Value, badge), "x:ConnectionId connects wrappers in document order");
            Check(page.NamedBeforeConnect, "names are registered before Connect");

            var again = new SourcePage();
            Application.LoadComponent(again, new Uri("ms-appx:///pages/MY%20page.xaml"));
            Check(again.FindName("title") is TextBlock && !ReferenceEquals(again.FindName("title"), title), "case-insensitive package path, separate tree");

            string missing = Fails("ms-appx:///Pages/Missing.xaml", new SourcePage());
            Check(missing.StartsWith("FileNotFoundException") && missing.Contains("ms-appx:///Pages/Missing.xaml"), "missing XAML names its URI: " + missing);
            File.WriteAllText("Pages/member.xaml", "<Page" + Ns + "><Grid Foo='1'/></Page>");
            string member = Fails("ms-appx:///Pages/member.xaml", new SourcePage());
            Check(member.Contains("Grid.Foo") && member.Contains("member.xaml"), "unknown member is named with its file: " + member);
            File.WriteAllText("Pages/type.xaml", "<Page" + Ns + "><Unknown/></Page>");
            string type = Fails("ms-appx:///Pages/type.xaml", new SourcePage());
            Check(type.Contains("Unknown") && type.Contains("type.xaml"), "unknown type is named with its file: " + type);

            var failing = new SourcePage { Failure = new InvalidOperationException("source callback failure") };
            try { Application.LoadComponent(failing, new Uri("ms-appx:///Pages/My%20Page.xaml")); throw new Exception("callback error accepted"); }
            catch (InvalidOperationException error) { Check(ReferenceEquals(error, failing.Failure), "connector exception identity"); }
            Check(ShimLogContains("managed callback threw System.InvalidOperationException: source callback failure"), "connector failure logged");

            // UWP keys a resource with only x:Name by that name, and the name is findable.
            File.WriteAllText("Pages/named.xaml", "<Page" + Ns + "><Page.Resources><Storyboard x:Name='intro'/><Storyboard x:Key='k' x:Name='both'/></Page.Resources></Page>");
            var named = new SourcePage();
            Application.LoadComponent(named, new Uri("ms-appx:///Pages/named.xaml"));
            Check(named.Resources.ContainsKey("intro") && named.FindName("intro") is Windows.UI.Xaml.Media.Animation.Storyboard &&
                  ReferenceEquals(named.Resources["intro"], named.FindName("intro")) && named.Resources.ContainsKey("k") && !named.Resources.ContainsKey("both"),
                  "x:Name keys a resource that has no x:Key");

            // A mapped member named differently in WPF keeps its UWP name in markup.
            File.WriteAllText("Pages/ring.xaml", "<Page" + Ns + "><ProgressRing x:Name='ring' IsActive='True'/></Page>");
            var ringPage = new SourcePage();
            Application.LoadComponent(ringPage, new Uri("ms-appx:///Pages/ring.xaml"));
            var ring = ringPage.FindName("ring") as ProgressRing;
            Check(ring != null && ring.IsActive, "UWP member names of mapped peers resolve in markup");

            // RichTextBlock's typographic styles use WPF's text rendering.
            File.WriteAllText("Pages/rich.xaml", "<Page" + Ns + "><Page.Resources><FontWeight x:Key='weight'>SemiLight</FontWeight><Style x:Key='rich' TargetType='RichTextBlock'>" +
                "<Setter Property='FontSize' Value='20'/><Setter Property='FontWeight' Value='SemiLight'/><Setter Property='TextWrapping' Value='Wrap'/><Setter Property='TextTrimming' Value='WordEllipsis'/>" +
                "</Style></Page.Resources><StackPanel><RichTextBlock x:Name='rich' Style='{StaticResource rich}'/><TextBlock x:Name='weight' FontWeight='SemiLight' Text='SemiLight'/></StackPanel></Page>");
            var richPage = new SourcePage();
            Application.LoadComponent(richPage, new Uri("ms-appx:///Pages/rich.xaml"));
            var rich = (System.Windows.Controls.TextBlock)PeerOf(richPage.FindName("rich"));
            Check(rich.FontSize == 20 && rich.TextWrapping == System.Windows.TextWrapping.Wrap && rich.TextTrimming == System.Windows.TextTrimming.WordEllipsis,
                  "RichTextBlock styles reach WPF text properties");

            Check(((System.Windows.Controls.TextBlock)PeerOf(richPage.FindName("weight"))).Text == "SemiLight" && rich.FontWeight.ToOpenTypeWeight() == 350 && ((System.Windows.Controls.TextBlock)PeerOf(richPage.FindName("weight"))).FontWeight.ToOpenTypeWeight() == 350 &&
                  ((System.Windows.FontWeight)((System.Windows.FrameworkElement)PeerOf(richPage)).Resources["weight"]).ToOpenTypeWeight() == 350, "SemiLight typography maps to OpenType weight 350");

            File.WriteAllText("Pages/replacement.xaml", "<Page" + Ns + " x:Name='replacement'/>");
            Application.LoadComponent(page, new Uri("ms-appx:///Pages/replacement.xaml"));
            Check(page.Content == null && page.FindName("title") == null && ReferenceEquals(page.FindName("replacement"), page), "reload replaces tree and names");
            Console.WriteLine("PASS: managed loader builds WPF content with names, connections and game types; unsupported input fails explicitly");
            VerifyCompatibility();
            VerifyStartupSurface();
            VerifyClicks();
            VerifyInputEvents();
            VerifyStyles();
        }

        // Independent markup gaps are collected together, rather than hiding behind
        // the first failing resource in the game's large shared dictionary.
        // The code-side XAML members Bejeweled's MainPage, SettingsCharmBar,
        // SwapChainBGPanel and LayoutAwarePage use on the way to the hub.
        void VerifyStartupSurface()
        {
            var failures = new List<string>();
            Action<string, Action> test = (name, verify) => { try { verify(); } catch (Exception error) { failures.Add(name + ": " + error); } };
            test("transitions", () =>
            {
                var element = new Grid();
                var entrance = new Windows.UI.Xaml.Media.Animation.EntranceThemeTransition();
                entrance.FromHorizontalOffset = -100;
                var transitions = new Windows.UI.Xaml.Media.Animation.TransitionCollection();
                transitions.Add(entrance);
                element.Transitions = transitions;
                Check(ReferenceEquals(element.Transitions, transitions) && element.Transitions.Count == 1 &&
                      ((Windows.UI.Xaml.Media.Animation.EntranceThemeTransition)element.Transitions[0]).FromHorizontalOffset == -100, "element transitions keep identity and offset");
            });
            test("scroll-layout", () =>
            {
                File.WriteAllText("Pages/startup-scroll.xaml", "<Page" + Ns + "><ScrollViewer x:Name='scroll' Width='100' Height='50' HorizontalScrollBarVisibility='Hidden' VerticalScrollBarVisibility='Disabled' HorizontalScrollMode='Enabled'>" +
                    "<Grid Width='300' Height='20'/></ScrollViewer></Page>");
                var page = new SourcePage(); Application.LoadComponent(page, new Uri("ms-appx:///Pages/startup-scroll.xaml"));
                var scroll = (ScrollViewer)page.FindName("scroll");
                var root = (System.Windows.UIElement)PeerOf(page);
                page.Measure(new Windows.Foundation.Size(400, 400));
                Check(page.DesiredSize.Width == 100 && page.DesiredSize.Height == 50, "Measure reaches WPF and DesiredSize returns its size");
                root.Arrange(new System.Windows.Rect(0, 0, 400, 400));
                page.UpdateLayout();
                var wpfScroll = (System.Windows.Controls.ScrollViewer)PeerOf(scroll);
                Check(scroll.ScrollableWidth == 200, "ScrollableWidth comes from WPF layout: " + scroll.ScrollableWidth + " extent " + wpfScroll.ExtentWidth + " viewport " + wpfScroll.ViewportWidth +
                      " template " + (wpfScroll.Template != null) + " actual " + wpfScroll.ActualWidth);
                scroll.ScrollToHorizontalOffset(50);
                page.UpdateLayout();
                Check(scroll.HorizontalOffset == 50, "ScrollToHorizontalOffset applies on the next layout");
            });
            test("size-and-keys", () =>
            {
                var grid = new Grid();
                var sizes = new List<Windows.Foundation.Size>();
                object sender = null;
                var token = grid.add_SizeChanged((s, e) => { sender = s; sizes.Add(e.NewSize); });
                grid.Width = 30; grid.Height = 40;
                grid.Measure(new Windows.Foundation.Size(100, 100));
                ((System.Windows.UIElement)PeerOf(grid)).Arrange(new System.Windows.Rect(0, 0, 30, 40));
                grid.UpdateLayout();
                Check(sizes.Count == 1 && sizes[0].Width == 30 && sizes[0].Height == 40 && ReferenceEquals(sender, grid), "SizeChanged reports the new size with the wrapper as sender: " + sizes.Count);
                grid.remove_SizeChanged(token);
                grid.Width = 31;
                grid.Measure(new Windows.Foundation.Size(100, 100));
                ((System.Windows.UIElement)PeerOf(grid)).Arrange(new System.Windows.Rect(0, 0, 31, 40));
                grid.UpdateLayout();
                Check(sizes.Count == 1, "removed SizeChanged handler was called");
                var keys = new List<Windows.System.VirtualKey>();
                var key = grid.add_KeyDown((s, e) => { keys.Add(e.Key); e.Handled = true; });
                var source = new System.Windows.Interop.HwndSource(new System.Windows.Interop.HwndSourceParameters("startup-keys")) { RootVisual = (System.Windows.Media.Visual)PeerOf(grid) };
                var args = new System.Windows.Input.KeyEventArgs(System.Windows.Input.Keyboard.PrimaryDevice, source, 0, System.Windows.Input.Key.Escape);
                args.RoutedEvent = System.Windows.Input.Keyboard.KeyDownEvent;
                ((System.Windows.UIElement)PeerOf(grid)).RaiseEvent(args);
                Check(keys.Count == 1 && keys[0] == Windows.System.VirtualKey.Escape && args.Handled, "KeyDown sees the key and handling");
                grid.remove_KeyDown(key);
                source.RootVisual = null; source.Dispose();
            });
            test("storyboard-code", () =>
            {
                var target = new Grid();
                var storyboard = new Windows.UI.Xaml.Media.Animation.Storyboard();
                var fade = new Windows.UI.Xaml.Media.Animation.DoubleAnimation();
                fade.From = 0; fade.To = 1;
                fade.Duration = TimeSpan.FromMilliseconds(250);
                Check(fade.From == 0 && fade.To == 1 && fade.Duration.HasTimeSpan && fade.Duration.TimeSpan == TimeSpan.FromMilliseconds(250), "DoubleAnimation From, To and Duration round trip");
                Windows.UI.Xaml.Media.Animation.Storyboard.SetTargetProperty(fade, "Opacity");
                Windows.UI.Xaml.Media.Animation.Storyboard.SetTarget(fade, target);
                Check(!storyboard.Children.Contains(fade), "new storyboard is empty");
                storyboard.Children.Add(fade);
                Check(storyboard.Children.Count == 1 && storyboard.Children.Contains(fade) && ReferenceEquals(storyboard.Children[0], fade), "Children keeps the wrapper");
                var peer = (System.Windows.Media.Animation.DoubleAnimation)PeerOf(fade);
                Check(System.Windows.Media.Animation.Storyboard.GetTarget(peer) == PeerOf(target) &&
                      System.Windows.Media.Animation.Storyboard.GetTargetProperty(peer).Path == "Opacity", "targets reach WPF");
                Windows.UI.Xaml.Media.Animation.Storyboard.SetTargetProperty(fade, "(UIElement.Opacity)");
                var path = System.Windows.Media.Animation.Storyboard.GetTargetProperty(peer);
                Check(path.PathParameters.Count == 1 && path.PathParameters[0] == System.Windows.UIElement.OpacityProperty, "qualified UWP paths resolve to WPF properties");
                Windows.UI.Xaml.Media.Animation.Storyboard.SetTargetProperty(fade, "(UIElement.RenderTransform).(TranslateTransform.X)");
                path = System.Windows.Media.Animation.Storyboard.GetTargetProperty(peer);
                Check(path.PathParameters.Count == 2 && path.PathParameters[1] == System.Windows.Media.TranslateTransform.XProperty, "nested UWP paths resolve each owner");
                var token = fade.add_Completed((s, e) => { });
                fade.remove_Completed(token);
                storyboard.Children.Clear();
                Check(storyboard.Children.Count == 0, "Children.Clear");
            });
            test("frame-journal", () =>
            {
                CachedPage.Made = 0; PlainPage.Made = 0;
                var frame = new Frame();
                Check(!frame.CanGoBack && !frame.CanGoForward && frame.BackStackDepth == 0, "new frame has no journal");
                frame.Navigate(typeof(CachedPage));
                var cached = frame.Content;
                frame.Navigate(typeof(PlainPage));
                Check(frame.BackStackDepth == 1 && frame.CanGoBack && !frame.CanGoForward, "Navigate records the previous page");
                frame.GoBack();
                Check(ReferenceEquals(frame.Content, cached) && CachedPage.Made == 1 && frame.CanGoForward && frame.BackStackDepth == 0, "GoBack returns to the cached page instance");
                frame.GoForward();
                Check(frame.Content is PlainPage && PlainPage.Made == 2 && frame.BackStackDepth == 1, "GoForward recreates an uncached page");
                frame.Navigate(typeof(PlainPage));
                Check(!frame.CanGoForward && frame.BackStackDepth == 2, "Navigate clears the forward stack");
                try { new Frame().GoBack(); throw new Exception("GoBack without history accepted"); }
                catch (COMException error) { Check(error.ErrorCode == unchecked((int)0x80004005), "GoBack without history fails with E_FAIL"); }
            });
            test("colors-brush-canvas", () =>
            {
                var gold = Windows.UI.Colors.Goldenrod;
                Check(gold.A == 255 && gold.R == 218 && gold.G == 165 && gold.B == 32, "Colors.Goldenrod");
                var gray = new Windows.UI.Xaml.Media.SolidColorBrush(Windows.UI.Colors.Gray);
                Check(gray.Color.R == 128 && ((System.Windows.Media.SolidColorBrush)PeerOf(gray)).Color == System.Windows.Media.Colors.Gray, "SolidColorBrush(Color)");
                var child = new Grid();
                Canvas.SetLeft(child, 12);
                Check(Canvas.GetLeft(child) == 12 && System.Windows.Controls.Canvas.GetLeft((System.Windows.UIElement)PeerOf(child)) == 12 &&
                      (double)child.GetValue(Canvas.LeftProperty) == 12, "Canvas.Left maps onto WPF");
                var box = new Viewbox();
                box.Child = child;
                Check(ReferenceEquals(box.Child, child), "Viewbox.Child");
                var move = new Windows.UI.Xaml.Media.TranslateTransform();
                move.X = 5; move.Y = 6;
                child.RenderTransform = move;
                var applied = ((System.Windows.UIElement)PeerOf(child)).RenderTransform.Transform(new System.Windows.Point(1, 1));
                Check(ReferenceEquals(child.RenderTransform, move) && applied.X == 6 && applied.Y == 7, "RenderTransform and TranslateTransform");
            });
            test("popup-overlay", () =>
            {
                // MainPage_Loaded opens a code-created popup at window offsets.
                var popup = new Windows.UI.Xaml.Controls.Primitives.Popup();
                var child = new Grid();
                popup.Child = child;
                popup.HorizontalOffset = 40; popup.VerticalOffset = 25;
                int closed = 0;
                var token = popup.add_Closed((sender, args) => { Check(ReferenceEquals(sender, popup), "Closed sender"); ++closed; });
                var visual = (System.Windows.UIElement)PeerOf(child);
                Check(System.Windows.Media.VisualTreeHelper.GetParent(visual) == null, "a closed popup shows nothing");
                popup.IsOpen = true;
                var overlay = System.Windows.Media.VisualTreeHelper.GetParent(visual) as System.Windows.Controls.Canvas;
                Check(overlay != null && System.Windows.Controls.Canvas.GetLeft(visual) == 40 && System.Windows.Controls.Canvas.GetTop(visual) == 25,
                      "an open popup's child sits in the window overlay at its offsets");
                Check(System.Windows.Media.VisualTreeHelper.GetParent(overlay) is System.Windows.Controls.Grid, "the overlay is a layer of the window root");
                popup.HorizontalOffset = 50;
                Check(System.Windows.Controls.Canvas.GetLeft(visual) == 50, "moving an open popup");
                popup.IsOpen = false;
                Check(System.Windows.Media.VisualTreeHelper.GetParent(visual) == null && closed == 1, "closing removes the child and raises Closed");
                // Light dismiss: a press on the overlay outside the child closes it.
                popup.IsLightDismissEnabled = true;
                popup.IsOpen = true;
                Check(overlay.Background != null, "light dismiss makes the overlay take presses");
                var press = new System.Windows.Input.MouseButtonEventArgs(System.Windows.Input.Mouse.PrimaryDevice, 0, System.Windows.Input.MouseButton.Left);
                press.RoutedEvent = System.Windows.UIElement.MouseDownEvent;
                overlay.RaiseEvent(press);
                Check(!popup.IsOpen && closed == 2 && overlay.Background == null, "a press outside the child dismisses it");
                popup.remove_Closed(token);
            });
            Check(failures.Count == 0, "startup surface gaps:\n" + string.Join("\n", failures));
            Console.WriteLine("PASS: startup layout, events, storyboards, journal and color members map onto WPF");
        }

        void VerifyCompatibility()
        {
            var failures = new List<string>();
            Action<string, string, Action<SourcePage>> test = (name, markup, verify) =>
            {
                try
                {
                    string file = "Pages/compat-" + name + ".xaml";
                    File.WriteAllText(file, "<Page" + Ns + ">" + markup + "</Page>");
                    var page = new SourcePage(); Application.LoadComponent(page, new Uri("ms-appx:///" + file)); verify(page);
                }
                catch (Exception error) { failures.Add(name + ": " + error); }
            };
            test("transitions", "<Page.Resources><Style x:Key='enter' TargetType='Grid'><Setter Property='ChildrenTransitions'><Setter.Value>" +
                "<TransitionCollection><EntranceThemeTransition IsStaggeringEnabled='False'/><EntranceThemeTransition/><ContentThemeTransition/><AddDeleteThemeTransition/><ReorderThemeTransition/>" +
                "</TransitionCollection></Setter.Value></Setter></Style></Page.Resources><Grid Style='{StaticResource enter}'/>", page =>
            {
                Check(ShimLogOccurrences("UNIMPL managed Windows.UI.Xaml.Media.Animation.EntranceThemeTransition") == 1, "unsupported transition is logged once");
                var property = typeof(Panel).GetProperty("ChildrenTransitions");
                var collection = property.GetValue(page.Content, null);
                var count = collection.GetType().GetProperty("Count");
                Check((int)count.GetValue(collection, null) == 5 && ReferenceEquals(collection, property.GetValue(page.Content, null)), "transition collection contents and identity");
                Exception wrong = null;
                var thread = new System.Threading.Thread(() => { try { count.GetValue(collection, null); } catch (System.Reflection.TargetInvocationException error) { wrong = error.InnerException; } });
                thread.Start(); thread.Join();
                Check(wrong != null && Marshal.GetHRForException(wrong) == unchecked((int)0x8001010e), "transition collection thread affinity");
            });
            test("reposition", "<Grid><Grid.Resources><Storyboard x:Key='move'><RepositionThemeAnimation FromHorizontalOffset='-3' TargetName='target'/></Storyboard>" +
                "</Grid.Resources><TextBlock x:Name='target'/></Grid>", page => Check(ShimLogContains("UNIMPL managed Windows.UI.Xaml.Media.Animation.RepositionThemeAnimation"), "unsupported theme animation is logged"));
            test("composite", "<Grid x:Name='grid'><Grid.RenderTransform><CompositeTransform ScaleX='2' ScaleY='3' Rotation='90' TranslateX='10' TranslateY='20'/></Grid.RenderTransform></Grid>", page =>
            {
                var transform = ((System.Windows.Controls.Grid)PeerOf(page.FindName("grid"))).RenderTransform;
                var point = transform.Transform(new System.Windows.Point(1, 2));
                Check(Math.Abs(point.X - 4) < 0.0001 && Math.Abs(point.Y - 22) < 0.0001, "composite scale, rotation and translation reach WPF");
            });
            test("toggle", "<ToggleSwitch x:Name='toggle' IsOn='True' Header='Sounds' FontSize='24'/>", page =>
            {
                var toggle = (System.Windows.Controls.CheckBox)PeerOf(page.FindName("toggle"));
                Check(toggle.IsChecked == true && Equals(toggle.Content, "Sounds") && toggle.FontSize == 24, "toggle maps checked state, header and typography");
            });
            test("wrap", "<WrapGrid x:Name='wrap' Orientation='Vertical'/>", page =>
                Check(((System.Windows.Controls.WrapPanel)PeerOf(page.FindName("wrap"))).Orientation == System.Windows.Controls.Orientation.Vertical, "wrap orientation maps onto WPF layout"));
            test("scroll", "<Page.Resources><Style x:Key='scroll' TargetType='ScrollViewer'><Setter Property='ScrollViewer.HorizontalScrollMode' Value='Enabled'/>" +
                "<Setter Property='ScrollViewer.VerticalScrollMode' Value='Disabled'/><Setter Property='ScrollViewer.ZoomMode' Value='Disabled'/><Setter Property='TabNavigation' Value='Once'/>" +
                "</Style></Page.Resources><ScrollViewer x:Name='scroll' Style='{StaticResource scroll}'/>", page =>
            {
                var scroll = (System.Windows.Controls.ScrollViewer)PeerOf(page.FindName("scroll"));
                Check(scroll.VerticalScrollBarVisibility == System.Windows.Controls.ScrollBarVisibility.Disabled &&
                    scroll.HorizontalScrollBarVisibility != System.Windows.Controls.ScrollBarVisibility.Disabled &&
                    System.Windows.Input.KeyboardNavigation.GetTabNavigation(scroll) == System.Windows.Input.KeyboardNavigationMode.Once, "scroll modes and keyboard navigation use WPF");
            });
            test("list", "<Page.Resources><Style x:Key='list' TargetType='ListView'><Setter Property='IsSwipeEnabled' Value='False'/><Setter Property='TabNavigation' Value='Once'/>" +
                "<Setter Property='ItemContainerTransitions'><Setter.Value><TransitionCollection/></Setter.Value></Setter></Style></Page.Resources><ListView x:Name='list' Style='{StaticResource list}' ScrollViewer.HorizontalScrollMode='Disabled'/>", page =>
                Check(PeerOf(page.FindName("list")) is System.Windows.Controls.ListView, "list styles load onto WPF"));
            test("fade", "<Grid><Grid.Resources><Storyboard x:Key='fade'><FadeInThemeAnimation TargetName='target'/><FadeOutThemeAnimation TargetName='target'/></Storyboard></Grid.Resources>" +
                "<TextBlock x:Name='target'/></Grid>", page => { });
            Directory.CreateDirectory("Assets");
            var image = new System.Windows.Media.Imaging.PngBitmapEncoder();
            image.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(System.Windows.Media.Imaging.BitmapSource.Create(1, 1, 96, 96,
                System.Windows.Media.PixelFormats.Bgra32, null, new byte[] { 0, 0, 255, 255 }, 4)));
            using (var stream = File.Create("Assets/pixel.png")) image.Save(stream);
            test("images", "<Page.Resources><ImageBrush x:Key='brush' ImageSource='/Assets/pixel.png'/></Page.Resources>" +
                "<Image x:Name='image' Source='ms-appx:///Assets/pixel.png'/>", page =>
            {
                var source = ((System.Windows.Controls.Image)PeerOf(page.FindName("image"))).Source as System.Windows.Media.Imaging.BitmapSource;
                Check(source != null && source.PixelWidth == 1, "package image URI resolves to WPF pixels");
                var bitmap = new Windows.UI.Xaml.Media.Imaging.BitmapImage(new Uri("ms-appx:///Assets/pixel.png"));
                var wrapper = (Image)page.FindName("image"); wrapper.Source = bitmap;
                Check(ReferenceEquals(wrapper.Source, bitmap) && ((System.Windows.Media.Imaging.BitmapSource)((System.Windows.Controls.Image)PeerOf(wrapper)).Source).PixelWidth == 1,
                      "code-created package bitmap reaches Image.Source with wrapper identity");
            });
            test("missing-bitmap", "<Page.Resources><BitmapImage x:Key='missing' UriSource='/Assets/missing.png'/></Page.Resources>", page => { });
            test("presenter-content", "<ContentPresenter x:Name='presenter'><Grid/></ContentPresenter>", page =>
                Check(((System.Windows.Controls.ContentPresenter)PeerOf(page.FindName("presenter"))).Content is System.Windows.Controls.Grid, "UWP ContentPresenter has implicit Content"));
            test("presenter-foreground", "<ContentPresenter x:Name='presenter' Foreground='Red' Content='text'/>", page =>
                Check(((System.Windows.Media.SolidColorBrush)System.Windows.Documents.TextElement.GetForeground((System.Windows.DependencyObject)PeerOf(page.FindName("presenter")))).Color == System.Windows.Media.Colors.Red,
                      "presenter foreground uses WPF inherited text foreground"));
            test("service-template", "<Page.Resources><ControlTemplate x:Key='media' TargetType='Frame'><MediaElement AutoPlay='True' AudioCategory='BackgroundCapableMedia'/></ControlTemplate>" +
                "<ControlTemplate x:Key='items' TargetType='ListView'><ItemsPresenter Header='header' HeaderTemplate='{x:Null}' Padding='0'/></ControlTemplate></Page.Resources>", page => { });
            test("selection-disabled", "<RichTextBlock IsTextSelectionEnabled='False'/>", page => { });
            foreach (var unsupported in new[] {
                new[] { "<ScrollViewer ZoomMode='Enabled'/>", "ScrollViewer.ZoomMode" },
                new[] { "<ListView IsSwipeEnabled='True'/>", "ListViewBase.IsSwipeEnabled" },
                new[] { "<RichTextBlock IsTextSelectionEnabled='True'/>", "RichTextBlock.IsTextSelectionEnabled" },
                new[] { "<ItemsPresenter Header='header'/>", "ItemsPresenter.Header" } })
            {
                File.WriteAllText("Pages/unimplemented.xaml", "<Page" + Ns + ">" + unsupported[0] + "</Page>");
                string failure = Fails("ms-appx:///Pages/unimplemented.xaml", new SourcePage());
                Check(failure.Contains(unsupported[1]), "unsupported behavior remains named: " + failure);
            }
            // RootFrameStyle's background music: markup values reach the media peer.
            test("media-markup", "<MediaElement x:Name='music' AutoPlay='False' IsLooping='True' Volume='0.5' AudioCategory='BackgroundCapableMedia'/>", page =>
            {
                var music = (Windows.UI.Xaml.Controls.MediaElement)page.FindName("music");
                Check(!music.AutoPlay && music.IsLooping && music.Volume == 0.5 && music.AudioCategory == Windows.UI.Xaml.Media.AudioCategory.BackgroundCapableMedia,
                      "media markup values");
            });
            test("wrapper-types", "<Page.Resources><ImageBrush x:Name='ImageBrush'/><BitmapImage x:Name='BitmapImage' UriSource='/Assets/pixel.png'/>" +
                "<VisualState x:Name='VisualState'/><VisualStateGroup x:Name='VisualStateGroup'/></Page.Resources><StackPanel x:Name='StackPanel'>" +
                "<Button x:Name='Button' x:ConnectionId='1'/><RadioButton x:Name='RadioButton' x:ConnectionId='2'/><Slider x:Name='Slider' x:ConnectionId='3'/>" +
                "<Border x:Name='Border'/><ItemsControl x:Name='ItemsControl'/><ListViewItem x:Name='ListViewItem'/>" +
                "<Ellipse x:Name='Ellipse'/><Rectangle x:Name='Rectangle'/></StackPanel>", page =>
            {
                foreach (string type in new[] { "Button", "RadioButton", "Slider", "Border", "StackPanel", "ItemsControl", "ListViewItem", "ImageBrush", "BitmapImage", "VisualState", "VisualStateGroup", "Ellipse", "Rectangle" })
                {
                    string ns = type == "ImageBrush" ? "Media" : type == "BitmapImage" ? "Media.Imaging" :
                        type == "Ellipse" || type == "Rectangle" ? "Shapes" : type == "VisualState" || type == "VisualStateGroup" ? "" : "Controls";
                    string expected = "Windows.UI.Xaml." + (ns == "" ? "" : ns + ".") + type;
                    var value = page.FindName(type);
                    Check(value != null && value.GetType().FullName == expected && ReferenceEquals(value, page.FindName(type)), "typed wrapper identity for " + type + ": " + value);
                }
                Check(page.Connections[0].Value.GetType().FullName == "Windows.UI.Xaml.Controls.Button" &&
                      page.Connections[1].Value.GetType().FullName == "Windows.UI.Xaml.Controls.RadioButton" &&
                      page.Connections[2].Value.GetType().FullName == "Windows.UI.Xaml.Controls.Slider", "Connect receives typed control wrappers");
            });
            test("enabled", "<Button x:Name='enabled'/>", page =>
            {
                var control = (Control)page.FindName("enabled");
                control.IsEnabled = false;
                Check(!control.IsEnabled && !((System.Windows.Controls.Button)PeerOf(control)).IsEnabled, "code-side enabled state reaches WPF");
            });
            test("inline-ticks", "<Slider x:Name='ticks' Minimum='0' Maximum='1' TickFrequency='0.05' TickPlacement='Inline'/>", page =>
            {
                var slider = (System.Windows.Controls.Slider)PeerOf(page.FindName("ticks"));
                Check(slider.TickPlacement == System.Windows.Controls.Primitives.TickPlacement.BottomRight && slider.TickFrequency == 0.05,
                      "inline tick markers use WPF's visible tick placement");
            });
            Check(failures.Count == 0, "compatibility gaps:\n" + string.Join("\n", failures));
            Console.WriteLine("PASS: surveyed compatibility mappings load through WPF");
        }

        void VerifyClicks()
        {
            File.WriteAllText("Pages/click.xaml", "<Page" + Ns + "><Button/></Page>");
            var page = new SourcePage(); Application.LoadComponent(page, new Uri("ms-appx:///Pages/click.xaml"));
            var button = (Button)page.Content;
            var peer = (System.Windows.Controls.Button)PeerOf(button);
            int clicks = 0;
            var token = button.add_Click((sender, args) =>
            {
                ++clicks;
                Check(ReferenceEquals(sender, button) && ReferenceEquals(args.OriginalSource, button), "click sender and source use the same wrapper");
            });
            peer.RaiseEvent(new System.Windows.RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent, peer));
            button.remove_Click(token);
            peer.RaiseEvent(new System.Windows.RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent, peer));
            Check(clicks == 1, "click event removal");
            var failure = new InvalidOperationException("click callback failure");
            token = button.add_Click((sender, args) => { throw failure; });
            try { peer.RaiseEvent(new System.Windows.RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent, peer)); throw new Exception("click failure accepted"); }
            catch (InvalidOperationException actual) { Check(ReferenceEquals(actual, failure), "click callback preserves exception identity"); }
            button.remove_Click(token);
            Check(ShimLogContains("managed callback threw System.InvalidOperationException: click callback failure"), "click callback exception logged");
            Console.WriteLine("PASS: typed buttons forward WPF clicks, tokens and callback failures");
        }

        void VerifyInputEvents()
        {
            File.WriteAllText("Pages/input-events.xaml", "<Page" + Ns + "><Grid><Slider x:Name='slider'/><ToggleSwitch x:Name='toggle'/></Grid></Page>");
            var page = new SourcePage(); Application.LoadComponent(page, new Uri("ms-appx:///Pages/input-events.xaml"));
            var element = (UIElement)page.Content; var peer = (System.Windows.UIElement)PeerOf(element);
            Action<System.Windows.RoutedEvent, Func<Windows.UI.Xaml.Input.PointerEventHandler, System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken>, Action<System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken>> pointer = (routed, add, remove) =>
            {
                int count = 0;
                var token = add((sender, args) => { ++count; Check(ReferenceEquals(sender, element) && ReferenceEquals(args.OriginalSource, element), "pointer wrapper identity"); args.Handled = true; });
                System.Windows.Input.MouseEventArgs argsWpf = routed == System.Windows.UIElement.MouseDownEvent ?
                    (System.Windows.Input.MouseEventArgs)new System.Windows.Input.MouseButtonEventArgs(System.Windows.Input.Mouse.PrimaryDevice, 1, System.Windows.Input.MouseButton.Left) :
                    new System.Windows.Input.MouseEventArgs(System.Windows.Input.Mouse.PrimaryDevice, 1);
                argsWpf.RoutedEvent = routed; peer.RaiseEvent(argsWpf);
                Check(count == 1 && argsWpf.Handled, "pointer routed event and Handled");
                remove(token); argsWpf.Handled = false; peer.RaiseEvent(argsWpf); Check(count == 1, "pointer removal");
            };
            pointer(System.Windows.UIElement.MouseEnterEvent, element.add_PointerEntered, element.remove_PointerEntered);
            pointer(System.Windows.UIElement.MouseLeaveEvent, element.add_PointerExited, element.remove_PointerExited);
            pointer(System.Windows.UIElement.MouseDownEvent, element.add_PointerPressed, element.remove_PointerPressed);
            int taps = 0;
            var tapped = element.add_Tapped((sender, args) => { ++taps; Check(ReferenceEquals(sender, element) && ReferenceEquals(args.OriginalSource, element), "tap identity"); args.Handled = true; });
            Action<System.Windows.RoutedEvent> mouse = routed => peer.RaiseEvent(new System.Windows.Input.MouseButtonEventArgs(System.Windows.Input.Mouse.PrimaryDevice, 1, System.Windows.Input.MouseButton.Left) { RoutedEvent = routed });
            mouse(System.Windows.UIElement.MouseLeftButtonUpEvent); Check(taps == 0, "a release without a press is not a tap");
            mouse(System.Windows.UIElement.MouseLeftButtonDownEvent); mouse(System.Windows.UIElement.MouseLeftButtonUpEvent); Check(taps == 1, "mouse click synthesizes a tap");
            element.remove_Tapped(tapped); mouse(System.Windows.UIElement.MouseLeftButtonDownEvent); mouse(System.Windows.UIElement.MouseLeftButtonUpEvent); Check(taps == 1, "tap removal");
            var inner = new Grid(); ((Grid)element).Children.Add(inner);
            var innerPeer = (System.Windows.UIElement)PeerOf(inner); int innerTaps = 0, outerTaps = 0; bool handled = true;
            var innerToken = inner.add_Tapped((sender, args) => { ++innerTaps; args.Handled = handled; });
            var outerToken = element.add_Tapped((sender, args) => { ++outerTaps; Check(ReferenceEquals(args.OriginalSource, inner), "bubbling tap retains original child wrapper"); });
            Action clickInner = () =>
            {
                innerPeer.RaiseEvent(new System.Windows.Input.MouseButtonEventArgs(System.Windows.Input.Mouse.PrimaryDevice, 1, System.Windows.Input.MouseButton.Left) { RoutedEvent = System.Windows.UIElement.MouseDownEvent });
                innerPeer.RaiseEvent(new System.Windows.Input.MouseButtonEventArgs(System.Windows.Input.Mouse.PrimaryDevice, 1, System.Windows.Input.MouseButton.Left) { RoutedEvent = System.Windows.UIElement.MouseUpEvent });
            };
            clickInner(); Check(innerTaps == 1 && outerTaps == 0, "a handled tap stops at the child");
            handled = false; clickInner(); Check(innerTaps == 2 && outerTaps == 1, "an unhandled tap bubbles to the parent exactly once");
            inner.remove_Tapped(innerToken); element.remove_Tapped(outerToken);
            var slider = (Slider)page.FindName("slider"); int changes = 0;
            var changed = slider.add_ValueChanged((sender, args) => { ++changes; Check(ReferenceEquals(sender, slider) && args.OldValue == 0 && args.NewValue == 3, "range values and sender"); });
            slider.Value = 3; slider.remove_ValueChanged(changed); slider.Value = 4; Check(changes == 1, "range event removal");
            var toggle = (ToggleSwitch)page.FindName("toggle"); int toggles = 0;
            var toggled = toggle.add_Toggled((sender, args) => { ++toggles; Check(ReferenceEquals(sender, toggle), "toggle sender"); });
            toggle.IsOn = true; toggle.IsOn = false; toggle.remove_Toggled(toggled); toggle.IsOn = true; Check(toggles == 2, "checked and unchecked forward as Toggled, with removal");
            Console.WriteLine("PASS: pointer, tapped, range and toggle events use WPF with tokens and UWP arguments");
        }

        const string StylesXaml = "<ResourceDictionary" + Ns + ">" +
            "<ResourceDictionary.ThemeDictionaries><ResourceDictionary x:Key='Default'><x:String x:Key='Glyph'>back</x:String></ResourceDictionary>" +
            "<ResourceDictionary x:Key='HighContrast'><x:String x:Key='Glyph'>hc</x:String><x:String x:Key='Big'>shadowed</x:String></ResourceDictionary>" +
            "</ResourceDictionary.ThemeDictionaries>" +
            "<Style x:Key='Big' TargetType='TextBlock'><Setter Property='FontSize' Value='42'/></Style>" +
            "<Style x:Key='Btn' TargetType='Button'><Setter Property='Template'><Setter.Value><ControlTemplate TargetType='Button'>" +
            "<Grid x:Name='root'><VisualStateManager.VisualStateGroups><VisualStateGroup x:Name='CommonStates'>" +
            "<VisualState x:Name='Normal'/><VisualState x:Name='PointerOver'/></VisualStateGroup></VisualStateManager.VisualStateGroups>" +
            "<Border x:Name='face' Background='{TemplateBinding Background}'><ContentPresenter/></Border></Grid>" +
            "</ControlTemplate></Setter.Value></Setter></Style></ResourceDictionary>";
        const string AppXaml = "<Application x:Class='Fixture.SourceApp' RequestedTheme='Dark'" + Ns + "><Application.Resources><ResourceDictionary>" +
            "<ResourceDictionary.MergedDictionaries><ResourceDictionary Source='Common/Styles.xaml'/></ResourceDictionary.MergedDictionaries>" +
            "</ResourceDictionary></Application.Resources></Application>";
        const string StyledXaml = "<Page" + Ns + "><StackPanel>" +
            "<TextBlock x:Name='big' Style='{StaticResource Big}' Foreground='{ThemeResource ApplicationForegroundThemeBrush}'/>" +
            "<Button x:Name='button' Style='{StaticResource Btn}' Background='Red' Content='go'/></StackPanel></Page>";

        void VerifyStyles()
        {
            Directory.CreateDirectory("Common");
            File.WriteAllText("Common/Styles.xaml", StylesXaml);
            File.WriteAllText("App.xaml", AppXaml);
            Application.LoadComponent(this, new Uri("ms-appx:///App.xaml"));
            Check(Resources.MergedDictionaries.Count == 1 && Resources.MergedDictionaries[0].ContainsKey("Big") && Resources.ContainsKey("Big"),
                  "App.xaml merges a relative Source dictionary");
            Check((string)Resources["Glyph"] == "back" && Resources["Big"] is Style, "the dark (Default) theme dictionary applies after the dictionary's own keys");
            File.WriteAllText("Pages/styled.xaml", StyledXaml);
            var page = new SourcePage();
            Application.LoadComponent(page, new Uri("ms-appx:///Pages/styled.xaml"));
            var big = (TextBlock)page.FindName("big");
            Check(big.FontSize == 42, "page Style from App.xaml's merged dictionary: " + big.FontSize);
            var foreground = big.Foreground as Windows.UI.Xaml.Media.SolidColorBrush;
            Check(foreground != null && foreground.Color.A != 0, "ThemeResource resolves the theme's ApplicationForegroundThemeBrush");
            var button = (Control)page.FindName("button");
            var peer = PeerOf(button) as System.Windows.Controls.Control;
            Check(peer != null && peer.ApplyTemplate(), "Button template applies");
            var root = Windows.UI.Xaml.Media.VisualTreeHelper.GetChild(button, 0);
            Check(root is Grid && ReferenceEquals(root, Windows.UI.Xaml.Media.VisualTreeHelper.GetChild(button, 0)), "template root wraps stably");
            Check(VisualStateManager.GoToState(button, "PointerOver", false) && !VisualStateManager.GoToState(button, "Missing", false), "VisualStateManager states");
            var code = new ResourceDictionary { Source = new Uri("ms-appx:///Common/Styles.xaml") };
            Check(code.ContainsKey("Btn") && code.Source.OriginalString == "ms-appx:///Common/Styles.xaml", "code ResourceDictionary.Source loads the package file");
            File.WriteAllText("Pages/nokey.xaml", "<Page" + Ns + "><TextBlock Style='{StaticResource Nope}'/></Page>");
            string missing = Fails("ms-appx:///Pages/nokey.xaml", new SourcePage());
            Check(missing.Contains("Nope") && missing.Contains("nokey.xaml"), "unknown resource key is named with its file: " + missing);
            File.WriteAllText("light.xaml", AppXaml.Replace("Dark", "Light"));
            string light = Fails("ms-appx:///light.xaml", this);
            Check(light.Contains("Light"), "an unsupported theme is named: " + light);
            Console.WriteLine("PASS: styles, templates, merged and theme resources load through WPF");
            VerifyBindings();
        }

        const string BindingXaml = "<local:ModelPage x:Class='Fixture.ModelPage'" + Ns + " xmlns:local='using:Fixture'" +
            " DataContext='{Binding DefaultViewModel, RelativeSource={RelativeSource Self}}'>" +
            "<local:ModelPage.Resources><local:Fmt x:Key='Fmt'/><local:Boom x:Key='Boom'/></local:ModelPage.Resources><StackPanel>" +
            "<TextBlock x:Name='score' Text='{Binding Score, Converter={StaticResource Fmt}, ConverterParameter=pts}'/>" +
            "<TextBlock x:Name='echo' Text='{Binding Text, ElementName=score}'/>" +
            "<TextBlock x:Name='plain' Text='{Binding Count}'/>" +
            "<TextBlock x:Name='boom' Text='{Binding Score, Converter={StaticResource Boom}}'/>" +
            "</StackPanel></local:ModelPage>";

        // Bindings settle once WPF's dispatcher has run; the checks wait for a tick.
        void VerifyBindings()
        {
            File.WriteAllText("Pages/binding.xaml", BindingXaml);
            var page = new ModelPage();
            page.DefaultViewModel.Put("Count", 5);
            Application.LoadComponent(page, new Uri("ms-appx:///Pages/binding.xaml"));
            page.DefaultViewModel.Put("Score", 3);
            var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(50) };
            int ticks = 0;
            timer.add_Tick((sender, args) =>
            {
                try
                {
                    if (++ticks == 1) { page.DefaultViewModel.Put("Score", 4); return; }
                    timer.Stop();
                    var score = (TextBlock)page.FindName("score");
                    Check(score.Text == "4 pts", "map key binding through a UWP converter follows changes: '" + score.Text + "'");
                    Check(Fmt.Target == typeof(string) && (string)Fmt.Parameter == "pts" && !string.IsNullOrEmpty(Fmt.Language), "converter sees UWP target type, parameter and language");
                    Check(((TextBlock)page.FindName("echo")).Text == "4 pts", "ElementName binding");
                    Check(((TextBlock)page.FindName("plain")).Text == "5", "default conversion without a converter");
                    Check(ShimLogContains("managed callback threw System.InvalidOperationException: converter failure"), "converter failure logged");
                    Console.WriteLine("PASS: bindings resolve game properties, observable maps, element names and UWP converters");
                    Exit();
                }
                catch (Exception error) { Console.Error.WriteLine(error); Environment.Exit(1); }
            });
            timer.Start();
        }
        static object PeerOf(object wrapper)
        {
            return typeof(DependencyObject).GetField("peer", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).GetValue(wrapper);
        }

        protected override void OnLaunched(Windows.ApplicationModel.Activation.LaunchActivatedEventArgs args)
        {
            try { Verify(); }
            catch (Exception error) { Console.Error.WriteLine(error); Environment.Exit(1); }
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
}
