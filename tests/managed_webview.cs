using System;
using System.IO;
using System.Runtime.InteropServices;
using Windows.ApplicationModel.Activation;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Markup;

#if COMPILED
sealed class OriginalWebViewPage : UserControl, IComponentConnector {
    public WebView Browser;
    public void Connect(int id, object target) {
        if (id == 2) Browser = (WebView)target;
        else if (id != 1) throw new Exception("Unexpected original XBF connection");
    }
    public OriginalWebViewPage() {
        Application.LoadComponent(this,new Uri("ms-appx:///WebViewFixture.xaml"));
    }
}
#endif

sealed class WebViewApp : Application, IXamlMetadataProvider {
    public IXamlType GetXamlType(Type type) { return null; }
    public IXamlType GetXamlType(string name) { return null; }
    public XmlnsDefinition[] GetXmlnsDefinitions() { return new XmlnsDefinition[0]; }
    private WebView web;
    private DispatcherTimer timeout;
    private bool ready, package;
#if CLIP
    private Border clip;
    [DllImport("user32.dll")] static extern int GetWindowRgnBox(IntPtr window, out Rect rect);
    static IntPtr BrowserHost(IntPtr host) {
        IntPtr browser = IntPtr.Zero;
        EnumChildWindows(host, (window, unused) => {
            var name = new System.Text.StringBuilder(128);
            GetClassName(window, name, name.Capacity);
            if (name.ToString() != "AppxPortableWebView") return true;
            browser = window;
            return false;
        }, IntPtr.Zero);
        if (browser == IntPtr.Zero) throw new Exception("WebView browser host missing");
        return browser;
    }
#endif
#if SCROLL || CLIP
    delegate bool EnumWindow(IntPtr window, IntPtr parameter);
    [StructLayout(LayoutKind.Sequential)] struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] struct Point { public int X, Y; }
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] static extern bool EnumThreadWindows(uint thread, EnumWindow callback, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumWindow callback, IntPtr parameter);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr window, System.Text.StringBuilder name, int capacity);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(Point point);
    [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr window);
    [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr window, IntPtr dc);
    [DllImport("gdi32.dll")] static extern uint GetPixel(IntPtr dc, int x, int y);
    static bool Near(uint actual, uint expected) {
        for (int shift = 0; shift < 24; shift += 8)
            if (Math.Abs((int)((actual >> shift) & 255) - (int)((expected >> shift) & 255)) > 8) return false;
        return actual != 0xFFFFFFFF;
    }
    static IntPtr Host() {
        IntPtr host = IntPtr.Zero;
        EnumThreadWindows(GetCurrentThreadId(), (window, unused) => {
            var name = new System.Text.StringBuilder(128);
            GetClassName(window, name, name.Capacity);
            if (name.ToString() != "AppxPortableWindow") return true;
            host = window;
            return false;
        }, IntPtr.Zero);
        if (host == IntPtr.Zero) throw new Exception("WebView host window missing");
        return host;
    }
    static IntPtr Renderer(IntPtr host) {
        IntPtr renderer = IntPtr.Zero;
        EnumChildWindows(host, (window, unused) => {
            var name = new System.Text.StringBuilder(128);
            GetClassName(window, name, name.Capacity);
            if (name.ToString() != "Chrome_RenderWidgetHostHWND") return true;
            renderer = window;
            return false;
        }, IntPtr.Zero);
        if (renderer == IntPtr.Zero) throw new Exception("WebView renderer window missing");
        return renderer;
    }
