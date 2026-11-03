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
            foreach (var type in new[] { typeof(SourcePage), typeof(Badge), typeof(ModelPage) }) Types.Add(type.FullName, new SourceXamlType(type));
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
        static bool ShimLogContains(string text)
        {
            if (!File.Exists("appx-portable.log")) return false;
            using (var stream = new FileStream("appx-portable.log", FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
            using (var reader = new StreamReader(stream)) return reader.ReadToEnd().Contains(text);
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

            File.WriteAllText("Pages/replacement.xaml", "<Page" + Ns + " x:Name='replacement'/>");
            Application.LoadComponent(page, new Uri("ms-appx:///Pages/replacement.xaml"));
            Check(page.Content == null && page.FindName("title") == null && ReferenceEquals(page.FindName("replacement"), page), "reload replaces tree and names");
            Console.WriteLine("PASS: managed loader builds WPF content with names, connections and game types; unsupported input fails explicitly");
            VerifyStyles();
        }

        const string StylesXaml = "<ResourceDictionary" + Ns + ">" +
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
