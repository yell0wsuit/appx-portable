using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

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
        private static readonly Dictionary<Type, Dictionary<string, System.Windows.DependencyProperty>> aliases = new Dictionary<Type, Dictionary<string, System.Windows.DependencyProperty>>();

        static Peers()
        {
            WpfWrappers.Register();
            Overrides.Register();
        }

        internal static void Register(Type wrapper, Type peer, Func<object, global::Windows.UI.Xaml.DependencyObject> factory)
        {
            factories.Add(peer, factory);
            peerTypes.Add(wrapper, peer);
        }

        // A UWP member that maps onto a differently named WPF member.
        internal static void Alias(Type wrapper, string uwp, System.Windows.DependencyProperty wpf)
        {
            Dictionary<string, System.Windows.DependencyProperty> names;
            if (!aliases.TryGetValue(wrapper, out names)) aliases.Add(wrapper, names = new Dictionary<string, System.Windows.DependencyProperty>(StringComparer.Ordinal));
            names.Add(uwp, wpf);
        }

        // The WPF names of a wrapper's members that differ, along its base types.
        internal static Dictionary<string, System.Windows.DependencyProperty> Aliases(Type wrapper)
        {
            var result = new Dictionary<string, System.Windows.DependencyProperty>(StringComparer.Ordinal);
            for (Type type = wrapper; type != null; type = type.BaseType)
            {
                Dictionary<string, System.Windows.DependencyProperty> names;
                if (aliases.TryGetValue(type, out names))
                    foreach (var pair in names) if (!result.ContainsKey(pair.Key)) result.Add(pair.Key, pair.Value);
            }
            return result;
        }

        // UWP wrapper types and the WPF types they create.
        internal static IEnumerable<KeyValuePair<Type, Type>> PeerTypes { get { return peerTypes; } }

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
            Overrides.Check(wrapper.GetType());
        }

        // The wrapper a peer already has, without creating one.
        internal static global::Windows.UI.Xaml.DependencyObject Existing(object peer)
        {
            global::Windows.UI.Xaml.DependencyObject wrapper;
            return peer != null && wrappers.TryGetValue(peer, out wrapper) ? wrapper : null;
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
    }

    // Distinct peers keep wrapper lookup unambiguous where UWP types share a WPF type.
    // They are public because XAML creates only types visible to it.
    // WPF text typography backs the initial RichTextBlock surface; Blocks and overflow
    // remain explicit completion stubs until their document mapping is implemented.
    public sealed class RichTextPeer : System.Windows.Controls.TextBlock
    {
        public bool IsTextSelectionEnabled
        {
            get { return false; }
            set { if (value) throw Errors.Unimplemented("Windows.UI.Xaml.Controls.RichTextBlock.IsTextSelectionEnabled"); }
        }
    }
    public sealed class PagePeer : System.Windows.Controls.UserControl { }
    public sealed class FramePeer : System.Windows.Controls.ContentControl { }
    // Placeholders for shim-backed services; they hold a place in the tree.
    public abstract class NativeHostPeer : System.Windows.FrameworkElement { internal NativeHostPeer() { } }
    public sealed class MediaElementPeer : NativeHostPeer
    {
        // Source templates can describe these services now; realizing them requires
        // the native playback bridge in Plan 2, with an explicit failure until then.
        public bool AutoPlay
        {
            get { throw Errors.Unimplemented("Windows.UI.Xaml.Controls.MediaElement.AutoPlay"); }
            set { throw Errors.Unimplemented("Windows.UI.Xaml.Controls.MediaElement.AutoPlay"); }
        }
        public string AudioCategory
        {
            get { throw Errors.Unimplemented("Windows.UI.Xaml.Controls.MediaElement.AudioCategory"); }
            set { throw Errors.Unimplemented("Windows.UI.Xaml.Controls.MediaElement.AudioCategory"); }
        }
    }
    public sealed class ItemsPresenterPeer : System.Windows.Controls.ItemsPresenter
    {
        public object Header
        {
            get { throw Errors.Unimplemented("Windows.UI.Xaml.Controls.ItemsPresenter.Header"); }
            set { throw Errors.Unimplemented("Windows.UI.Xaml.Controls.ItemsPresenter.Header"); }
        }
        public System.Windows.DataTemplate HeaderTemplate
        {
            get { throw Errors.Unimplemented("Windows.UI.Xaml.Controls.ItemsPresenter.HeaderTemplate"); }
            set { throw Errors.Unimplemented("Windows.UI.Xaml.Controls.ItemsPresenter.HeaderTemplate"); }
        }
        public System.Windows.Thickness Padding
        {
            get { throw Errors.Unimplemented("Windows.UI.Xaml.Controls.ItemsPresenter.Padding"); }
            set { throw Errors.Unimplemented("Windows.UI.Xaml.Controls.ItemsPresenter.Padding"); }
        }
    }
    public sealed class SwapChainPanelPeer : NativeHostPeer { }
}
