using System;
using System.IO;
using Windows.ApplicationModel.Activation;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Markup;
using Windows.UI.Xaml.Media;

sealed class LegacyGrid : GridView { }
sealed class ScrollApp : Application {
    private ScrollViewer horizontalScroll;
    private int stage;
    static ScrollViewer FindScroll(DependencyObject element) {
        var scroll = element as ScrollViewer;
        if (scroll != null) return scroll;
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(element); ++i) {
            scroll = FindScroll(VisualTreeHelper.GetChild(element, i));
            if (scroll != null) return scroll;
        }
        return null;
    }
    static LegacyGrid CreateGrid(string orientation) {
        var grid = new LegacyGrid { Width = 500, Height = 180 };
        grid.ItemsPanel = (ItemsPanelTemplate)XamlReader.Load(
            "<ItemsPanelTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'>" +
            "<StackPanel Orientation='" + orientation + "'/></ItemsPanelTemplate>");
        for (int i = 0; i < 12; ++i) grid.Items.Add(new TextBlock { Text = "Item " + i, Width = 150, Height = 80 });
        return grid;
    }
    protected override void OnLaunched(LaunchActivatedEventArgs args) {
        var horizontal = CreateGrid("Horizontal");
        var vertical = CreateGrid("Vertical");
        var content = new StackPanel();
        content.Children.Add(horizontal);
        content.Children.Add(vertical);
        Window.Current.Content = content;
        Window.Current.Activate();
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(300) };
        timer.Tick += (s, e) => {
            if (stage++ == 0) {
                horizontalScroll = FindScroll(horizontal);
                var verticalScroll = FindScroll(vertical);
                if (horizontalScroll == null || horizontalScroll.ScrollableWidth <= 0 ||
                    horizontalScroll.HorizontalScrollMode != ScrollMode.Enabled ||
                    horizontalScroll.HorizontalScrollBarVisibility != ScrollBarVisibility.Auto)
                    throw new Exception("Legacy horizontal panel is clipped");
                if (verticalScroll == null || verticalScroll.ScrollableHeight <= 0 ||
                    verticalScroll.HorizontalScrollMode != ScrollMode.Disabled)
                    throw new Exception("Vertical grid behavior changed");
                if (!horizontalScroll.ChangeView(200, null, null, true)) throw new Exception("Cannot pan horizontal grid");
            } else {
                timer.Stop();
                if (horizontalScroll.HorizontalOffset < 199) throw new Exception("Horizontal grid did not move");
                File.WriteAllText("startup-passed.txt", "composed grid, horizontal overflow, visible bar, panning, vertical grid preservation");
                Window.Current.Close();
            }
        };
        timer.Start();
    }
}
static class ManagedScrollTest {
    [STAThread] static void Main() { Application.Start(p => new ScrollApp()); }
}
