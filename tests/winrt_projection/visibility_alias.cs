using System;

using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
class VisibilityAliasApp : Application
{
    protected override void OnLaunched(Windows.ApplicationModel.Activation.LaunchActivatedEventArgs args)
    {
        UIElement element = new Frame();
        Window.Current.Content = element;
        element.Visibility = Visibility.Collapsed;
        if (element.Visibility != Visibility.Collapsed) throw new Exception("WinRT enum setter did not forward");
        try { element.Visibility = (Visibility)(-1); throw new Exception("invalid WinRT enum accepted"); }
        catch (ArgumentException error) { if (error.HResult != unchecked((int)0x80070057)) throw; }
        element.Visibility = Visibility.Visible;
        if (element.Visibility != Visibility.Visible) throw new Exception("WinRT setter recovery");
        Console.WriteLine("PASS: WinRT put_Visibility forwards to the WPF peer");
        Exit();
    }
}
class VisibilityAliasTest
{
    [STAThread]
    static int Main()
    {
        try { Application.Start(p => new VisibilityAliasApp()); return 1; }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
