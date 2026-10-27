using System;
using System.IO;
using Windows.ApplicationModel.Activation;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Markup;

sealed class CompiledWebViewPage : UserControl, IComponentConnector {
    public WebView Browser;
    public void Connect(int id, object target) {
        if (id == 2) Browser = (WebView)target;
        else if (id != 1) throw new Exception("Unexpected compiled connection ID: " + id);
    }
    public CompiledWebViewPage() {
        Application.LoadComponent(this,new Uri("ms-appx:///WebViewFixture.xaml"));
    }
}
sealed class CompiledWebViewApp : Application, IXamlMetadataProvider {
    public IXamlType GetXamlType(Type type) { return null; }
    public IXamlType GetXamlType(string name) { return null; }
    public XmlnsDefinition[] GetXmlnsDefinitions() { return new XmlnsDefinition[0]; }
    protected override void OnLaunched(LaunchActivatedEventArgs args) {
        var before = File.ReadAllBytes("WebViewFixture.xbf");
        for(int i=0;i<3;i++) {
            var page = new CompiledWebViewPage();
            if(page.Browser == null || page.Browser != page.FindName("articleView") ||
               page.Browser.Width != 500 || page.Browser.Height != 300 || page.Content != page.Browser)
                throw new Exception("Original compiled WebView connection, properties, or namescope failed");
            page.Browser.NavigateToString("<h1>Queued until Loaded</h1>");
        }
        var after = File.ReadAllBytes("WebViewFixture.xbf");
        if(Convert.ToBase64String(before) != Convert.ToBase64String(after)) throw new Exception("Package XBF changed");
        // Detached views exercise parsing without requiring the browser runtime.
        Window.Current.Content = new TextBlock {Text="Original XBF WebView passed"};
        Window.Current.Activate();
        var timer = new DispatcherTimer {Interval=TimeSpan.FromMilliseconds(30)};
        timer.Tick += (s,e) => {
            timer.Stop();
            File.WriteAllText("startup-passed.txt","original XBF, connections, properties, namescope, unchanged package");
            Window.Current.Close();
        };
        timer.Start();
    }
}
static class CompiledWebViewTest {
    [STAThread] static void Main() {Application.Start(p=>new CompiledWebViewApp());}
}
