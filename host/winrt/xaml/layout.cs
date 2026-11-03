using System;
using System.Collections.Generic;
using System.Runtime.InteropServices.WindowsRuntime;
using AppxPortable.WinRT.Xaml;
using Windows.UI.Xaml.Input;

namespace Windows.UI.Xaml
{
    public partial class DependencyObject
    {
        // XAML objects belong to the view's CoreDispatcher: WPF and the shim share its thread.
        public global::Windows.UI.Core.CoreDispatcher Dispatcher { get { return Host.CoreDispatcher; } }
    }

    public partial class UIElement
    {
        private Dictionary<long, Delegate> keyDowns, keyUps;

        public void UpdateLayout() { InputPeer.UpdateLayout(); }
        public void InvalidateMeasure() { InputPeer.InvalidateMeasure(); }
        public void InvalidateArrange() { InputPeer.InvalidateArrange(); }
        public void Measure(global::Windows.Foundation.Size availableSize) { InputPeer.Measure(new global::System.Windows.Size(availableSize.Width, availableSize.Height)); }
        public void Arrange(global::Windows.Foundation.Rect finalRect)
        {
            InputPeer.Arrange(new global::System.Windows.Rect(finalRect.X, finalRect.Y, finalRect.Width, finalRect.Height));
        }
        public global::Windows.Foundation.Size DesiredSize
        {
            get { var size = InputPeer.DesiredSize; return new global::Windows.Foundation.Size(size.Width, size.Height); }
        }
        public global::Windows.Foundation.Size RenderSize
        {
            get { var size = InputPeer.RenderSize; return new global::Windows.Foundation.Size(size.Width, size.Height); }
        }

        public EventRegistrationToken add_KeyDown(KeyEventHandler handler) { return Key(ref keyDowns, handler, global::System.Windows.UIElement.KeyDownEvent); }
        public void remove_KeyDown(EventRegistrationToken token) { RemovePointer(keyDowns, token, global::System.Windows.UIElement.KeyDownEvent); }
        public EventRegistrationToken add_KeyUp(KeyEventHandler handler) { return Key(ref keyUps, handler, global::System.Windows.UIElement.KeyUpEvent); }
        public void remove_KeyUp(EventRegistrationToken token) { RemovePointer(keyUps, token, global::System.Windows.UIElement.KeyUpEvent); }

        private EventRegistrationToken Key(ref Dictionary<long, Delegate> table, KeyEventHandler handler, global::System.Windows.RoutedEvent routed)
        {
            if (handler == null) throw new ArgumentNullException("handler");
            var peer = InputPeer;
            global::System.Windows.Input.KeyEventHandler callback = (s, e) =>
                Host.Callback(() => handler(this, new KeyRoutedEventArgs(Values.FromWpf(e.OriginalSource, typeof(object)), e)));
            peer.AddHandler(routed, callback);
            return Tokens.Add(ref table, callback);
        }
    }

    public partial class FrameworkElement
    {
        private Dictionary<long, Delegate> sizes;

        public EventRegistrationToken add_SizeChanged(SizeChangedEventHandler handler)
        {
            if (handler == null) throw new ArgumentNullException("handler");
            global::System.Windows.SizeChangedEventHandler callback = (s, e) => Host.Callback(() => handler(this, new SizeChangedEventArgs(e)));
            Element.AddHandler(global::System.Windows.FrameworkElement.SizeChangedEvent, callback);
            return Tokens.Add(ref sizes, callback);
        }

        public void remove_SizeChanged(EventRegistrationToken token)
        {
            CheckThread();
            var callback = Tokens.Remove(sizes, token);
            if (callback != null) Element.RemoveHandler(global::System.Windows.FrameworkElement.SizeChangedEvent, callback);
        }
    }

    public sealed partial class SizeChangedEventArgs
    {
        private readonly global::System.Windows.SizeChangedEventArgs args;
        internal SizeChangedEventArgs(global::System.Windows.SizeChangedEventArgs args)
            : base(Values.FromWpf(args.OriginalSource, typeof(object))) { this.args = args; }
        public global::Windows.Foundation.Size NewSize { get { return new global::Windows.Foundation.Size(args.NewSize.Width, args.NewSize.Height); } }
        public global::Windows.Foundation.Size PreviousSize { get { return new global::Windows.Foundation.Size(args.PreviousSize.Width, args.PreviousSize.Height); } }
    }
}

