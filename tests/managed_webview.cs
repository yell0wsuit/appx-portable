using System;
using System.IO;
using Windows.ApplicationModel.Activation;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;

sealed class WebViewApp : Application {
    private WebView web;
    private DispatcherTimer timeout;
    private bool ready, package;
    protected override void OnLaunched(LaunchActivatedEventArgs args) {
        web = new WebView { Name = "articleView", IsRightTapEnabled = false, Width = 500, Height = 300,
                            Margin = new Thickness(20) };
        var grid = new Grid();
        grid.Children.Add(web);
        File.WriteAllText("webview-local.js", "function echo(s){return s;} window.external.notify('ready');");
        File.WriteAllText("webview-local.html", "<!doctype html><h1>Package resource</h1><script>window.external.notify('package');</script>");
        web.ScriptNotify += (s, e) => {
            if (s != web) throw new Exception("ScriptNotify sender");
            if (e.Value == "package") {
                if (!ready || e.CallingUri.AbsoluteUri != "ms-appx-web:///webview-local.html" ||
                    web.Source.AbsoluteUri != e.CallingUri.AbsoluteUri) throw new Exception("Package URI mapping");
                package = true;
                return;
            }
            if (e.Value != "ready") throw new Exception("ScriptNotify contract");
            // This runs from a notification, the legacy dictionaries' pattern.
#pragma warning disable 618
            string text = web.InvokeScript("echo", new[] { "quote\" slash\\ newline\n\u263a" });
#pragma warning restore 618
            if (text != "quote\" slash\\ newline\n\u263a") throw new Exception("InvokeScript result: " + text);
            ready = true;
            web.Navigate(new Uri("ms-appx-web:///webview-local.html"));
        };
#pragma warning disable 618
        web.LoadCompleted += (s, e) => {
            if (!package) return;
            if (e.Uri.AbsoluteUri != "ms-appx-web:///webview-local.html") throw new Exception("LoadCompleted URI");
            timeout.Stop();
            File.WriteAllText("startup-passed.txt", "CLR projection, HTML, package resources, notify, synchronous script, LoadCompleted");
            Window.Current.Close();
        };
#pragma warning restore 618
        // HTML can be supplied before the element enters the tree.
        web.NavigateToString("<!doctype html><h1>WebView stand-in</h1><script src='ms-appx-web:///webview-local.js'></script>");
        Window.Current.Content = grid;
        Window.Current.Activate();
        timeout = new DispatcherTimer { Interval = TimeSpan.FromSeconds(20) };
        timeout.Tick += (s, e) => { throw new Exception("WebView2 notification timed out"); };
        timeout.Start();
    }
}
static class ManagedWebViewTest {
    [STAThread] static void Main() { Application.Start(p => new WebViewApp()); }
}
