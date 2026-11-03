using System.Windows.Threading;

namespace AppxPortable.WinRT.Xaml
{
    // WPF backs Windows.UI.Xaml for managed games under Wine. Its objects live on the
    // shim's view thread, whose message loop also pumps WPF's dispatcher.
    internal static class Host
    {
        private static Dispatcher dispatcher;

        internal static Dispatcher Dispatcher { get { EnsureDispatcher(); return dispatcher; } }

        // Binds WPF to the calling (view) thread on first use; the shim's loop pumps it.
        internal static void EnsureDispatcher()
        {
            if (dispatcher == null) dispatcher = Dispatcher.CurrentDispatcher;
        }

        // The window's content and the WPF tree the compositor draws.
        internal static global::Windows.UI.Xaml.UIElement Content { get; private set; }
        internal static System.Windows.UIElement Root { get; private set; }

        internal static void SetContent(global::Windows.UI.Xaml.UIElement content)
        {
            Content = content;
            Root = (System.Windows.UIElement)Peers.Of(content);
        }
    }
}

namespace Windows.UI.Xaml
{
    // The native Window keeps bounds, activation and CoreWindow; its content is WPF.
    public sealed partial class Window
    {
        public UIElement Content
        {
            get { return AppxPortable.WinRT.Xaml.Host.Content; }
            set { AppxPortable.WinRT.Xaml.Host.SetContent(value); }
        }
    }
}
