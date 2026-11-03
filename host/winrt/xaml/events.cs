using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;

using AppxPortable.WinRT.Xaml;

namespace Windows.UI.Xaml
{
    public partial class RoutedEventArgs
    {
        private readonly object originalSource;

        public RoutedEventArgs() { }

        internal RoutedEventArgs(object originalSource) { this.originalSource = originalSource; }

        public object OriginalSource { get { return originalSource; } }
    }

    public partial class FrameworkElement
    {
        private Dictionary<long, Delegate> loaded, unloaded;

        public EventRegistrationToken add_Loaded(RoutedEventHandler handler)
        {
            return Routed(ref loaded, handler, global::System.Windows.FrameworkElement.LoadedEvent);
        }

        public void remove_Loaded(EventRegistrationToken token)
        {
            Unrouted(loaded, token, global::System.Windows.FrameworkElement.LoadedEvent);
        }

        public EventRegistrationToken add_Unloaded(RoutedEventHandler handler)
        {
            return Routed(ref unloaded, handler, global::System.Windows.FrameworkElement.UnloadedEvent);
        }

        public void remove_Unloaded(EventRegistrationToken token)
        {
            Unrouted(unloaded, token, global::System.Windows.FrameworkElement.UnloadedEvent);
        }

        // A UWP handler on a WPF routed event: sender and source are wrappers.
        internal EventRegistrationToken Routed(ref Dictionary<long, Delegate> table, RoutedEventHandler handler, global::System.Windows.RoutedEvent routed)
        {
            if (handler == null) throw new ArgumentNullException("handler");
            global::System.Windows.RoutedEventHandler wpf = (sender, args) =>
                Host.Callback(() => handler(this, new RoutedEventArgs(Values.FromWpf(args.OriginalSource, typeof(object)))));
            Element.AddHandler(routed, wpf);
            return Tokens.Add(ref table, wpf);
        }

        internal void Unrouted(Dictionary<long, Delegate> table, EventRegistrationToken token, global::System.Windows.RoutedEvent routed)
        {
            CheckThread();
            var wpf = Tokens.Remove(table, token);
            if (wpf != null) Element.RemoveHandler(routed, wpf);
        }
    }
}

namespace Windows.UI.Xaml.Controls.Primitives
{
    public partial class ButtonBase
    {
        private Dictionary<long, Delegate> clicks;
        public EventRegistrationToken add_Click(RoutedEventHandler handler)
        {
            return Routed(ref clicks, handler, global::System.Windows.Controls.Primitives.ButtonBase.ClickEvent);
        }
        public void remove_Click(EventRegistrationToken token)
        {
            Unrouted(clicks, token, global::System.Windows.Controls.Primitives.ButtonBase.ClickEvent);
        }
    }
}

namespace AppxPortable.WinRT.Xaml
{
    // WinRT events on WPF-backed wrappers: add returns a token, remove takes it.
    internal static class Tokens
    {
        private static long next;

        internal static EventRegistrationToken Add(ref Dictionary<long, Delegate> table, Delegate handler)
        {
            if (table == null) table = new Dictionary<long, Delegate>();
            long value = System.Threading.Interlocked.Increment(ref next);
            table.Add(value, handler);
            return Make(value);
        }

        internal static Delegate Remove(Dictionary<long, Delegate> table, EventRegistrationToken token)
        {
            Delegate handler;
            long value = Value(token);
            if (table == null || !table.TryGetValue(value, out handler)) return null;
            table.Remove(value);
            return handler;
        }

        // The token is the ABI's 64-bit value; its constructor is not public.
        private static EventRegistrationToken Make(long value)
        {
            IntPtr memory = Marshal.AllocHGlobal(8);
            try
            {
                Marshal.WriteInt64(memory, value);
                return (EventRegistrationToken)Marshal.PtrToStructure(memory, typeof(EventRegistrationToken));
            }
            finally { Marshal.FreeHGlobal(memory); }
        }

        private static long Value(EventRegistrationToken token)
        {
            IntPtr memory = Marshal.AllocHGlobal(8);
            try
            {
                Marshal.StructureToPtr(token, memory, false);
                return Marshal.ReadInt64(memory);
            }
            finally { Marshal.FreeHGlobal(memory); }
        }
    }
}
