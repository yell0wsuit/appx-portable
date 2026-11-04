using System.Reflection;
[assembly: AssemblyVersion("255.255.255.255")]
namespace Windows.System
{
    public enum VirtualKey { Shift = 16, Escape = 27 }
    public enum ProcessorArchitecture { X86 = 0, X64 = 9, Unknown = 65535 }
}
namespace Windows.UI.Xaml.Input
{
    public delegate void KeyEventHandler(object sender, KeyRoutedEventArgs e);
    public sealed class KeyRoutedEventArgs
    {
        internal KeyRoutedEventArgs() { }
        public global::Windows.System.VirtualKey Key { get { throw new global::System.NotImplementedException(); } }
        public bool Handled { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
}
namespace Windows.ApplicationModel
{
    public struct PackageVersion { public ushort Major; public ushort Minor; public ushort Build; public ushort Revision; }
    public sealed class SuspendingEventArgs { }
    public sealed class Package
    {
        public static Package Current { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.Package.Current"); } }
        public PackageId Id { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.Package.Id"); } }
    }
    public sealed class PackageId
    {
        public string Name { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.PackageId.Name"); } }
        public string Publisher { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.PackageId.Publisher"); } }
        public string ResourceId { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.PackageId.ResourceId"); } }
        public global::Windows.System.ProcessorArchitecture Architecture { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.PackageId.Architecture"); } }
        public PackageVersion Version { get { throw new global::System.NotImplementedException(); } }
    }
}
namespace Windows.UI.Xaml
{
    public sealed class ApplicationInitializationCallbackParams { }
    public delegate void ApplicationInitializationCallback(ApplicationInitializationCallbackParams p);
    public delegate void SuspendingEventHandler(object sender, global::Windows.ApplicationModel.SuspendingEventArgs e);
    public class Application
    {
        public Application() { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application..ctor"); }
        public static void LoadComponent(object component, global::System.Uri uri) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.LoadComponent"); }
        public static void LoadComponent(object component, global::System.Uri uri, global::Windows.UI.Xaml.Controls.Primitives.ComponentResourceLocation location) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.LoadComponent"); }
        public ResourceDictionary Resources { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public static Application Current { get { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.Current"); } }
        public static void Start(ApplicationInitializationCallback callback) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.Start"); }
        public void Exit() { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.Exit"); }
        // A WinRT event as consumers bind it: token-returning add, token-taking remove.
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_Suspending(SuspendingEventHandler handler) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.add_Suspending"); }
        public void remove_Suspending(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.remove_Suspending"); }
        protected virtual void OnLaunched(global::Windows.ApplicationModel.Activation.LaunchActivatedEventArgs args) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.OnLaunched"); }
        protected virtual void OnActivated(global::Windows.ApplicationModel.Activation.IActivatedEventArgs args) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Application.OnActivated"); }
    }
}
namespace Windows.UI.Xaml
{
    public delegate void WindowSizeChangedEventHandler(object sender, global::Windows.UI.Core.WindowSizeChangedEventArgs args);
    public sealed class Window
    {
        public static Window Current { get { throw new global::System.NotImplementedException("Windows.UI.Xaml.Window.Current"); } }
        public UIElement Content
        {
            get { throw new global::System.NotImplementedException("Windows.UI.Xaml.Window.Content"); }
            set { throw new global::System.NotImplementedException("Windows.UI.Xaml.Window.Content"); }
        }
        public void Activate() { throw new global::System.NotImplementedException("Windows.UI.Xaml.Window.Activate"); }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_SizeChanged(WindowSizeChangedEventHandler handler) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Window.add_SizeChanged"); }
        public void remove_SizeChanged(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException("Windows.UI.Xaml.Window.remove_SizeChanged"); }
        public global::Windows.Foundation.Rect Bounds { get { throw new global::System.NotImplementedException(); } }
        public global::Windows.UI.Core.CoreWindow CoreWindow { get { throw new global::System.NotImplementedException(); } }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_Activated(WindowActivatedEventHandler handler) { throw new global::System.NotImplementedException(); }
        public void remove_Activated(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_VisibilityChanged(WindowVisibilityChangedEventHandler handler) { throw new global::System.NotImplementedException(); }
        public void remove_VisibilityChanged(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
    public delegate void WindowVisibilityChangedEventHandler(object sender, global::Windows.UI.Core.VisibilityChangedEventArgs e);
    public delegate void SizeChangedEventHandler(object sender, SizeChangedEventArgs e);
    public sealed class SizeChangedEventArgs : RoutedEventArgs
    {
        internal SizeChangedEventArgs() { }
        public global::Windows.Foundation.Size NewSize { get { throw new global::System.NotImplementedException(); } }
        public global::Windows.Foundation.Size PreviousSize { get { throw new global::System.NotImplementedException(); } }
    }
    public delegate void WindowActivatedEventHandler(object sender, global::Windows.UI.Core.WindowActivatedEventArgs e);
    public delegate void RoutedEventHandler(object sender, RoutedEventArgs e);
    public class DispatcherTimer
    {
        public DispatcherTimer() { throw new global::System.NotImplementedException("Windows.UI.Xaml.DispatcherTimer..ctor"); }
        public global::System.TimeSpan Interval
        {
            get { throw new global::System.NotImplementedException("Windows.UI.Xaml.DispatcherTimer.Interval"); }
            set { throw new global::System.NotImplementedException("Windows.UI.Xaml.DispatcherTimer.Interval"); }
        }
        public void Start() { throw new global::System.NotImplementedException("Windows.UI.Xaml.DispatcherTimer.Start"); }
        public void Stop() { throw new global::System.NotImplementedException("Windows.UI.Xaml.DispatcherTimer.Stop"); }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_Tick(global::System.EventHandler<object> handler) { throw new global::System.NotImplementedException("Windows.UI.Xaml.DispatcherTimer.add_Tick"); }
        public void remove_Tick(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException("Windows.UI.Xaml.DispatcherTimer.remove_Tick"); }
    }
    public delegate void PropertyChangedCallback(DependencyObject d, DependencyPropertyChangedEventArgs e);
    public sealed class DependencyPropertyChangedEventArgs
    {
        internal DependencyPropertyChangedEventArgs() { }
        public DependencyProperty Property { get { throw new global::System.NotImplementedException(); } }
        public object OldValue { get { throw new global::System.NotImplementedException(); } }
        public object NewValue { get { throw new global::System.NotImplementedException(); } }
    }
    public class PropertyMetadata
    {
        public PropertyMetadata(object defaultValue) { throw new global::System.NotImplementedException(); }
        public PropertyMetadata(object defaultValue, PropertyChangedCallback propertyChangedCallback) { throw new global::System.NotImplementedException(); }
    }
    public sealed class DependencyProperty
    {
        public static DependencyProperty RegisterAttached(string name, global::System.Type type, global::System.Type owner, PropertyMetadata metadata) { throw new global::System.NotImplementedException("Windows.UI.Xaml.DependencyProperty.RegisterAttached"); }
        public static DependencyProperty Register(string name, global::System.Type type, global::System.Type owner, PropertyMetadata metadata) { throw new global::System.NotImplementedException("Windows.UI.Xaml.DependencyProperty.Register"); }
        public static object UnsetValue { get { throw new global::System.NotImplementedException("Windows.UI.Xaml.DependencyProperty.UnsetValue"); } }
    }
    public class DependencyObject
    {
        internal DependencyObject() { }
        public object GetValue(DependencyProperty property) { throw new global::System.NotImplementedException("Windows.UI.Xaml.DependencyObject.GetValue"); }
        public void SetValue(DependencyProperty property, object value) { throw new global::System.NotImplementedException("Windows.UI.Xaml.DependencyObject.SetValue"); }
        public void ClearValue(DependencyProperty property) { throw new global::System.NotImplementedException("Windows.UI.Xaml.DependencyObject.ClearValue"); }
        public object ReadLocalValue(DependencyProperty property) { throw new global::System.NotImplementedException("Windows.UI.Xaml.DependencyObject.ReadLocalValue"); }
        public global::Windows.UI.Core.CoreDispatcher Dispatcher { get { throw new global::System.NotImplementedException(); } }
    }
    public enum Visibility { Visible = 0, Collapsed = 1 }
    public enum HorizontalAlignment { Left = 0, Center = 1, Right = 2, Stretch = 3 }
    public struct Duration
    {
        public Duration(global::System.TimeSpan timeSpan) { throw new global::System.NotImplementedException(); }
        public static implicit operator Duration(global::System.TimeSpan timeSpan) { throw new global::System.NotImplementedException(); }
        public static Duration Automatic { get { throw new global::System.NotImplementedException(); } }
        public static Duration Forever { get { throw new global::System.NotImplementedException(); } }
        public bool HasTimeSpan { get { throw new global::System.NotImplementedException(); } }
        public global::System.TimeSpan TimeSpan { get { throw new global::System.NotImplementedException(); } }
    }
    public struct Thickness
    {
        private double _Left, _Top, _Right, _Bottom;
        public Thickness(double left, double top, double right, double bottom) { _Left = left; _Top = top; _Right = right; _Bottom = bottom; }
        public double Left { get { return _Left; } }
        public double Top { get { return _Top; } }
        public double Right { get { return _Right; } }
        public double Bottom { get { return _Bottom; } }
        public static bool operator ==(Thickness a, Thickness b) { throw new global::System.NotImplementedException(); }
        public static bool operator !=(Thickness a, Thickness b) { throw new global::System.NotImplementedException(); }
        public override bool Equals(object o) { throw new global::System.NotImplementedException(); }
        public override int GetHashCode() { throw new global::System.NotImplementedException(); }
    }
    public class UIElement : DependencyObject
    {
        internal UIElement() { }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_PointerEntered(global::Windows.UI.Xaml.Input.PointerEventHandler handler) { throw new global::System.NotImplementedException(); }
        public void remove_PointerEntered(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_PointerExited(global::Windows.UI.Xaml.Input.PointerEventHandler handler) { throw new global::System.NotImplementedException(); }
        public void remove_PointerExited(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_PointerPressed(global::Windows.UI.Xaml.Input.PointerEventHandler handler) { throw new global::System.NotImplementedException(); }
        public void remove_PointerPressed(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_Tapped(global::Windows.UI.Xaml.Input.TappedEventHandler handler) { throw new global::System.NotImplementedException(); }
        public void remove_Tapped(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
        public Visibility Visibility { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public double Opacity { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public static DependencyProperty OpacityProperty { get { throw new global::System.NotImplementedException(); } }
        public global::Windows.UI.Xaml.Media.Animation.TransitionCollection Transitions { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public global::Windows.UI.Xaml.Media.Transform RenderTransform { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public void UpdateLayout() { throw new global::System.NotImplementedException(); }
        public void Measure(global::Windows.Foundation.Size availableSize) { throw new global::System.NotImplementedException(); }
        public global::Windows.Foundation.Size DesiredSize { get { throw new global::System.NotImplementedException(); } }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_KeyDown(global::Windows.UI.Xaml.Input.KeyEventHandler handler) { throw new global::System.NotImplementedException(); }
        public void remove_KeyDown(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
    public class FrameworkElement : UIElement
    {
        internal FrameworkElement() { }
        public ResourceDictionary Resources { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public object FindName(string name) { throw new global::System.NotImplementedException(); }
        public Style Style { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public double ActualWidth { get { throw new global::System.NotImplementedException(); } }
        public double Width { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public double Height { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public Thickness Margin { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public HorizontalAlignment HorizontalAlignment { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public object DataContext { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public object Tag { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public DependencyObject Parent { get { throw new global::System.NotImplementedException(); } }
        public double ActualHeight { get { throw new global::System.NotImplementedException(); } }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_Loaded(RoutedEventHandler handler) { throw new global::System.NotImplementedException(); }
        public void remove_Loaded(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_Unloaded(RoutedEventHandler handler) { throw new global::System.NotImplementedException(); }
        public void remove_Unloaded(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_SizeChanged(SizeChangedEventHandler handler) { throw new global::System.NotImplementedException(); }
        public void remove_SizeChanged(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
    public class VisualStateManager : DependencyObject
    {
        public VisualStateManager() { throw new global::System.NotImplementedException(); }
        public static bool GoToState(global::Windows.UI.Xaml.Controls.Control control, string stateName, bool useTransitions) { throw new global::System.NotImplementedException(); }
    }
    public sealed class Style : DependencyObject { public Style() { throw new global::System.NotImplementedException(); } }
    public class RoutedEventArgs
    {
        public RoutedEventArgs() { throw new global::System.NotImplementedException(); }
        public object OriginalSource { get { throw new global::System.NotImplementedException(); } }
    }
}
namespace Windows.UI.Xaml.Media
{
    public static class VisualTreeHelper
    {
        public static int GetChildrenCount(global::Windows.UI.Xaml.DependencyObject reference) { throw new global::System.NotImplementedException(); }
        public static global::Windows.UI.Xaml.DependencyObject GetChild(global::Windows.UI.Xaml.DependencyObject reference, int childIndex) { throw new global::System.NotImplementedException(); }
        public static global::Windows.UI.Xaml.DependencyObject GetParent(global::Windows.UI.Xaml.DependencyObject reference) { throw new global::System.NotImplementedException(); }
    }
}
namespace Windows.UI.Core
{
    public sealed class WindowSizeChangedEventArgs { internal WindowSizeChangedEventArgs() { } }
    public enum CoreWindowActivationState { CodeActivated = 0, Deactivated = 1, PointerActivated = 2 }
    public sealed class WindowActivatedEventArgs
    {
        internal WindowActivatedEventArgs() { }
        public CoreWindowActivationState WindowActivationState { get { throw new global::System.NotImplementedException(); } }
    }
}
namespace Windows.UI.Xaml.Controls
{
    public sealed class UIElementCollection : global::System.Collections.Generic.IList<global::Windows.UI.Xaml.UIElement>
    {
        internal UIElementCollection() { }
        public global::Windows.UI.Xaml.UIElement this[int index] { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public int Count { get { throw new global::System.NotImplementedException(); } }
        public bool IsReadOnly { get { throw new global::System.NotImplementedException(); } }
        public int IndexOf(global::Windows.UI.Xaml.UIElement item) { throw new global::System.NotImplementedException(); }
        public void Insert(int index, global::Windows.UI.Xaml.UIElement item) { throw new global::System.NotImplementedException(); }
        public void RemoveAt(int index) { throw new global::System.NotImplementedException(); }
        public void Add(global::Windows.UI.Xaml.UIElement item) { throw new global::System.NotImplementedException(); }
        public void Clear() { throw new global::System.NotImplementedException(); }
        public bool Contains(global::Windows.UI.Xaml.UIElement item) { throw new global::System.NotImplementedException(); }
        public void CopyTo(global::Windows.UI.Xaml.UIElement[] array, int index) { throw new global::System.NotImplementedException(); }
        public bool Remove(global::Windows.UI.Xaml.UIElement item) { throw new global::System.NotImplementedException(); }
        public global::System.Collections.Generic.IEnumerator<global::Windows.UI.Xaml.UIElement> GetEnumerator() { throw new global::System.NotImplementedException(); }
        global::System.Collections.IEnumerator global::System.Collections.IEnumerable.GetEnumerator() { throw new global::System.NotImplementedException(); }
    }
    public class Panel : global::Windows.UI.Xaml.FrameworkElement
    {
        internal Panel() { }
        public UIElementCollection Children { get { throw new global::System.NotImplementedException(); } }
    }
    public class Grid : Panel
    {
        public Grid() { throw new global::System.NotImplementedException(); }
        public static global::Windows.UI.Xaml.DependencyProperty RowProperty { get { throw new global::System.NotImplementedException(); } }
        public static int GetRow(global::Windows.UI.Xaml.FrameworkElement element) { throw new global::System.NotImplementedException(); }
        public static void SetRow(global::Windows.UI.Xaml.FrameworkElement element, int value) { throw new global::System.NotImplementedException(); }
    }
    public class Image : global::Windows.UI.Xaml.FrameworkElement
    {
        internal Image() { }
        public global::Windows.UI.Xaml.Media.ImageSource Source { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
    public class Control : global::Windows.UI.Xaml.FrameworkElement
    {
        internal Control() { }
        public bool IsEnabled { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        protected virtual void OnKeyDown(global::Windows.UI.Xaml.Input.KeyRoutedEventArgs args) { throw new global::System.NotImplementedException(); }
        protected virtual void OnKeyUp(global::Windows.UI.Xaml.Input.KeyRoutedEventArgs args) { throw new global::System.NotImplementedException(); }
        protected virtual void OnGotFocus(global::Windows.UI.Xaml.RoutedEventArgs e) { throw new global::System.NotImplementedException(); }
    }
    public class Viewbox : global::Windows.UI.Xaml.FrameworkElement
    {
        public Viewbox() { throw new global::System.NotImplementedException(); }
        public global::Windows.UI.Xaml.UIElement Child { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
    public class Canvas : Panel
    {
        public Canvas() { throw new global::System.NotImplementedException(); }
        public static global::Windows.UI.Xaml.DependencyProperty LeftProperty { get { throw new global::System.NotImplementedException(); } }
        public static double GetLeft(global::Windows.UI.Xaml.UIElement element) { throw new global::System.NotImplementedException(); }
        public static void SetLeft(global::Windows.UI.Xaml.UIElement element, double length) { throw new global::System.NotImplementedException(); }
        public static void SetTop(global::Windows.UI.Xaml.UIElement element, double length) { throw new global::System.NotImplementedException(); }
    }
    public class ItemsControl : Control
    {
        internal ItemsControl() { }
        public ItemContainerGenerator ItemContainerGenerator { get { throw new global::System.NotImplementedException(); } }
    }
    public sealed class ItemContainerGenerator
    {
        internal ItemContainerGenerator() { }
        public int IndexFromContainer(global::Windows.UI.Xaml.DependencyObject container) { throw new global::System.NotImplementedException(); }
        public global::Windows.UI.Xaml.DependencyObject ContainerFromIndex(int index) { throw new global::System.NotImplementedException(); }
    }
    public class ScrollViewer : ContentControl
    {
        public ScrollViewer() { throw new global::System.NotImplementedException(); }
        public double ScrollableWidth { get { throw new global::System.NotImplementedException(); } }
        public double HorizontalOffset { get { throw new global::System.NotImplementedException(); } }
        public void ScrollToHorizontalOffset(double offset) { throw new global::System.NotImplementedException(); }
    }
    public class ContentPresenter : global::Windows.UI.Xaml.FrameworkElement { internal ContentPresenter() { } }
    public sealed class MediaElement : global::Windows.UI.Xaml.FrameworkElement
    {
        public MediaElement() { throw new global::System.NotImplementedException(); }
        public global::System.Uri Source { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public bool AutoPlay { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public bool IsLooping { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public double Volume { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public global::Windows.UI.Xaml.Media.AudioCategory AudioCategory { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public void Play() { throw new global::System.NotImplementedException(); }
        public void Pause() { throw new global::System.NotImplementedException(); }
        public void Stop() { throw new global::System.NotImplementedException(); }
    }
    public class SwapChainBackgroundPanel : Grid { public SwapChainBackgroundPanel() { throw new global::System.NotImplementedException(); } }
    public class TextBlock : global::Windows.UI.Xaml.FrameworkElement
    {
        public TextBlock() { throw new global::System.NotImplementedException(); }
        public static global::Windows.UI.Xaml.DependencyProperty TextProperty { get { throw new global::System.NotImplementedException(); } }
        public global::Windows.UI.Xaml.Media.Brush Foreground { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public double FontSize { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public string Text { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
    public class ProgressRing : Control
    {
        internal ProgressRing() { }
        public bool IsActive { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
    public class Button : global::Windows.UI.Xaml.Controls.Primitives.ButtonBase { public Button() { throw new global::System.NotImplementedException(); } }
    public class RadioButton : global::Windows.UI.Xaml.Controls.Primitives.ToggleButton { public RadioButton() { throw new global::System.NotImplementedException(); } }
    public class ContentControl : Control
    {
        public ContentControl() { throw new global::System.NotImplementedException(); }
        public object Content { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
    public class UserControl : Control
    {
        public UserControl() { throw new global::System.NotImplementedException(); }
        public global::Windows.UI.Xaml.UIElement Content { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
    public class Page : UserControl
    {
        public Page() { throw new global::System.NotImplementedException("Windows.UI.Xaml.Controls.Page..ctor"); }
        public Frame Frame { get { throw new global::System.NotImplementedException(); } }
        public global::Windows.UI.Xaml.Navigation.NavigationCacheMode NavigationCacheMode { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        protected virtual void OnNavigatedTo(global::Windows.UI.Xaml.Navigation.NavigationEventArgs args) { throw new global::System.NotImplementedException(); }
        protected virtual void OnNavigatedFrom(global::Windows.UI.Xaml.Navigation.NavigationEventArgs args) { throw new global::System.NotImplementedException(); }
        protected virtual void OnNavigatingFrom(global::Windows.UI.Xaml.Navigation.NavigatingCancelEventArgs args) { throw new global::System.NotImplementedException(); }
    }
    public class Frame : ContentControl
    {
        public Frame() { throw new global::System.NotImplementedException("Windows.UI.Xaml.Controls.Frame..ctor"); }
        public bool Navigate(global::System.Type sourcePageType) { throw new global::System.NotImplementedException(); }
        public bool Navigate(global::System.Type sourcePageType, object parameter) { throw new global::System.NotImplementedException(); }
        public int BackStackDepth { get { throw new global::System.NotImplementedException(); } }
        public bool CanGoBack { get { throw new global::System.NotImplementedException(); } }
        public bool CanGoForward { get { throw new global::System.NotImplementedException(); } }
        public void GoBack() { throw new global::System.NotImplementedException(); }
        public void GoForward() { throw new global::System.NotImplementedException(); }
    }
}
namespace Windows.Devices.Geolocation
{
    // Outside the implemented surface: completion must stub it, naming itself.
    public sealed class Geolocator
    {
        public Geolocator() { throw new global::System.NotImplementedException("Windows.Devices.Geolocation.Geolocator..ctor"); }
    }
}
namespace Windows.ApplicationModel.Activation
{
    // Values match WinMD: they are compiled into consumers as constants.
    public enum ActivationKind { Launch = 0, Search = 1 }
    public enum ApplicationExecutionState { NotRunning = 0, Running = 1 }
    public interface IActivatedEventArgs
    {
        ActivationKind Kind { get; }
        ApplicationExecutionState PreviousExecutionState { get; }
        SplashScreen SplashScreen { get; }
    }
    public sealed class LaunchActivatedEventArgs : IActivatedEventArgs
    {
        public string Arguments { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.Activation.LaunchActivatedEventArgs.Arguments"); } }
        public ActivationKind Kind { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.Activation.LaunchActivatedEventArgs.Kind"); } }
        public ApplicationExecutionState PreviousExecutionState { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.Activation.LaunchActivatedEventArgs.PreviousExecutionState"); } }
        public SplashScreen SplashScreen { get { throw new global::System.NotImplementedException("Windows.ApplicationModel.Activation.IActivatedEventArgs.get_SplashScreen"); } }
    }
}
namespace Windows.UI.Core
{
    public sealed class CoreWindow
    {
        public static CoreWindow GetForCurrentThread() { throw new global::System.NotImplementedException("Windows.UI.Core.CoreWindow.GetForCurrentThread"); }
        public CoreDispatcher Dispatcher { get { throw new global::System.NotImplementedException("Windows.UI.Core.CoreWindow.Dispatcher"); } }
        public CoreVirtualKeyStates GetKeyState(global::Windows.System.VirtualKey virtualKey) { throw new global::System.NotImplementedException(); }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_KeyDown(global::Windows.Foundation.TypedEventHandler<CoreWindow, KeyEventArgs> handler) { throw new global::System.NotImplementedException(); }
        public void remove_KeyDown(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_PointerPressed(global::Windows.Foundation.TypedEventHandler<CoreWindow, PointerEventArgs> handler) { throw new global::System.NotImplementedException(); }
        public void remove_PointerPressed(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_PointerMoved(global::Windows.Foundation.TypedEventHandler<CoreWindow, PointerEventArgs> handler) { throw new global::System.NotImplementedException(); }
        public void remove_PointerMoved(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_PointerReleased(global::Windows.Foundation.TypedEventHandler<CoreWindow, PointerEventArgs> handler) { throw new global::System.NotImplementedException(); }
        public void remove_PointerReleased(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
    [global::System.Flags] public enum CoreVirtualKeyStates : uint { None = 0, Down = 1, Locked = 2 }
    public sealed class VisibilityChangedEventArgs { internal VisibilityChangedEventArgs() { } public bool Visible { get { throw new global::System.NotImplementedException(); } } }
    public sealed class KeyEventArgs { internal KeyEventArgs() { } public global::Windows.System.VirtualKey VirtualKey { get { throw new global::System.NotImplementedException(); } } }
    public sealed class PointerEventArgs
    {
        internal PointerEventArgs() { }
        public global::Windows.UI.Input.PointerPoint CurrentPoint { get { throw new global::System.NotImplementedException(); } }
        public bool Handled { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
    public enum CoreDispatcherPriority { Idle = -2, Low = -1, Normal = 0, High = 1 }
    public delegate void DispatchedHandler();
    public enum CoreAcceleratorKeyEventType { Character = 2, DeadCharacter = 3, KeyDown = 0, KeyUp = 1, SystemCharacter = 6, SystemDeadCharacter = 7, SystemKeyDown = 4, SystemKeyUp = 5 }
    public sealed class AcceleratorKeyEventArgs
    {
        internal AcceleratorKeyEventArgs() { }
        public CoreAcceleratorKeyEventType EventType { get { throw new global::System.NotImplementedException(); } }
        public global::Windows.System.VirtualKey VirtualKey { get { throw new global::System.NotImplementedException(); } }
        public bool Handled { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
    public sealed class CoreDispatcher
    {
        internal CoreDispatcher() { }
        public global::Windows.Foundation.IAsyncAction RunAsync(CoreDispatcherPriority priority, DispatchedHandler agileCallback) { throw new global::System.NotImplementedException("Windows.UI.Core.CoreDispatcher.RunAsync"); }
        public bool HasThreadAccess { get { throw new global::System.NotImplementedException(); } }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_AcceleratorKeyActivated(global::Windows.Foundation.TypedEventHandler<CoreDispatcher, AcceleratorKeyEventArgs> handler) { throw new global::System.NotImplementedException(); }
        public void remove_AcceleratorKeyActivated(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
}
namespace Windows.UI.Input
{
    public sealed class PointerPoint { internal PointerPoint() { } public PointerPointProperties Properties { get { throw new global::System.NotImplementedException(); } } }
    public sealed class PointerPointProperties { internal PointerPointProperties() { } public bool IsLeftButtonPressed { get { throw new global::System.NotImplementedException(); } } }
}
namespace Windows.UI.ApplicationSettings
{
    public enum SettingsEdgeLocation { Right = 0, Left = 1 }
    public sealed class SettingsPaneCommandsRequestedEventArgs { internal SettingsPaneCommandsRequestedEventArgs() { } }
    public sealed class SettingsPane
    {
        internal SettingsPane() { }
        public static SettingsPane GetForCurrentView() { throw new global::System.NotImplementedException(); }
        public static SettingsEdgeLocation Edge { get { throw new global::System.NotImplementedException(); } }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_CommandsRequested(global::Windows.Foundation.TypedEventHandler<SettingsPane, SettingsPaneCommandsRequestedEventArgs> handler) { throw new global::System.NotImplementedException(); }
        public void remove_CommandsRequested(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
}
namespace Windows.ApplicationModel.Store
{
    public delegate void LicenseChangedEventHandler();
    public sealed class LicenseInformation
    {
        internal LicenseInformation() { }
        public bool IsActive { get { throw new global::System.NotImplementedException(); } }
        public bool IsTrial { get { throw new global::System.NotImplementedException(); } }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_LicenseChanged(LicenseChangedEventHandler handler) { throw new global::System.NotImplementedException(); }
        public void remove_LicenseChanged(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
    public static class CurrentApp { public static LicenseInformation LicenseInformation { get { throw new global::System.NotImplementedException(); } } }
}
namespace Windows.Data.Xml.Dom
{
    public interface IXmlNode
    {
        XmlNamedNodeMap Attributes { get; }
        IXmlNode AppendChild(IXmlNode newChild);
    }
    public sealed class XmlNodeList : global::System.Collections.Generic.IReadOnlyList<IXmlNode>
    {
        internal XmlNodeList() { }
        public uint Length { get { throw new global::System.NotImplementedException(); } }
        public IXmlNode Item(uint index) { throw new global::System.NotImplementedException(); }
        public int Count { get { throw new global::System.NotImplementedException(); } }
        IXmlNode global::System.Collections.Generic.IReadOnlyList<IXmlNode>.this[int index] { get { throw new global::System.NotImplementedException(); } }
        public global::System.Collections.Generic.IEnumerator<IXmlNode> GetEnumerator() { throw new global::System.NotImplementedException(); }
        global::System.Collections.IEnumerator global::System.Collections.IEnumerable.GetEnumerator() { throw new global::System.NotImplementedException(); }
    }
    public sealed class XmlNamedNodeMap : global::System.Collections.Generic.IReadOnlyList<IXmlNode>
    {
        internal XmlNamedNodeMap() { }
        public uint Length { get { throw new global::System.NotImplementedException(); } }
        public IXmlNode Item(uint index) { throw new global::System.NotImplementedException(); }
        public IXmlNode GetNamedItem(string name) { throw new global::System.NotImplementedException(); }
        public int Count { get { throw new global::System.NotImplementedException(); } }
        IXmlNode global::System.Collections.Generic.IReadOnlyList<IXmlNode>.this[int index] { get { throw new global::System.NotImplementedException(); } }
        public global::System.Collections.Generic.IEnumerator<IXmlNode> GetEnumerator() { throw new global::System.NotImplementedException(); }
        global::System.Collections.IEnumerator global::System.Collections.IEnumerable.GetEnumerator() { throw new global::System.NotImplementedException(); }
    }
    public sealed class XmlText : IXmlNode
    {
        internal XmlText() { }
        public XmlNamedNodeMap Attributes { get { throw new global::System.NotImplementedException(); } }
        public IXmlNode AppendChild(IXmlNode newChild) { throw new global::System.NotImplementedException(); }
    }
    public sealed class XmlAttribute : IXmlNode
    {
        internal XmlAttribute() { }
        public string Name { get { throw new global::System.NotImplementedException(); } }
        public string Value { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public XmlNamedNodeMap Attributes { get { throw new global::System.NotImplementedException(); } }
        public IXmlNode AppendChild(IXmlNode newChild) { throw new global::System.NotImplementedException(); }
    }
    public sealed class XmlElement : IXmlNode
    {
        internal XmlElement() { }
        public string GetAttribute(string attributeName) { throw new global::System.NotImplementedException(); }
        public void SetAttribute(string attributeName, string attributeValue) { throw new global::System.NotImplementedException(); }
        public XmlNamedNodeMap Attributes { get { throw new global::System.NotImplementedException(); } }
        public IXmlNode AppendChild(IXmlNode newChild) { throw new global::System.NotImplementedException(); }
    }
    public sealed class XmlDocument : IXmlNode
    {
        internal XmlDocument() { }
        public XmlNodeList GetElementsByTagName(string tagName) { throw new global::System.NotImplementedException(); }
        public XmlText CreateTextNode(string data) { throw new global::System.NotImplementedException(); }
        public IXmlNode SelectSingleNode(string xpath) { throw new global::System.NotImplementedException(); }
        public string GetXml() { throw new global::System.NotImplementedException(); }
        public XmlNamedNodeMap Attributes { get { throw new global::System.NotImplementedException(); } }
        public IXmlNode AppendChild(IXmlNode newChild) { throw new global::System.NotImplementedException(); }
    }
}
namespace Windows.UI.Notifications
{
    public enum ToastTemplateType { ToastText01 = 4 }
    public enum TileTemplateType { TileWideImageAndText01 = 12 }
    public sealed class TileNotification { public TileNotification(global::Windows.Data.Xml.Dom.XmlDocument content) { throw new global::System.NotImplementedException(); } }
    public sealed class TileUpdater
    {
        internal TileUpdater() { }
        public void Clear() { throw new global::System.NotImplementedException(); }
        public void EnableNotificationQueue(bool enable) { throw new global::System.NotImplementedException(); }
        public void Update(TileNotification notification) { throw new global::System.NotImplementedException(); }
    }
    public static class TileUpdateManager
    {
        public static TileUpdater CreateTileUpdaterForApplication() { throw new global::System.NotImplementedException(); }
        public static global::Windows.Data.Xml.Dom.XmlDocument GetTemplateContent(TileTemplateType type) { throw new global::System.NotImplementedException(); }
    }
    public sealed class ToastNotification
    {
        public ToastNotification(global::Windows.Data.Xml.Dom.XmlDocument content) { throw new global::System.NotImplementedException(); }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_Activated(global::Windows.Foundation.TypedEventHandler<ToastNotification, object> handler) { throw new global::System.NotImplementedException(); }
        public void remove_Activated(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
    public sealed class ToastNotifier { internal ToastNotifier() { } public void Show(ToastNotification notification) { throw new global::System.NotImplementedException(); } }
    public static class ToastNotificationManager
    {
        public static ToastNotifier CreateToastNotifier() { throw new global::System.NotImplementedException(); }
        public static global::Windows.Data.Xml.Dom.XmlDocument GetTemplateContent(ToastTemplateType type) { throw new global::System.NotImplementedException(); }
    }
}
namespace Windows.Foundation.Collections
{
    public interface IPropertySet : global::System.Collections.Generic.IDictionary<string, object> { }
}
namespace Windows.Storage
{
    public enum ApplicationDataLocality { Local = 0, Roaming = 1, Temporary = 2 }
    public enum ApplicationDataCreateDisposition { Always = 0, Existing = 1 }
    public sealed class ApplicationData
    {
        public static ApplicationData Current { get { throw new global::System.NotImplementedException("Windows.Storage.ApplicationData.Current"); } }
        public ApplicationDataContainer LocalSettings { get { throw new global::System.NotImplementedException("Windows.Storage.ApplicationData.LocalSettings"); } }
        public ApplicationDataContainer RoamingSettings { get { throw new global::System.NotImplementedException("Windows.Storage.ApplicationData.RoamingSettings"); } }
    }
    public sealed class ApplicationDataContainer
    {
        public string Name { get { throw new global::System.NotImplementedException("Windows.Storage.ApplicationDataContainer.Name"); } }
        public ApplicationDataLocality Locality { get { throw new global::System.NotImplementedException("Windows.Storage.ApplicationDataContainer.Locality"); } }
        public global::Windows.Foundation.Collections.IPropertySet Values { get { throw new global::System.NotImplementedException("Windows.Storage.ApplicationDataContainer.Values"); } }
        public global::System.Collections.Generic.IReadOnlyDictionary<string, ApplicationDataContainer> Containers { get { throw new global::System.NotImplementedException("Windows.Storage.ApplicationDataContainer.Containers"); } }
        public ApplicationDataContainer CreateContainer(string name, ApplicationDataCreateDisposition disposition) { throw new global::System.NotImplementedException("Windows.Storage.ApplicationDataContainer.CreateContainer"); }
        public void DeleteContainer(string name) { throw new global::System.NotImplementedException("Windows.Storage.ApplicationDataContainer.DeleteContainer"); }
    }
}
namespace Windows.Networking.Connectivity
{
    public enum NetworkConnectivityLevel { None, LocalAccess, ConstrainedInternetAccess, InternetAccess }
    public delegate void NetworkStatusChangedEventHandler(object sender);
    public sealed class ConnectionProfile
    {
        public string ProfileName { get { throw new global::System.NotImplementedException(); } }
        public NetworkConnectivityLevel GetNetworkConnectivityLevel() { throw new global::System.NotImplementedException(); }
    }
    public static class NetworkInformation
    {
        public static ConnectionProfile GetInternetConnectionProfile() { throw new global::System.NotImplementedException("Windows.Networking.Connectivity.NetworkInformation.GetInternetConnectionProfile"); }
        public static global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_NetworkStatusChanged(NetworkStatusChangedEventHandler handler) { throw new global::System.NotImplementedException(); }
        public static void remove_NetworkStatusChanged(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
}

namespace Windows.Foundation
{
    public struct Size
    {
        public Size(double width, double height) { throw new global::System.NotImplementedException(); }
        public double Width { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public double Height { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
    public struct Rect
    {
        public double X { get { throw new global::System.NotImplementedException(); } }
        public double Y { get { throw new global::System.NotImplementedException(); } }
        public double Width { get { throw new global::System.NotImplementedException(); } }
        public double Height { get { throw new global::System.NotImplementedException(); } }
    }
    public delegate void TypedEventHandler<TSender, TArgs>(TSender sender, TArgs args);
    public enum AsyncStatus { Started = 0, Completed = 1, Canceled = 2, Error = 3 }
    public delegate void AsyncActionCompletedHandler(IAsyncAction asyncInfo, AsyncStatus asyncStatus);
    public interface IAsyncInfo
    {
        global::System.Exception ErrorCode { get; }
        uint Id { get; }
        AsyncStatus Status { get; }
        void Cancel();
        void Close();
    }
    public interface IAsyncAction : IAsyncInfo
    {
        AsyncActionCompletedHandler Completed { get; set; }
        void GetResults();
    }
}
namespace Windows.ApplicationModel.DataTransfer
{
    public sealed class DataRequestedEventArgs { internal DataRequestedEventArgs() { } }
    public sealed class DataTransferManager
    {
        internal DataTransferManager() { }
        public static DataTransferManager GetForCurrentView() { throw new global::System.NotImplementedException(); }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_DataRequested(global::Windows.Foundation.TypedEventHandler<DataTransferManager, DataRequestedEventArgs> handler) { throw new global::System.NotImplementedException(); }
        public void remove_DataRequested(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
}
namespace Windows.ApplicationModel.Activation
{
    public sealed class SplashScreen
    {
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_Dismissed(global::Windows.Foundation.TypedEventHandler<SplashScreen, object> handler) { throw new global::System.NotImplementedException(); }
        public void remove_Dismissed(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
}

namespace Windows.UI.Xaml.Navigation
{
    public enum NavigationCacheMode { Disabled = 0, Required = 1, Enabled = 2 }
    public enum NavigationMode { New = 0, Back = 1, Forward = 2, Refresh = 3 }
    public sealed class NavigationEventArgs
    {
        public global::System.Uri Uri { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public object Content { get { throw new global::System.NotImplementedException(); } }
        public object Parameter { get { throw new global::System.NotImplementedException(); } }
        public global::System.Type SourcePageType { get { throw new global::System.NotImplementedException(); } }
        public NavigationMode NavigationMode { get { throw new global::System.NotImplementedException(); } }
    }
    public sealed class NavigatingCancelEventArgs
    {
        public bool Cancel { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
}

namespace Windows.UI.Xaml.Markup
{
    public interface IComponentConnector { void Connect(int connectionId, object target); }
    public interface IXamlMetadataProvider
    {
        IXamlType GetXamlType(global::System.Type type);
        IXamlType GetXamlType(string fullName);
        XmlnsDefinition[] GetXmlnsDefinitions();
    }
    public interface IXamlType
    {
        IXamlType BaseType { get; }
        IXamlMember ContentProperty { get; }
        string FullName { get; }
        bool IsArray { get; }
        bool IsCollection { get; }
        bool IsConstructible { get; }
        bool IsDictionary { get; }
        bool IsMarkupExtension { get; }
        bool IsBindable { get; }
        IXamlType ItemType { get; }
        IXamlType KeyType { get; }
        global::System.Type UnderlyingType { get; }
        object ActivateInstance();
        object CreateFromString(string value);
        IXamlMember GetMember(string name);
        void AddToVector(object instance, object value);
        void AddToMap(object instance, object key, object value);
        void RunInitializer();
    }
    public interface IXamlMember
    {
        bool IsAttachable { get; }
        bool IsDependencyProperty { get; }
        bool IsReadOnly { get; }
        string Name { get; }
        IXamlType TargetType { get; }
        IXamlType Type { get; }
        object GetValue(object instance);
        void SetValue(object instance, object value);
    }
    public struct XmlnsDefinition
    {
        public string XmlNamespace;
        public string Namespace;
    }
}

namespace Windows.UI.Xaml.Controls.Primitives
{
    public enum ComponentResourceLocation { Application, Nested }
    public sealed class Popup : global::Windows.UI.Xaml.FrameworkElement
    {
        public Popup() { throw new global::System.NotImplementedException(); }
        public global::Windows.UI.Xaml.UIElement Child { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public bool IsOpen { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public double HorizontalOffset { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public double VerticalOffset { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public bool IsLightDismissEnabled { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_Closed(global::System.EventHandler<object> handler) { throw new global::System.NotImplementedException(); }
        public void remove_Closed(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
    public class ButtonBase : global::Windows.UI.Xaml.Controls.ContentControl
    {
        internal ButtonBase() { }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_Click(global::Windows.UI.Xaml.RoutedEventHandler handler) { throw new global::System.NotImplementedException(); }
        public void remove_Click(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
    public class ToggleButton : ButtonBase
    {
        internal ToggleButton() { }
        public global::System.Nullable<bool> IsChecked { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
}

namespace Windows.UI.Xaml
{
    public class ResourceDictionary : DependencyObject, global::System.Collections.Generic.IDictionary<object, object>
    {
        public ResourceDictionary() { throw new global::System.NotImplementedException(); }
        public global::System.Uri Source { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public global::System.Collections.Generic.IList<ResourceDictionary> MergedDictionaries { get { throw new global::System.NotImplementedException(); } }
        public object this[object key] { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public global::System.Collections.Generic.ICollection<object> Keys { get { throw new global::System.NotImplementedException(); } }
        public global::System.Collections.Generic.ICollection<object> Values { get { throw new global::System.NotImplementedException(); } }
        public int Count { get { throw new global::System.NotImplementedException(); } }
        public bool IsReadOnly { get { throw new global::System.NotImplementedException(); } }
        public void Add(object key, object value) { throw new global::System.NotImplementedException(); }
        public bool ContainsKey(object key) { throw new global::System.NotImplementedException(); }
        public bool Remove(object key) { throw new global::System.NotImplementedException(); }
        public bool TryGetValue(object key, out object value) { throw new global::System.NotImplementedException(); }
        public void Add(global::System.Collections.Generic.KeyValuePair<object, object> item) { throw new global::System.NotImplementedException(); }
        public void Clear() { throw new global::System.NotImplementedException(); }
        public bool Contains(global::System.Collections.Generic.KeyValuePair<object, object> item) { throw new global::System.NotImplementedException(); }
        public void CopyTo(global::System.Collections.Generic.KeyValuePair<object, object>[] array, int index) { throw new global::System.NotImplementedException(); }
        public bool Remove(global::System.Collections.Generic.KeyValuePair<object, object> item) { throw new global::System.NotImplementedException(); }
        public global::System.Collections.Generic.IEnumerator<global::System.Collections.Generic.KeyValuePair<object, object>> GetEnumerator() { throw new global::System.NotImplementedException(); }
        global::System.Collections.IEnumerator global::System.Collections.IEnumerable.GetEnumerator() { throw new global::System.NotImplementedException(); }
    }
}

namespace Windows.UI.Xaml.Media.Animation
{
    public class Timeline : global::Windows.UI.Xaml.DependencyObject
    {
        internal Timeline() { }
        public global::Windows.UI.Xaml.Duration Duration { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_Completed(global::System.EventHandler<object> handler) { throw new global::System.NotImplementedException(); }
        public void remove_Completed(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
    public sealed class TimelineCollection : global::System.Collections.Generic.IList<Timeline>
    {
        internal TimelineCollection() { }
        public Timeline this[int index] { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public int Count { get { throw new global::System.NotImplementedException(); } }
        public bool IsReadOnly { get { throw new global::System.NotImplementedException(); } }
        public void Add(Timeline item) { throw new global::System.NotImplementedException(); }
        public void Clear() { throw new global::System.NotImplementedException(); }
        public bool Contains(Timeline item) { throw new global::System.NotImplementedException(); }
        public void CopyTo(Timeline[] array, int index) { throw new global::System.NotImplementedException(); }
        public int IndexOf(Timeline item) { throw new global::System.NotImplementedException(); }
        public void Insert(int index, Timeline item) { throw new global::System.NotImplementedException(); }
        public bool Remove(Timeline item) { throw new global::System.NotImplementedException(); }
        public void RemoveAt(int index) { throw new global::System.NotImplementedException(); }
        public global::System.Collections.Generic.IEnumerator<Timeline> GetEnumerator() { throw new global::System.NotImplementedException(); }
        global::System.Collections.IEnumerator global::System.Collections.IEnumerable.GetEnumerator() { throw new global::System.NotImplementedException(); }
    }
    public sealed class DoubleAnimation : Timeline
    {
        public DoubleAnimation() { throw new global::System.NotImplementedException(); }
        public double? From { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public double? To { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
    public class Storyboard : Timeline
    {
        public Storyboard() { throw new global::System.NotImplementedException(); }
        public void Begin() { throw new global::System.NotImplementedException(); }
        public void Stop() { throw new global::System.NotImplementedException(); }
        public TimelineCollection Children { get { throw new global::System.NotImplementedException(); } }
        public static void SetTarget(Timeline timeline, global::Windows.UI.Xaml.DependencyObject target) { throw new global::System.NotImplementedException(); }
        public static void SetTargetProperty(Timeline element, string path) { throw new global::System.NotImplementedException(); }
    }
    public class Transition : global::Windows.UI.Xaml.DependencyObject { internal Transition() { } }
    public sealed class EntranceThemeTransition : Transition
    {
        public EntranceThemeTransition() { throw new global::System.NotImplementedException(); }
        public double FromHorizontalOffset { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
    public sealed class TransitionCollection : global::System.Collections.Generic.IList<Transition>
    {
        public TransitionCollection() { throw new global::System.NotImplementedException(); }
        public Transition this[int index] { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public int Count { get { throw new global::System.NotImplementedException(); } }
        public bool IsReadOnly { get { throw new global::System.NotImplementedException(); } }
        public void Add(Transition item) { throw new global::System.NotImplementedException(); }
        public void Clear() { throw new global::System.NotImplementedException(); }
        public bool Contains(Transition item) { throw new global::System.NotImplementedException(); }
        public void CopyTo(Transition[] array, int index) { throw new global::System.NotImplementedException(); }
        public int IndexOf(Transition item) { throw new global::System.NotImplementedException(); }
        public void Insert(int index, Transition item) { throw new global::System.NotImplementedException(); }
        public bool Remove(Transition item) { throw new global::System.NotImplementedException(); }
        public void RemoveAt(int index) { throw new global::System.NotImplementedException(); }
        public global::System.Collections.Generic.IEnumerator<Transition> GetEnumerator() { throw new global::System.NotImplementedException(); }
        global::System.Collections.IEnumerator global::System.Collections.IEnumerable.GetEnumerator() { throw new global::System.NotImplementedException(); }
    }
    public class FadeInThemeAnimation : Timeline { internal FadeInThemeAnimation() { } }
    public class FadeOutThemeAnimation : Timeline
    {
        public FadeOutThemeAnimation() { throw new global::System.NotImplementedException(); }
        public string TargetName { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
}

namespace Windows.UI.ViewManagement
{
    public enum ApplicationViewState { FullScreenLandscape = 0, Filled = 1, Snapped = 2, FullScreenPortrait = 3 }
    public sealed class ApplicationView
    {
        public static ApplicationViewState Value { get { throw new global::System.NotImplementedException("ApplicationView.Value"); } }
        public static bool TryUnsnap() { throw new global::System.NotImplementedException(); }
    }
}

namespace Windows.UI.Xaml.Media
{
    public class Brush : global::Windows.UI.Xaml.DependencyObject { internal Brush() { } }
    public enum AudioCategory { Other = 0, ForegroundOnlyMedia = 1, BackgroundCapableMedia = 2 }
    public static class CompositionTarget
    {
        public static global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_Rendering(global::System.EventHandler<object> handler) { throw new global::System.NotImplementedException(); }
        public static void remove_Rendering(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
    public class GeneralTransform : global::Windows.UI.Xaml.DependencyObject { internal GeneralTransform() { } }
    public class Transform : GeneralTransform { internal Transform() { } }
    public sealed class TranslateTransform : Transform
    {
        public TranslateTransform() { throw new global::System.NotImplementedException(); }
        public double X { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public double Y { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
    public sealed class SolidColorBrush : Brush
    {
        public SolidColorBrush() { throw new global::System.NotImplementedException(); }
        public SolidColorBrush(global::Windows.UI.Color color) { throw new global::System.NotImplementedException(); }
        public global::Windows.UI.Color Color { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
}

namespace Windows.UI
{
    public struct Color
    {
        private byte _A, _R, _G, _B;
        public byte A { get { return _A; } set { _A = value; } }
        public byte R { get { return _R; } set { _R = value; } }
        public byte G { get { return _G; } set { _G = value; } }
        public byte B { get { return _B; } set { _B = value; } }
    }
    public static class Colors
    {
        public static Color Goldenrod { get { throw new global::System.NotImplementedException(); } }
        public static Color Gray { get { throw new global::System.NotImplementedException(); } }
    }
}

namespace Windows.UI.Xaml.Data
{
    public interface IValueConverter
    {
        object Convert(object value, global::System.Type targetType, object parameter, string language);
        object ConvertBack(object value, global::System.Type targetType, object parameter, string language);
    }
}

namespace Windows.UI.Xaml.Media
{
    public class ImageSource : global::Windows.UI.Xaml.DependencyObject { internal ImageSource() { } }
}
namespace Windows.UI.Xaml.Media.Imaging
{
    public class BitmapSource : global::Windows.UI.Xaml.Media.ImageSource { internal BitmapSource() { } }
    public sealed class BitmapImage : BitmapSource
    {
        public BitmapImage() { throw new global::System.NotImplementedException(); }
        public BitmapImage(global::System.Uri source) { throw new global::System.NotImplementedException(); }
    }
}

namespace Windows.UI.Xaml.Input
{
    public delegate void PointerEventHandler(object sender, PointerRoutedEventArgs args);
    public delegate void TappedEventHandler(object sender, TappedRoutedEventArgs args);
    public sealed class PointerRoutedEventArgs : global::Windows.UI.Xaml.RoutedEventArgs
    {
        public bool Handled { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
    public sealed class TappedRoutedEventArgs : global::Windows.UI.Xaml.RoutedEventArgs
    {
        public bool Handled { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
    }
}
namespace Windows.UI.Xaml.Controls.Primitives
{
    public delegate void RangeBaseValueChangedEventHandler(object sender, RangeBaseValueChangedEventArgs args);
    public sealed class RangeBaseValueChangedEventArgs : global::Windows.UI.Xaml.RoutedEventArgs
    {
        public double NewValue { get { throw new global::System.NotImplementedException(); } }
        public double OldValue { get { throw new global::System.NotImplementedException(); } }
    }
    public class RangeBase : global::Windows.UI.Xaml.Controls.Control
    {
        internal RangeBase() { }
        public double Value { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_ValueChanged(RangeBaseValueChangedEventHandler handler) { throw new global::System.NotImplementedException(); }
        public void remove_ValueChanged(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
}
namespace Windows.UI.Xaml.Controls
{
    public class Slider : global::Windows.UI.Xaml.Controls.Primitives.RangeBase { internal Slider() { } }
    public class ToggleSwitch : Control
    {
        internal ToggleSwitch() { }
        public bool IsOn { get { throw new global::System.NotImplementedException(); } set { throw new global::System.NotImplementedException(); } }
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_Toggled(global::Windows.UI.Xaml.RoutedEventHandler handler) { throw new global::System.NotImplementedException(); }
        public void remove_Toggled(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token) { throw new global::System.NotImplementedException(); }
    }
}
