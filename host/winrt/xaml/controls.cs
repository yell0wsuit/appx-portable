using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace AppxPortable.WinRT.Xaml
{
    public sealed class ToggleSwitchPeer : CheckBox { }

    // WPF does not provide swipe selection or zoom. Their disabled states are
    // supported; enabling them remains an explicitly named completion failure.
    public sealed class ListViewPeer : ListView
    {
        public static readonly DependencyProperty IsSwipeEnabledProperty = DependencyProperty.Register("IsSwipeEnabled", typeof(bool), typeof(ListViewPeer),
            new PropertyMetadata(false, (d, e) => { if ((bool)e.NewValue) throw Errors.Unimplemented("Windows.UI.Xaml.Controls.ListViewBase.IsSwipeEnabled"); }));
        public bool IsSwipeEnabled { get { return (bool)GetValue(IsSwipeEnabledProperty); } set { SetValue(IsSwipeEnabledProperty, value); } }
    }

    public sealed class ScrollViewerPeer : ScrollViewer
    {
        public ScrollViewerPeer()
        {
            RequestBringIntoView += (s, e) => { if (!ScrollProperties.GetBringIntoViewOnFocusChange(this)) e.Handled = true; };
        }
    }

    public static class ScrollProperties
    {
        public static readonly DependencyProperty HorizontalScrollModeProperty = Mode("HorizontalScrollMode", false);
        public static readonly DependencyProperty VerticalScrollModeProperty = Mode("VerticalScrollMode", true);
        public static readonly DependencyProperty ZoomModeProperty = DependencyProperty.RegisterAttached("ZoomMode", typeof(global::Windows.UI.Xaml.Controls.ZoomMode), typeof(ScrollProperties),
            new PropertyMetadata(global::Windows.UI.Xaml.Controls.ZoomMode.Disabled, (d, e) =>
            { if ((global::Windows.UI.Xaml.Controls.ZoomMode)e.NewValue != global::Windows.UI.Xaml.Controls.ZoomMode.Disabled) throw Errors.Unimplemented("Windows.UI.Xaml.Controls.ScrollViewer.ZoomMode"); }));
        public static readonly DependencyProperty BringIntoViewOnFocusChangeProperty = DependencyProperty.RegisterAttached("BringIntoViewOnFocusChange", typeof(bool), typeof(ScrollProperties), new PropertyMetadata(true));
        public static readonly DependencyProperty IsHorizontalRailEnabledProperty = Disabled("IsHorizontalRailEnabled");
        public static readonly DependencyProperty IsVerticalRailEnabledProperty = Disabled("IsVerticalRailEnabled");
        public static readonly DependencyProperty IsHorizontalScrollChainingEnabledProperty = Disabled("IsHorizontalScrollChainingEnabled");
        public static readonly DependencyProperty IsVerticalScrollChainingEnabledProperty = Disabled("IsVerticalScrollChainingEnabled");

        static ScrollProperties()
        {
            foreach (var field in typeof(ScrollProperties).GetFields(System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Static))
                ((DependencyProperty)field.GetValue(null)).AddOwner(typeof(ScrollViewer));
        }

        private static DependencyProperty Disabled(string name)
        {
            return DependencyProperty.RegisterAttached(name, typeof(bool), typeof(ScrollProperties), new PropertyMetadata(false, (d, e) =>
            { if ((bool)e.NewValue) throw Errors.Unimplemented("Windows.UI.Xaml.Controls.ScrollViewer." + name); }));
        }

        private static DependencyProperty Mode(string name, bool vertical)
        {
            return DependencyProperty.RegisterAttached(name, typeof(global::Windows.UI.Xaml.Controls.ScrollMode), typeof(ScrollProperties),
                new PropertyMetadata(global::Windows.UI.Xaml.Controls.ScrollMode.Auto, (d, e) =>
                {
                    var property = vertical ? ScrollViewer.VerticalScrollBarVisibilityProperty : ScrollViewer.HorizontalScrollBarVisibilityProperty;
                    var mode = (global::Windows.UI.Xaml.Controls.ScrollMode)e.NewValue;
                    if (mode == global::Windows.UI.Xaml.Controls.ScrollMode.Disabled) d.SetValue(property, ScrollBarVisibility.Disabled);
                    else if ((ScrollBarVisibility)d.GetValue(property) == ScrollBarVisibility.Disabled) d.SetValue(property, ScrollBarVisibility.Auto);
                }));
        }

        public static global::Windows.UI.Xaml.Controls.ScrollMode GetHorizontalScrollMode(DependencyObject d) { return (global::Windows.UI.Xaml.Controls.ScrollMode)d.GetValue(HorizontalScrollModeProperty); }
        public static void SetHorizontalScrollMode(DependencyObject d, global::Windows.UI.Xaml.Controls.ScrollMode value) { d.SetValue(HorizontalScrollModeProperty, value); }
        public static global::Windows.UI.Xaml.Controls.ScrollMode GetVerticalScrollMode(DependencyObject d) { return (global::Windows.UI.Xaml.Controls.ScrollMode)d.GetValue(VerticalScrollModeProperty); }
        public static void SetVerticalScrollMode(DependencyObject d, global::Windows.UI.Xaml.Controls.ScrollMode value) { d.SetValue(VerticalScrollModeProperty, value); }
        public static global::Windows.UI.Xaml.Controls.ZoomMode GetZoomMode(DependencyObject d) { return (global::Windows.UI.Xaml.Controls.ZoomMode)d.GetValue(ZoomModeProperty); }
        public static void SetZoomMode(DependencyObject d, global::Windows.UI.Xaml.Controls.ZoomMode value) { d.SetValue(ZoomModeProperty, value); }
        public static bool GetBringIntoViewOnFocusChange(DependencyObject d) { return (bool)d.GetValue(BringIntoViewOnFocusChangeProperty); }
        public static void SetBringIntoViewOnFocusChange(DependencyObject d, bool value) { d.SetValue(BringIntoViewOnFocusChangeProperty, value); }
        public static bool GetIsHorizontalRailEnabled(DependencyObject d) { return (bool)d.GetValue(IsHorizontalRailEnabledProperty); }
        public static void SetIsHorizontalRailEnabled(DependencyObject d, bool value) { d.SetValue(IsHorizontalRailEnabledProperty, value); }
        public static bool GetIsVerticalRailEnabled(DependencyObject d) { return (bool)d.GetValue(IsVerticalRailEnabledProperty); }
        public static void SetIsVerticalRailEnabled(DependencyObject d, bool value) { d.SetValue(IsVerticalRailEnabledProperty, value); }
        public static bool GetIsHorizontalScrollChainingEnabled(DependencyObject d) { return (bool)d.GetValue(IsHorizontalScrollChainingEnabledProperty); }
        public static void SetIsHorizontalScrollChainingEnabled(DependencyObject d, bool value) { d.SetValue(IsHorizontalScrollChainingEnabledProperty, value); }
        public static bool GetIsVerticalScrollChainingEnabled(DependencyObject d) { return (bool)d.GetValue(IsVerticalScrollChainingEnabledProperty); }
        public static void SetIsVerticalScrollChainingEnabled(DependencyObject d, bool value) { d.SetValue(IsVerticalScrollChainingEnabledProperty, value); }
    }

    public static class CompositeProperties
    {
        public static readonly DependencyProperty ScaleXProperty = Property("ScaleX", 1);
        public static readonly DependencyProperty ScaleYProperty = Property("ScaleY", 1);
        public static readonly DependencyProperty SkewXProperty = Property("SkewX", 0);
        public static readonly DependencyProperty SkewYProperty = Property("SkewY", 0);
        public static readonly DependencyProperty RotationProperty = Property("Rotation", 0);
        public static readonly DependencyProperty TranslateXProperty = Property("TranslateX", 0);
        public static readonly DependencyProperty TranslateYProperty = Property("TranslateY", 0);
        public static readonly DependencyProperty CenterXProperty = Property("CenterX", 0);
        public static readonly DependencyProperty CenterYProperty = Property("CenterY", 0);

        static CompositeProperties()
        {
            foreach (var field in typeof(CompositeProperties).GetFields(System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Static))
                ((DependencyProperty)field.GetValue(null)).AddOwner(typeof(MatrixTransform));
        }

        private static DependencyProperty Property(string name, double value)
        {
            return DependencyProperty.RegisterAttached(name, typeof(double), typeof(CompositeProperties), new PropertyMetadata(value, Changed));
        }

        private static void Changed(DependencyObject d, DependencyPropertyChangedEventArgs e)
        {
            var transform = (MatrixTransform)d;
            double x = GetCenterX(d), y = GetCenterY(d);
            var matrix = Matrix.Identity;
            matrix.Translate(-x, -y);
            matrix.Scale(GetScaleX(d), GetScaleY(d));
            matrix.Skew(GetSkewX(d), GetSkewY(d));
            matrix.Rotate(GetRotation(d));
            matrix.Translate(x + GetTranslateX(d), y + GetTranslateY(d));
            transform.Matrix = matrix;
        }
        public static double GetScaleX(DependencyObject d) { return (double)d.GetValue(ScaleXProperty); }
        public static void SetScaleX(DependencyObject d, double value) { d.SetValue(ScaleXProperty, value); }
        public static double GetScaleY(DependencyObject d) { return (double)d.GetValue(ScaleYProperty); }
        public static void SetScaleY(DependencyObject d, double value) { d.SetValue(ScaleYProperty, value); }
        public static double GetSkewX(DependencyObject d) { return (double)d.GetValue(SkewXProperty); }
        public static void SetSkewX(DependencyObject d, double value) { d.SetValue(SkewXProperty, value); }
        public static double GetSkewY(DependencyObject d) { return (double)d.GetValue(SkewYProperty); }
        public static void SetSkewY(DependencyObject d, double value) { d.SetValue(SkewYProperty, value); }
        public static double GetRotation(DependencyObject d) { return (double)d.GetValue(RotationProperty); }
        public static void SetRotation(DependencyObject d, double value) { d.SetValue(RotationProperty, value); }
        public static double GetTranslateX(DependencyObject d) { return (double)d.GetValue(TranslateXProperty); }
        public static void SetTranslateX(DependencyObject d, double value) { d.SetValue(TranslateXProperty, value); }
        public static double GetTranslateY(DependencyObject d) { return (double)d.GetValue(TranslateYProperty); }
        public static void SetTranslateY(DependencyObject d, double value) { d.SetValue(TranslateYProperty, value); }
        public static double GetCenterX(DependencyObject d) { return (double)d.GetValue(CenterXProperty); }
        public static void SetCenterX(DependencyObject d, double value) { d.SetValue(CenterXProperty, value); }
        public static double GetCenterY(DependencyObject d) { return (double)d.GetValue(CenterYProperty); }
        public static void SetCenterY(DependencyObject d, double value) { d.SetValue(CenterYProperty, value); }
    }

    // Explicitly approved exception: these decorative transitions load but do not
    // animate. Each unsupported effect is logged once for the process.
    internal static class ThemeEffects
    {
        private static readonly HashSet<string> reported = new HashSet<string>();
        internal static void Report(string name)
        {
            lock (reported) if (reported.Add(name)) Errors.Unimplemented("Windows.UI.Xaml.Media.Animation." + name);
        }
    }
    public class ThemeTransitionPeer : DependencyObject
    {
        public ThemeTransitionPeer() { ThemeEffects.Report(GetType().Name.Replace("Peer", "")); }
    }
    public sealed class EntranceThemeTransitionPeer : ThemeTransitionPeer
    {
        public bool IsStaggeringEnabled { get; set; }
        public static readonly DependencyProperty FromHorizontalOffsetProperty = DependencyProperty.Register("FromHorizontalOffset", typeof(double), typeof(EntranceThemeTransitionPeer), new PropertyMetadata(40.0));
        public static readonly DependencyProperty FromVerticalOffsetProperty = DependencyProperty.Register("FromVerticalOffset", typeof(double), typeof(EntranceThemeTransitionPeer), new PropertyMetadata(0.0));
        public double FromHorizontalOffset { get { return (double)GetValue(FromHorizontalOffsetProperty); } set { SetValue(FromHorizontalOffsetProperty, value); } }
        public double FromVerticalOffset { get { return (double)GetValue(FromVerticalOffsetProperty); } set { SetValue(FromVerticalOffsetProperty, value); } }
    }
    public sealed class ContentThemeTransitionPeer : ThemeTransitionPeer { }
    public sealed class AddDeleteThemeTransitionPeer : ThemeTransitionPeer { }
    public sealed class ReorderThemeTransitionPeer : ThemeTransitionPeer { }
    public sealed class TransitionCollectionPeer : System.Collections.ObjectModel.Collection<ThemeTransitionPeer>
    {
        private readonly System.Windows.Threading.Dispatcher dispatcher = System.Windows.Threading.Dispatcher.CurrentDispatcher;
        internal void CheckThread()
        {
            if (!dispatcher.CheckAccess()) throw new System.Runtime.InteropServices.COMException("The XAML object belongs to another thread.", unchecked((int)0x8001010e));
        }
    }
    public sealed class RepositionThemeAnimationPeer : Storyboard
    {
        public RepositionThemeAnimationPeer() { ThemeEffects.Report("RepositionThemeAnimation"); }
        public static readonly DependencyProperty FromHorizontalOffsetProperty = DependencyProperty.Register("FromHorizontalOffset", typeof(double), typeof(RepositionThemeAnimationPeer));
        public double FromHorizontalOffset { get { return (double)GetValue(FromHorizontalOffsetProperty); } set { SetValue(FromHorizontalOffsetProperty, value); } }
        public string TargetName { get { return Storyboard.GetTargetName(this); } set { Storyboard.SetTargetName(this, value); } }
        protected override Freezable CreateInstanceCore() { return new RepositionThemeAnimationPeer(); }
    }

    public static class CompatibilityProperties
    {
        public static readonly DependencyProperty ChildrenTransitionsProperty = Transition("ChildrenTransitions");
        public static readonly DependencyProperty ItemContainerTransitionsProperty = Transition("ItemContainerTransitions");
        public static readonly DependencyProperty ContentTransitionsProperty = Transition("ContentTransitions");
        public static readonly DependencyProperty HeaderTransitionsProperty = Transition("HeaderTransitions");
        public static readonly DependencyProperty TransitionsProperty = Transition("Transitions");
        public static readonly DependencyProperty ChildTransitionsProperty = Transition("ChildTransitions");
        private static DependencyProperty Transition(string name)
        {
            var property = DependencyProperty.RegisterAttached(name, typeof(TransitionCollectionPeer), typeof(CompatibilityProperties));
            property.AddOwner(typeof(UIElement));
            return property;
        }
        internal static void Initialize()
        {
            System.Runtime.CompilerServices.RuntimeHelpers.RunClassConstructor(typeof(CompatibilityProperties).TypeHandle);
            System.Runtime.CompilerServices.RuntimeHelpers.RunClassConstructor(typeof(ScrollProperties).TypeHandle);
            System.Runtime.CompilerServices.RuntimeHelpers.RunClassConstructor(typeof(CompositeProperties).TypeHandle);
            // UWP puts keyboard navigation on Control; WPF exposes attached DPs.
            System.Windows.Input.KeyboardNavigation.TabNavigationProperty.AddOwner(typeof(Control));
            System.Windows.Documents.TextElement.ForegroundProperty.AddOwner(typeof(ContentPresenter));
        }
        public static TransitionCollectionPeer GetChildrenTransitions(DependencyObject d) { return (TransitionCollectionPeer)d.GetValue(ChildrenTransitionsProperty); }
        public static void SetChildrenTransitions(DependencyObject d, TransitionCollectionPeer value) { d.SetValue(ChildrenTransitionsProperty, value); }
        public static TransitionCollectionPeer GetItemContainerTransitions(DependencyObject d) { return (TransitionCollectionPeer)d.GetValue(ItemContainerTransitionsProperty); }
        public static void SetItemContainerTransitions(DependencyObject d, TransitionCollectionPeer value) { d.SetValue(ItemContainerTransitionsProperty, value); }
        public static TransitionCollectionPeer GetContentTransitions(DependencyObject d) { return (TransitionCollectionPeer)d.GetValue(ContentTransitionsProperty); }
        public static void SetContentTransitions(DependencyObject d, TransitionCollectionPeer value) { d.SetValue(ContentTransitionsProperty, value); }
        public static TransitionCollectionPeer GetHeaderTransitions(DependencyObject d) { return (TransitionCollectionPeer)d.GetValue(HeaderTransitionsProperty); }
        public static void SetHeaderTransitions(DependencyObject d, TransitionCollectionPeer value) { d.SetValue(HeaderTransitionsProperty, value); }
        public static TransitionCollectionPeer GetChildTransitions(DependencyObject d) { return (TransitionCollectionPeer)d.GetValue(ChildTransitionsProperty); }
        public static void SetChildTransitions(DependencyObject d, TransitionCollectionPeer value) { d.SetValue(ChildTransitionsProperty, value); }
        public static TransitionCollectionPeer GetTransitions(DependencyObject d) { return (TransitionCollectionPeer)d.GetValue(TransitionsProperty); }
        public static void SetTransitions(DependencyObject d, TransitionCollectionPeer value) { d.SetValue(TransitionsProperty, value); }
    }
}

