using System;
using System.Collections.Generic;
using System.Runtime.InteropServices.WindowsRuntime;
using AppxPortable.WinRT;
using AppxPortable.WinRT.Xaml;

namespace AppxPortable.WinRT.Xaml
{
    // The window's WPF root: its content, then an overlay where open popups draw.
    // WPF's own Popup opens a separate HWND, which the composited frame never
    // shows, so a UWP Popup places its child in this overlay instead.
    internal static partial class Host
    {
        private static System.Windows.Controls.Grid layers;
        private static System.Windows.Controls.Canvas overlay;

        internal static System.Windows.Controls.Grid Layers
        {
            get
            {
                if (layers != null) return layers;
                EnsureDispatcher();
                layers = new System.Windows.Controls.Grid();
                overlay = new System.Windows.Controls.Canvas();
                // Light dismiss: while such a popup is open the overlay takes the
                // presses that land outside every popup child.
                overlay.MouseDown += (s, e) => { if (e.OriginalSource == overlay) PopupPeer.DismissLight(); };
                layers.Children.Add(overlay);
                return layers;
            }
        }

        internal static System.Windows.Controls.Canvas Overlay { get { var root = Layers; return overlay; } }

        internal static void ShowContent(System.Windows.UIElement content)
        {
            var root = Layers;
            while (root.Children.Count > 1) root.Children.RemoveAt(0);
            if (content != null) root.Children.Insert(0, content);
        }
    }

    [System.Windows.Markup.ContentProperty("Child")]
    public sealed class PopupPeer : System.Windows.FrameworkElement
    {
        private static readonly List<PopupPeer> open = new List<PopupPeer>();

        public static readonly System.Windows.DependencyProperty ChildProperty = System.Windows.DependencyProperty.Register("Child",
            typeof(System.Windows.UIElement), typeof(PopupPeer), new System.Windows.PropertyMetadata(null, (d, e) => ((PopupPeer)d).ChildChanged(e)));
        public static readonly System.Windows.DependencyProperty IsOpenProperty = System.Windows.DependencyProperty.Register("IsOpen",
            typeof(bool), typeof(PopupPeer), new System.Windows.PropertyMetadata(false, (d, e) => ((PopupPeer)d).OpenChanged((bool)e.NewValue)));
        public static readonly System.Windows.DependencyProperty HorizontalOffsetProperty = Offset("HorizontalOffset");
        public static readonly System.Windows.DependencyProperty VerticalOffsetProperty = Offset("VerticalOffset");
        public static readonly System.Windows.DependencyProperty IsLightDismissEnabledProperty = System.Windows.DependencyProperty.Register("IsLightDismissEnabled",
            typeof(bool), typeof(PopupPeer), new System.Windows.PropertyMetadata(false, (d, e) => UpdateDismissal()));

        public System.Windows.UIElement Child { get { return (System.Windows.UIElement)GetValue(ChildProperty); } set { SetValue(ChildProperty, value); } }
        public bool IsOpen { get { return (bool)GetValue(IsOpenProperty); } set { SetValue(IsOpenProperty, value); } }
        public double HorizontalOffset { get { return (double)GetValue(HorizontalOffsetProperty); } set { SetValue(HorizontalOffsetProperty, value); } }
        public double VerticalOffset { get { return (double)GetValue(VerticalOffsetProperty); } set { SetValue(VerticalOffsetProperty, value); } }
        public bool IsLightDismissEnabled { get { return (bool)GetValue(IsLightDismissEnabledProperty); } set { SetValue(IsLightDismissEnabledProperty, value); } }

        internal event EventHandler Opened, Closed;

        public PopupPeer()
        {
            // An in-tree popup is positioned relative to where it sits.
            LayoutUpdated += (s, e) => { if (IsOpen) Place(); };
        }

        private static System.Windows.DependencyProperty Offset(string name)
        {
            return System.Windows.DependencyProperty.Register(name, typeof(double), typeof(PopupPeer),
                new System.Windows.PropertyMetadata(0.0, (d, e) => { var popup = (PopupPeer)d; if (popup.IsOpen) popup.Place(); }));
        }

        private void ChildChanged(System.Windows.DependencyPropertyChangedEventArgs e)
        {
            if (!IsOpen) return;
            var overlay = Host.Overlay;
            if (e.OldValue != null) overlay.Children.Remove((System.Windows.UIElement)e.OldValue);
            if (e.NewValue != null) { overlay.Children.Add((System.Windows.UIElement)e.NewValue); Place(); }
        }

        private void OpenChanged(bool value)
        {
            var overlay = Host.Overlay;
            var child = Child;
            if (value)
            {
                open.Add(this);
                if (child != null && !overlay.Children.Contains(child)) { overlay.Children.Add(child); Place(); }
            }
            else
            {
                open.Remove(this);
                if (child != null) overlay.Children.Remove(child);
            }
            UpdateDismissal();
            var raised = value ? Opened : Closed;
            if (raised != null) raised(this, EventArgs.Empty);
        }

        private void Place()
        {
            var child = Child;
            if (child == null) return;
            double x = HorizontalOffset, y = VerticalOffset;
            var root = Host.Layers;
            // In the window's tree, offsets are from the popup's own position.
            if (IsVisible && root.IsAncestorOf(this))
            {
                var at = TransformToAncestor(root).Transform(new System.Windows.Point());
                x += at.X; y += at.Y;
            }
            System.Windows.Controls.Canvas.SetLeft(child, x);
            System.Windows.Controls.Canvas.SetTop(child, y);
        }

        private static void UpdateDismissal()
        {
            bool light = open.Exists(p => p.IsLightDismissEnabled);
            Host.Overlay.Background = light ? System.Windows.Media.Brushes.Transparent : null;
        }

        internal static void DismissLight()
        {
            foreach (var popup in open.ToArray()) if (popup.IsLightDismissEnabled) popup.IsOpen = false;
        }
    }
}

namespace Windows.UI.Xaml.Controls.Primitives
{
    public sealed partial class Popup
    {
        private Dictionary<long, Delegate> opened, closed;
        private PopupPeer Overlay { get { return (PopupPeer)Values.Dependency(this); } }

        public EventRegistrationToken add_Opened(EventHandler<object> handler) { return Add(ref opened, handler, true); }
        public void remove_Opened(EventRegistrationToken token) { Remove(opened, token, true); }
        public EventRegistrationToken add_Closed(EventHandler<object> handler) { return Add(ref closed, handler, false); }
        public void remove_Closed(EventRegistrationToken token) { Remove(closed, token, false); }

        private EventRegistrationToken Add(ref Dictionary<long, Delegate> table, EventHandler<object> handler, bool opening)
        {
            if (handler == null) throw new ArgumentNullException("handler");
            var peer = Overlay;
            EventHandler callback = (s, e) => Host.Callback(() => handler(this, null));
            if (opening) peer.Opened += callback; else peer.Closed += callback;
            return Tokens.Add(ref table, callback);
        }

        private void Remove(Dictionary<long, Delegate> table, EventRegistrationToken token, bool opening)
        {
            var peer = Overlay;
            var callback = Tokens.Remove(table, token) as EventHandler;
            if (callback == null) return;
            if (opening) peer.Opened -= callback; else peer.Closed -= callback;
        }
    }
}
