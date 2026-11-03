using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace AppxPortable.WinRT.Xaml
{
    // Each Windows.UI.Xaml wrapper owns a WPF peer; this maps peers back to their
    // wrappers, creating one for peers WPF made itself (templates, visual children).
    internal static class Peers
    {
        private static readonly ConditionalWeakTable<object, global::Windows.UI.Xaml.DependencyObject> wrappers =
            new ConditionalWeakTable<object, global::Windows.UI.Xaml.DependencyObject>();
        private static readonly Dictionary<Type, Func<object, global::Windows.UI.Xaml.DependencyObject>> factories =
            new Dictionary<Type, Func<object, global::Windows.UI.Xaml.DependencyObject>>();
        private static readonly Dictionary<Type, Type> peerTypes = new Dictionary<Type, Type>();

        static Peers() { WpfWrappers.Register(); }

        internal static void Register(Type wrapper, Type peer, Func<object, global::Windows.UI.Xaml.DependencyObject> factory)
        {
            factories.Add(peer, factory);
            peerTypes.Add(wrapper, peer);
        }

        // The WPF type a UWP wrapper type creates, or null if it has none.
        internal static Type PeerType(Type wrapper)
        {
            Type peer;
            return peerTypes.TryGetValue(wrapper, out peer) ? peer : null;
        }

        internal static void Attach(global::Windows.UI.Xaml.DependencyObject wrapper)
        {
            Host.EnsureDispatcher();
            wrappers.Add(wrapper.peer, wrapper);
        }

        internal static object Of(global::Windows.UI.Xaml.DependencyObject wrapper)
        {
            return wrapper == null ? null : wrapper.peer;
        }

        internal static bool IsPeer(object value)
        {
            if (value == null) return false;
            global::Windows.UI.Xaml.DependencyObject known;
            if (wrappers.TryGetValue(value, out known)) return true;
            for (Type type = value.GetType(); type != null; type = type.BaseType)
                if (factories.ContainsKey(type)) return true;
            return false;
        }

        // The wrapper of a peer: the one it was created with, else one of the nearest
        // registered WPF base type, kept for the peer's lifetime.
        internal static global::Windows.UI.Xaml.DependencyObject Wrap(object peer)
        {
            if (peer == null) return null;
            global::Windows.UI.Xaml.DependencyObject wrapper;
            if (wrappers.TryGetValue(peer, out wrapper)) return wrapper;
            Host.EnsureDispatcher();
            for (Type type = peer.GetType(); type != null; type = type.BaseType)
            {
                Func<object, global::Windows.UI.Xaml.DependencyObject> factory;
                if (!factories.TryGetValue(type, out factory)) continue;
                wrapper = factory(peer);
                wrappers.Add(peer, wrapper);
                return wrapper;
            }
            throw new NotSupportedException("No UWP wrapper for " + peer.GetType().FullName);
        }

        // WinRT XAML objects belong to their thread: RPC_E_WRONG_THREAD elsewhere.
        internal static void CheckThread(object peer)
        {
            var owner = peer as System.Windows.Threading.DispatcherObject;
            if (owner != null && !owner.CheckAccess())
                throw new COMException("The XAML object belongs to another thread.", unchecked((int)0x8001010e));
        }
    }

    // Distinct peers keep wrapper lookup unambiguous where UWP types share a WPF type.
    internal sealed class PagePeer : System.Windows.Controls.UserControl { }
    internal sealed class FramePeer : System.Windows.Controls.ContentControl { }
    // Placeholders for shim-backed services; they hold a place in the tree.
    internal abstract class NativeHostPeer : System.Windows.FrameworkElement { }
    internal sealed class MediaElementPeer : NativeHostPeer { }
    internal sealed class SwapChainPanelPeer : NativeHostPeer { }
}
