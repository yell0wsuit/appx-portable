using System;
using System.IO;
using System.Runtime.InteropServices;
using Windows.ApplicationModel.Activation;
using Windows.UI.ApplicationSettings;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Media;
#pragma warning disable 618 // Windows 8 charm APIs are intentionally exercised.

sealed class SettingsApp : Application {
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] static extern bool PostThreadMessage(uint thread, uint message, IntPtr wparam, IntPtr lparam);
    private int requested, invoked, stage, flyoutsLoaded, appearancePhase;
    private uint uiThread;
    private SettingsPane pane;
    private SettingsFlyout flyout;
    private StackPanel page, flyoutContent;
    private ToggleSwitch flyoutToggle;
    void CheckAppearance(ElementTheme expected) {
        var header = FindHeader(flyoutToggle);
        var brush = header == null ? null : header.Foreground as SolidColorBrush;
        byte channel = expected == ElementTheme.Light ? (byte)0 : (byte)255;
        if (flyout.RequestedTheme != expected || brush == null || brush.Color.R != channel ||
            brush.Color.G != channel || brush.Color.B != channel)
            throw new Exception("Dynamic flyout theme/header did not update");
    }
    static TextBlock FindHeader(DependencyObject element) {
        var text = element as TextBlock;
        if (text != null && text.Text == "Show timer") return text;
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(element); ++i) {
            var found = FindHeader(VisualTreeHelper.GetChild(element, i));
            if (found != null) return found;
        }
        return null;
    }
    protected override void OnLaunched(LaunchActivatedEventArgs args) {
        uiThread = GetCurrentThreadId();
        pane = SettingsPane.GetForCurrentView();
        if (pane != SettingsPane.GetForCurrentView()) throw new Exception("SettingsPane identity changed");
        pane.CommandsRequested += CommandsRequested;
        page = new StackPanel { RequestedTheme = ElementTheme.Light };
        Window.Current.Content = page;
        Window.Current.Activate();
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(200) };
        timer.Tick += (s, e) => {
            if (stage == 2 && appearancePhase < 7) {
                if (appearancePhase == 0) ((SolidColorBrush)flyoutContent.Background).Color = Windows.UI.Colors.Black;
                if (appearancePhase == 1) {
                    CheckAppearance(ElementTheme.Dark);
                    flyoutContent.Background = new SolidColorBrush(Windows.UI.Colors.White);
                }
                if (appearancePhase == 2) {
                    CheckAppearance(ElementTheme.Light);
                    flyoutContent.Children.Remove(flyoutToggle);
                    flyout.Content = new Border { Background = new SolidColorBrush(Windows.UI.Colors.Navy), Child = flyoutToggle };
                }
                if (appearancePhase == 3) {
                    CheckAppearance(ElementTheme.Dark);
                    ((SolidColorBrush)((Border)flyout.Content).Background).Opacity = 0.5;
                }
                if (appearancePhase == 4) {
                    CheckAppearance(ElementTheme.Light);
                    page.RequestedTheme = ElementTheme.Dark;
                }
                if (appearancePhase == 5) {
                    CheckAppearance(ElementTheme.Dark);
                    page.RequestedTheme = ElementTheme.Light;
                }
                if (appearancePhase == 6) {
                    CheckAppearance(ElementTheme.Light);
                    flyout.Hide();
                    flyoutContent.Background = new SolidColorBrush(Windows.UI.Colors.Black);
                    if (flyout.RequestedTheme != ElementTheme.Default ||
                        flyout.ReadLocalValue(Control.BackgroundProperty) != DependencyProperty.UnsetValue)
                        throw new Exception("Appearance overrides/listeners survived closing");
                }
                ++appearancePhase;
                return;
            }
            if (stage == 6 && appearancePhase < 11) {
                if (appearancePhase == 7) {
                    var style = new Style { TargetType = typeof(SettingsFlyout) };
                    style.Setters.Add(new Setter(Control.BackgroundProperty, new SolidColorBrush(Windows.UI.Colors.Navy)));
                    flyout.Style = style;
                } else if (appearancePhase == 8) {
                    CheckAppearance(ElementTheme.Dark);
                    var brush = flyout.Background as SolidColorBrush;
                    if (brush == null || !brush.Color.Equals(Windows.UI.Colors.Navy))
                        throw new Exception("Runtime authored style was not respected");
                    var style = new Style { TargetType = typeof(SettingsFlyout) };
                    style.Setters.Add(new Setter(Control.BackgroundProperty, new SolidColorBrush(Windows.UI.Colors.White)));
                    style.Setters.Add(new Setter(FrameworkElement.RequestedThemeProperty, ElementTheme.Dark));
                    flyout.Style = style;
                } else if (appearancePhase == 9) {
                    CheckAppearance(ElementTheme.Dark);
                    ((SolidColorBrush)flyoutContent.Background).Color = Windows.UI.Colors.Khaki;
                } else {
                    CheckAppearance(ElementTheme.Dark);
                    if (!((SolidColorBrush)flyout.Background).Color.Equals(Windows.UI.Colors.White))
                        throw new Exception("Explicit runtime style lost priority over content");
                }
                ++appearancePhase;
                return;
            }
            if (stage < 6 && stage % 2 == 0) {
                ++stage;
                SettingsPane.Show();
            } else if (stage < 6) {
                // Exercise the menu's own nested message loop, without global input.
                PostThreadMessage(uiThread, 0x100, (IntPtr)0x28, IntPtr.Zero);
                PostThreadMessage(uiThread, 0x100, (IntPtr)0x0D, IntPtr.Zero);
            } else {
                timer.Stop();
                if (requested != 3 || invoked != 3 || flyoutsLoaded != 3)
                    throw new Exception("Settings commands or flyouts did not refresh/invoke/load: " + flyoutsLoaded);
                flyout.Hide();
                pane.CommandsRequested -= CommandsRequested;
                File.WriteAllText("startup-passed.txt", "settings identity, UI thread, request refresh, command identity and invocation");
                Window.Current.Close();
            }
        };
        timer.Start();
    }
    private void CommandsRequested(SettingsPane sender, SettingsPaneCommandsRequestedEventArgs args) {
        if (sender != pane || GetCurrentThreadId() != uiThread) throw new Exception("Wrong settings sender/thread");
        ++requested;
        var commands = args.Request.ApplicationCommands;
        if (commands.Count != 0) throw new Exception("Stale command list");
        var command = new SettingsCommand("fixture", "Fixture settings", selected => {
            if ((string)selected.Id != "fixture" || selected.Label != "Fixture settings" || GetCurrentThreadId() != uiThread)
                throw new Exception("Wrong command identity/thread");
            ++invoked;
            if (flyout != null) flyout.Hide();
            var content = new StackPanel { Background = new SolidColorBrush(Windows.UI.Colors.White) };
            var toggle = new ToggleSwitch { Header = "Show timer", IsOn = true };
            content.Children.Add(toggle);
            flyoutContent = content;
            flyoutToggle = toggle;
            flyout = new SettingsFlyout { Title = "Fixture settings", Content = content };
            if (invoked == 2) {
                content.Background = new SolidColorBrush(Windows.UI.Colors.Black);
                flyout.RequestedTheme = ElementTheme.Dark;
            }
            Style authoredStyle = null;
            if (invoked == 3) {
                authoredStyle = new Style { TargetType = typeof(SettingsFlyout) };
                authoredStyle.Setters.Add(new Setter(Control.BackgroundProperty, new SolidColorBrush(Windows.UI.Colors.Khaki)));
                flyout.Style = authoredStyle;
            }
            flyout.Loaded += (s, e) => {
                ++flyoutsLoaded;
                var expectedTheme = invoked == 2 ? ElementTheme.Dark : ElementTheme.Light;
                if (flyout.RequestedTheme != expectedTheme) throw new Exception("Wrong default or explicit flyout theme");
                var foreground = toggle.Foreground as SolidColorBrush;
                byte expected = invoked == 2 ? (byte)255 : (byte)0;
                if (foreground == null || foreground.Color.R != expected || foreground.Color.G != expected || foreground.Color.B != expected)
                    throw new Exception("Flyout content foreground has incorrect contrast");
                var header = FindHeader(toggle);
                var headerBrush = header == null ? null : header.Foreground as SolidColorBrush;
                if (header == null || header.ActualWidth <= 0 || header.ActualHeight <= 0 || headerBrush == null ||
                    headerBrush.Color.R != expected || headerBrush.Color.G != expected || headerBrush.Color.B != expected)
                    throw new Exception("Rendered toggle header is missing or has incorrect contrast");
                var background = flyout.Background as SolidColorBrush;
                var expectedBackground = invoked == 2 ? Windows.UI.Colors.Black : invoked == 3 ? Windows.UI.Colors.Khaki : Windows.UI.Colors.White;
                if (background == null || !background.Color.Equals(expectedBackground))
                    throw new Exception("Flyout background did not follow content/authored appearance");
                if (authoredStyle != null && flyout.Style != authoredStyle) throw new Exception("Authored flyout style was replaced");
                if (flyout.ActualHeight < ((FrameworkElement)Window.Current.Content).ActualHeight - 1)
                    throw new Exception("Settings flyout used the hidden CoreWindow size");
                var position = flyout.TransformToVisual(Window.Current.Content).TransformPoint(new Windows.Foundation.Point());
                double expectedX = ((FrameworkElement)Window.Current.Content).ActualWidth - flyout.ActualWidth;
                if (Math.Abs(position.X - expectedX) > 1 || Math.Abs(position.Y) > 1)
                    throw new Exception("Settings flyout is outside the island: " + position.X + "," + position.Y);
            };
            if (invoked == 1) flyout.Show(); else flyout.ShowIndependent();
            ++stage;
        });
        commands.Add(command);
        if (commands.Count != 1 || commands[0] != command) throw new Exception("Broken settings collection");
        foreach (var item in commands) if (item != command) throw new Exception("Broken settings iterator");
    }
}
static class ManagedSettingsTest {
    [STAThread] static void Main() { Application.Start(p => new SettingsApp()); }
}