namespace Windows.UI.Xaml.Media.Animation
{
    public sealed partial class TransitionCollection : IList<Transition>
    {
        internal readonly AppxPortable.WinRT.Xaml.TransitionCollectionPeer Peer;
        private static readonly global::System.Runtime.CompilerServices.ConditionalWeakTable<AppxPortable.WinRT.Xaml.TransitionCollectionPeer, TransitionCollection> wrappers =
            new global::System.Runtime.CompilerServices.ConditionalWeakTable<AppxPortable.WinRT.Xaml.TransitionCollectionPeer, TransitionCollection>();
        public TransitionCollection() : this(new AppxPortable.WinRT.Xaml.TransitionCollectionPeer()) { wrappers.Add(Peer, this); }
        private TransitionCollection(AppxPortable.WinRT.Xaml.TransitionCollectionPeer peer) { Peer = peer; }
        internal static TransitionCollection Of(AppxPortable.WinRT.Xaml.TransitionCollectionPeer peer) { return wrappers.GetValue(peer, p => new TransitionCollection(p)); }
        private AppxPortable.WinRT.Xaml.TransitionCollectionPeer Items { get { Peer.CheckThread(); return Peer; } }
        public Transition this[int index] { get { return (Transition)AppxPortable.WinRT.Xaml.Peers.Wrap(Items[index]); } set { Items[index] = (AppxPortable.WinRT.Xaml.ThemeTransitionPeer)AppxPortable.WinRT.Xaml.Peers.Of(value); } }
        public int Count { get { return Items.Count; } }
        public bool IsReadOnly { get { Peer.CheckThread(); return false; } }
        public void Add(Transition value) { Items.Add((AppxPortable.WinRT.Xaml.ThemeTransitionPeer)AppxPortable.WinRT.Xaml.Peers.Of(value)); }
        public void Clear() { Items.Clear(); }
        public bool Contains(Transition value) { return IndexOf(value) >= 0; }
        public int IndexOf(Transition value) { return Items.IndexOf((AppxPortable.WinRT.Xaml.ThemeTransitionPeer)AppxPortable.WinRT.Xaml.Peers.Of(value)); }
        public void Insert(int index, Transition value) { Items.Insert(index, (AppxPortable.WinRT.Xaml.ThemeTransitionPeer)AppxPortable.WinRT.Xaml.Peers.Of(value)); }
        public bool Remove(Transition value) { return Items.Remove((AppxPortable.WinRT.Xaml.ThemeTransitionPeer)AppxPortable.WinRT.Xaml.Peers.Of(value)); }
        public void RemoveAt(int index) { Items.RemoveAt(index); }
        public void CopyTo(Transition[] array, int index) { foreach (var value in this) array[index++] = value; }
        public IEnumerator<Transition> GetEnumerator() { for (int i = 0; i < Count; ++i) yield return this[i]; }
        global::System.Collections.IEnumerator global::System.Collections.IEnumerable.GetEnumerator() { return GetEnumerator(); }
    }
}
