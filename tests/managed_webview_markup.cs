using System;
using System.IO;
using Windows.ApplicationModel.Activation;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Markup;

sealed class MarkupWebViewApp : Application, IXamlMetadataProvider {
    public IXamlType GetXamlType(Type type) { return null; }
    public IXamlType GetXamlType(string name) { return null; }
    public XmlnsDefinition[] GetXmlnsDefinitions() { return new XmlnsDefinition[0]; }
    protected override void OnLaunched(LaunchActivatedEventArgs args) {
        var root = (Grid)XamlReader.Load(
            "<Grid xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' " +
            "xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' xmlns:p='using:Windows.UI.Xaml.Controls'>" +
            "<p:PortableWebView x:Name='indexArticleView' Width='420' Height='180' Margin='10,20,30,40' " +
            "DefaultBackgroundColor='Transparent' IsRightTapEnabled='False'/>" +
            "<p:PortableWebView x:Name='articleView' DefaultBackgroundColor='#FF123456'/></Grid>");
        var index = (WebView)root.FindName("indexArticleView");
        var article = (WebView)root.FindName("articleView");
        if (index == null || article == null || index == article || index.Width != 420 || index.Height != 180 ||
            index.Margin.Left != 10 || index.Margin.Bottom != 40 || index.IsRightTapEnabled)
            throw new Exception("Parsed WebView identity, properties or namescope");
        index.ScriptNotify += (s, e) => { throw new Exception("Unloaded WebView notified"); };
        index.NavigateToString("<h1>Queued until Loaded</h1>");
        // Leave the parsed views detached: the parser contract needs no runtime.
        Window.Current.Content = new TextBlock { Text = "Parsed WebViews passed" };
        Window.Current.Activate();
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(30) };
        timer.Tick += (s, e) => {
            timer.Stop();
            File.WriteAllText("startup-passed.txt", "parsed WebViews, CLR casts, namescope, inherited properties, colors");
            Window.Current.Close();
        };
        timer.Start();
    }
}
static class ManagedMarkupWebViewTest {
    [STAThread] static void Main() { Application.Start(p => new MarkupWebViewApp()); }
}