namespace Windows.UI.Xaml.Controls
{
    public partial class ScrollViewer
    {
        private global::System.Windows.Controls.ScrollViewer Scroller { get { return (global::System.Windows.Controls.ScrollViewer)Values.Dependency(this); } }
        // WPF applies the offset on its next layout pass, as UWP's asynchronous scroll does.
        public void ScrollToHorizontalOffset(double offset) { Scroller.ScrollToHorizontalOffset(offset); }
        public void ScrollToVerticalOffset(double offset) { Scroller.ScrollToVerticalOffset(offset); }
    }

    public partial class Viewbox
    {
        public UIElement Child
        {
            get { return (UIElement)Peers.Wrap(((global::System.Windows.Controls.Viewbox)Values.Dependency(this)).Child); }
            set { ((global::System.Windows.Controls.Viewbox)Values.Dependency(this)).Child = (global::System.Windows.UIElement)Peers.Of(value); }
        }
    }
}

namespace Windows.UI.Xaml.Media
{
    public partial class SolidColorBrush
    {
        public SolidColorBrush(global::Windows.UI.Color color) : this() { Color = color; }
    }
}

namespace AppxPortable.WinRT.Xaml
{
    internal static partial class Host
    {
        private static global::Windows.UI.Core.CoreDispatcher coreDispatcher;

        // The view thread's CoreDispatcher, found on that thread and then kept for others.
        internal static global::Windows.UI.Core.CoreDispatcher CoreDispatcher
        {
            get
            {
                if (coreDispatcher == null && Dispatcher.CheckAccess())
                {
                    var window = global::Windows.UI.Core.CoreWindow.GetForCurrentThread();
                    if (window != null) coreDispatcher = window.Dispatcher;
                }
                return coreDispatcher;
            }
        }
    }
}

namespace Windows.UI.Xaml.Controls
{
    public partial class ItemsControl
    {
        public ItemContainerGenerator ItemContainerGenerator
        {
            get { return ItemContainerGenerator.Of(((global::System.Windows.Controls.ItemsControl)Values.Dependency(this)).ItemContainerGenerator); }
        }
    }

    // Maps items to their containers through WPF's generator; containers are wrappers.
    public sealed partial class ItemContainerGenerator
    {
        private static readonly global::System.Runtime.CompilerServices.ConditionalWeakTable<global::System.Windows.Controls.ItemContainerGenerator, ItemContainerGenerator> wrappers =
            new global::System.Runtime.CompilerServices.ConditionalWeakTable<global::System.Windows.Controls.ItemContainerGenerator, ItemContainerGenerator>();
        private readonly global::System.Windows.Controls.ItemContainerGenerator generator;
        private ItemContainerGenerator(global::System.Windows.Controls.ItemContainerGenerator generator) { this.generator = generator; }
        internal static ItemContainerGenerator Of(global::System.Windows.Controls.ItemContainerGenerator generator)
        {
            return generator == null ? null : wrappers.GetValue(generator, g => new ItemContainerGenerator(g));
        }
        private static global::System.Windows.DependencyObject Container(DependencyObject container)
        {
            return container == null ? null : (global::System.Windows.DependencyObject)Peers.Of(container);
        }
        public int IndexFromContainer(DependencyObject container) { return generator.IndexFromContainer(Container(container)); }
        public DependencyObject ContainerFromIndex(int index) { return (DependencyObject)Peers.Wrap(generator.ContainerFromIndex(index)); }
        public object ItemFromContainer(DependencyObject container) { return Values.FromWpf(generator.ItemFromContainer(Container(container)), typeof(object)); }
        public DependencyObject ContainerFromItem(object item) { return (DependencyObject)Peers.Wrap(generator.ContainerFromItem(Values.ToWpf(item, typeof(object)))); }
    }
}
