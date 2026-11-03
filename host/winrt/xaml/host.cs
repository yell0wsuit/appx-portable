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
    }
}
