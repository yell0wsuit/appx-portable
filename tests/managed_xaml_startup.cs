using System;
using System.IO;
using System.Threading;
using Windows.ApplicationModel.Activation;
using Windows.UI.Notifications;
using Windows.UI.StartScreen;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Controls.Primitives;
using Windows.UI.Xaml.Media;

sealed class StartupApp : Application {
    private bool constructed;
    private Frame frame;
    private SwapChainPanel panel;
    private TextBlock text;
    private static int launches;

    public StartupApp() {
#if XAML_LIGHT_THEME
        // As generated InitializeComponent does; the theme comes from the real parser.
        LoadComponent(this, new Uri("ms-appx:///App.xaml"));
#endif
#if LIGHT_THEME
        RequestedTheme = ApplicationTheme.Light;
#endif
#if CONSTRUCTOR_CONTROLS
        CreateContent();
#endif
        constructed = true;
    }

    private void CreateContent() {
        frame = new Frame();
        var content = new Grid();
        text = new TextBlock { Text = "Theme fixture" };
        content.Children.Add(text);
        frame.Content = content;
        panel = new SwapChainPanel();
#if ROOT_LIGHT_THEME
        panel.RequestedTheme = ElementTheme.Light;
#endif
        panel.Children.Add(frame);
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args) {
#if LIGHT_THEME || XAML_LIGHT_THEME
        const ApplicationTheme expectedTheme = ApplicationTheme.Light;
#else
        const ApplicationTheme expectedTheme = ApplicationTheme.Dark;
#endif
#if LIGHT_THEME || ROOT_LIGHT_THEME || XAML_LIGHT_THEME
        const byte expectedForeground = 0;
#else
        const byte expectedForeground = 255;
#endif
        if (RequestedTheme != expectedTheme) throw new Exception("incorrect Application theme");
        if (!constructed || Current != this) throw new Exception("premature launch or wrong Application.Current");
        if (++launches != 1 || args.PrelaunchActivated) throw new Exception("invalid launch");
        if (!Windows.ApplicationModel.Core.CoreApplication.MainView.CoreWindow.Dispatcher.HasThreadAccess)
            throw new Exception("wrong dispatcher");
        var window = Window.Current.CoreWindow;
        if (Windows.UI.Core.CoreWindow.GetForCurrentThread() != window ||
            Windows.ApplicationModel.Core.CoreApplication.MainView.CoreWindow != window)
            throw new Exception("inconsistent CoreWindow identity");
        var dispatcher = window.Dispatcher;
        Exception workerFailure = null;
        var worker = new Thread(() => {
            try {
                if (window.Dispatcher != dispatcher || window.Dispatcher.HasThreadAccess)
                    throw new Exception("worker received the wrong dispatcher");
            } catch (Exception ex) { workerFailure = ex; }
        });
        worker.Start();
        worker.Join();
        if (workerFailure != null) throw workerFailure;
#if !CONSTRUCTOR_CONTROLS
        CreateContent();
#endif
        // These shell queries must be safe for a portable, unpackaged game.
        if (SecondaryTile.Exists("fixture-pin")) throw new Exception("unexpected Start tile");
        var badge = new BadgeNotification(BadgeUpdateManager.GetTemplateContent(BadgeTemplateType.BadgeNumber));
        BadgeUpdateManager.CreateBadgeUpdaterForApplication().Update(badge);
        BadgeUpdateManager.CreateBadgeUpdaterForApplication().Clear();
        Window.Current.Content = panel;
        Window.Current.Activate();
        var timer = new DispatcherTimer();
        var popupText = new TextBlock { Text = "Detached popup theme" };
        var popup = new Popup { Child = popupText };
        bool popupOpened = false;
        timer.Interval = TimeSpan.FromMilliseconds(150);
        timer.Tick += (s, e) => {
            if (!popupOpened) {
                popup.IsOpen = true;
                popupOpened = true;
                return;
            }
            timer.Stop();
            if (launches != 1 || Window.Current.Content != panel || frame.ActualWidth <= 0)
                throw new Exception("invalid island content or repeated launch");
            var foreground = text.Foreground as SolidColorBrush;
            if (foreground == null || foreground.Color.R != expectedForeground ||
                foreground.Color.G != expectedForeground || foreground.Color.B != expectedForeground)
                throw new Exception("incorrect inherited text foreground");
            var popupForeground = popupText.Foreground as SolidColorBrush;
            byte popupExpected = expectedTheme == ApplicationTheme.Dark ? (byte)255 : (byte)0;
            if (popupForeground == null || popupForeground.Color.R != popupExpected ||
                popupForeground.Color.G != popupExpected || popupForeground.Color.B != popupExpected)
                throw new Exception("incorrect detached popup foreground");
            popup.IsOpen = false;
            File.WriteAllText("startup-passed.txt", "constructor, identity, prelaunch, dispatcher, shell, launch count, layout");
#if APPLICATION_EXIT
            // Simulate an engine that already tore down before requesting exit.
            Window.Current.CoreWindow.VisibilityChanged += (sender, visibility) => {
                if (!visibility.Visible) Environment.Exit(91);
            };
            Suspending += (sender, suspension) => Environment.Exit(92);
            Application.Current.Exit();
            Environment.Exit(93);
#else
            Window.Current.Close();
#endif
        };
        timer.Start();
    }
}

static class ManagedXamlStartupTest {
    [STAThread]
    static void Main() {
        Application.Start(p => new StartupApp());
    }
}