#endif
    protected override void OnLaunched(LaunchActivatedEventArgs args) {
#if COMPILED
        var page = new OriginalWebViewPage();
        web = page.Browser;
        page.Content = null;
        if(web == null) throw new Exception("Original XBF WebView was not connected");
#elif MARKUP
        web = (WebView)XamlReader.Load(
            "<p:PortableWebView xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' " +
            "xmlns:p='using:Windows.UI.Xaml.Controls' Name='articleView' IsRightTapEnabled='False' " +
            "Width='500' Height='300' Margin='20' DefaultBackgroundColor='Transparent'/>");
#else
        web = new WebView { Name = "articleView", IsRightTapEnabled = false, Width = 500, Height = 300,
                            Margin = new Thickness(20) };
#endif
        var grid = new Grid();
#if SCROLL
        // A transparent browser must retain this authored light backdrop.
        grid.Background = new Windows.UI.Xaml.Media.SolidColorBrush(Windows.UI.Color.FromArgb(255, 240, 245, 250));
#endif
#if CLIP
        // A narrower ancestor clips the element, as a narrowed SettingsFlyout does.
        clip = new Border { Width = 200, Height = 340, HorizontalAlignment = HorizontalAlignment.Left, Child = web };
        grid.Children.Add(clip);
#else
        grid.Children.Add(web);
#endif
        File.WriteAllText("webview-local.js", "function echo(s){return s;} window.external.notify('ready');");
#if SCROLL
        File.WriteAllText("webview-local.js", "function echo(s){return s;}" +
            "function scrollPosition(){return String(scrollY);}" +
            "window.onload=function(){document.body.style.minHeight='3000px';" +
            "const marker=document.createElement('div');" +
            "marker.style.cssText='position:fixed;left:10px;top:10px;width:30px;height:30px;background:#ff0000;z-index:100';" +
            "document.body.appendChild(marker);window.external.notify('ready');};");
#endif
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
#if CLIP
            // The 500-wide element shows only the border's 200 less its 20 margin.
            var browserHost = BrowserHost(Host());
            Rect bounds, region;
            if (!GetWindowRect(browserHost, out bounds)) throw new Exception("Cannot locate WebView browser host");
            double scale = (bounds.Right - bounds.Left) / web.Width;
            if (GetWindowRgnBox(browserHost, out region) != 2) throw new Exception("Narrower ancestor did not clip the browser host");
            if (region.Left != 0 || Math.Abs(region.Right - 180 * scale) > 2 || Math.Abs(region.Bottom - 300 * scale) > 2)
                throw new Exception("Browser clip " + region.Left + ".." + region.Right + " x " + region.Bottom + " at scale " + scale);
            // A removed WebView is suspended, and resumes when called into.
            clip.Child = null;
            var resume = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
            resume.Tick += (sender, unused) => {
                resume.Stop();
#pragma warning disable 618
                string again = web.InvokeScript("echo", new[] { "resumed" });
#pragma warning restore 618
                if (again != "resumed") throw new Exception("InvokeScript after suspension: " + again);
                clip.Child = web;
                ready = true;
                web.Navigate(new Uri("ms-appx-web:///webview-local.html"));
            };
            resume.Start();
#elif SCROLL
            var host = Host();
            Rect rect;
            if (!GetWindowRect(host, out rect) ||
                !SetWindowPos(host, IntPtr.Zero, 0, 0, rect.Right-rect.Left+80, rect.Bottom-rect.Top+40, 0x2|0x4|0x10))
                throw new Exception("Cannot resize WebView host");
            bool posted = false;
            int paints = 0;
            var scrollCheck = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(300) };
            scrollCheck.Tick += (sender, unused) => {
                if (!posted) {
                    var renderer = Renderer(host);
                    Rect browser;
                    if (!GetWindowRect(renderer, out browser)) throw new Exception("Cannot locate WebView renderer");
                    var point = new Point { X = browser.Left + 100, Y = browser.Top + 100 };
                    // Keep this test window above other desktop windows while
                    // hit testing; no keyboard focus or real mouse input.
                    SetWindowPos(host, (IntPtr)(-1), 0, 0, 0, 0, 0x1|0x2|0x10);
                    var hit = WindowFromPoint(point);
                    var name = new System.Text.StringBuilder(128);
                    GetClassName(hit, name, name.Capacity);
                    if (hit != renderer) throw new Exception("Another window covers the WebView test");
                    if (!name.ToString().StartsWith("Chrome_")) throw new Exception("XAML obscured browser input after resize: " + name);
                    // Read displayed pixels: a live DOM can still be hidden
                    // behind XAML or paint a dark canvas under black text.
                    double scale = (browser.Right - browser.Left) / web.Width;
                    var dc = GetDC(IntPtr.Zero);
                    uint marker, backdrop;
                    try {
                        marker = GetPixel(dc, browser.Left + (int)(20 * scale), browser.Top + (int)(20 * scale));
                        backdrop = GetPixel(dc, browser.Left + (int)(80 * scale), browser.Top + (int)(80 * scale));
                    } finally { ReleaseDC(IntPtr.Zero, dc); }
                    if (!Near(marker, 0x0000FF) || !Near(backdrop, 0xFAF5F0)) {
                        if (++paints < 8) return;
                        throw new Exception("Browser pixels missing or wrong theme: marker=" + marker.ToString("x6") +
                                            " backdrop=" + backdrop.ToString("x6"));
                    }
                    // Exercise wheel routing through the host pump instead of
                    // addressing Chromium. Windows rejects posted WM_POINTER
                    // messages, so use the equivalent legacy mouse message.
                    var packed = (IntPtr)((point.Y << 16) | (point.X & 0xFFFF));
                    if (!PostMessage(host, 0x20A, (IntPtr)(-120 << 16), packed)) throw new Exception("Cannot post host wheel");
                    posted = true;
                    return;
                }
                scrollCheck.Stop();
#pragma warning disable 618
                double offset = double.Parse(web.InvokeScript("scrollPosition", new string[0]), System.Globalization.CultureInfo.InvariantCulture);
#pragma warning restore 618
                if (offset <= 0) throw new Exception("Native wheel did not scroll WebView");
                ready = true;
                web.Navigate(new Uri("ms-appx-web:///webview-local.html"));
            };
            scrollCheck.Start();
#else
            ready = true;
            web.Navigate(new Uri("ms-appx-web:///webview-local.html"));
#endif
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
