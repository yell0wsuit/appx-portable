using System;
using System.Collections.Generic;
using System.Reflection;

using AppxPortable.WinRT.Xaml;
using Windows.UI.Xaml.Input;
using Windows.UI.Xaml.Navigation;

namespace Windows.UI.Xaml.Controls
{
    public partial class Control
    {
        [Raised] protected virtual void OnKeyDown(KeyRoutedEventArgs e) { }
        [Raised] protected virtual void OnKeyUp(KeyRoutedEventArgs e) { }

        internal void RaiseKey(bool down, KeyRoutedEventArgs e)
        {
            if (down) OnKeyDown(e);
            else OnKeyUp(e);
        }
    }

    public partial class Page
    {
        private Frame frame;
        private NavigationCacheMode cacheMode;

        // Set by Frame.Navigate only: plain Frame content does not navigate.
        public Frame Frame { get { CheckThread(); return frame; } }

        public NavigationCacheMode NavigationCacheMode
        {
            get { CheckThread(); return cacheMode; }
            set { CheckThread(); cacheMode = value; }
        }

        [Raised] protected virtual void OnNavigatedTo(NavigationEventArgs e) { }
        [Raised] protected virtual void OnNavigatedFrom(NavigationEventArgs e) { }
        [Raised] protected virtual void OnNavigatingFrom(NavigatingCancelEventArgs e) { }

        internal void NavigatedTo(Frame host, NavigationEventArgs e) { frame = host; Host.Callback(() => OnNavigatedTo(e)); }
        internal void NavigatedFrom(NavigationEventArgs e) { Host.Callback(() => OnNavigatedFrom(e)); }
        internal void NavigatingFrom(NavigatingCancelEventArgs e) { Host.Callback(() => OnNavigatingFrom(e)); }
    }

    public partial class Frame
    {
        private struct Entry { internal Type Type; internal object Parameter; }
        private readonly List<Entry> back = new List<Entry>(), forward = new List<Entry>();
        private Entry? current;
        // Pages whose NavigationCacheMode is not Disabled are kept per type, as UWP's
        // frame cache does, so returning to them does not construct them again.
        private readonly Dictionary<Type, object> cache = new Dictionary<Type, object>();

        public bool Navigate(Type sourcePageType) { return Navigate(sourcePageType, null); }

        // The page comes from the app's XAML metadata, as UWP creates it; a page that
        // cannot be created leaves the current one in place.
        public bool Navigate(Type sourcePageType, object parameter)
        {
            if (sourcePageType == null) throw new ArgumentNullException("sourcePageType");
            CheckThread();
            if (!Show(sourcePageType, parameter, NavigationMode.New)) return false;
            forward.Clear();
            return true;
        }

        public int BackStackDepth { get { CheckThread(); return back.Count; } }
        public bool CanGoBack { get { CheckThread(); return back.Count > 0; } }
        public bool CanGoForward { get { CheckThread(); return forward.Count > 0; } }

        public void GoBack() { Step(back, forward, NavigationMode.Back); }
        public void GoForward() { Step(forward, back, NavigationMode.Forward); }

        // UWP fails with E_FAIL when there is no entry to go to.
        private void Step(List<Entry> from, List<Entry> to, NavigationMode mode)
        {
            CheckThread();
            if (from.Count == 0) throw new global::System.Runtime.InteropServices.COMException("No page to navigate to.", unchecked((int)0x80004005));
            var target = from[from.Count - 1];
            var leaving = current;
            if (!Show(target.Type, target.Parameter, mode, false)) return;
            from.RemoveAt(from.Count - 1);
            if (leaving.HasValue) to.Add(leaving.Value);
        }

        private bool Show(Type type, object parameter, NavigationMode mode, bool record = true)
        {
            object cached;
            object created = cache.TryGetValue(type, out cached) ? cached : Create(type);
            var old = Content as Page;
            if (old != null)
            {
                var leaving = new NavigatingCancelEventArgs(mode, type, parameter);
                old.NavigatingFrom(leaving);
                if (leaving.Cancel) return false;
            }
            if (record && current.HasValue) back.Add(current.Value);
            current = new Entry { Type = type, Parameter = parameter };
            Content = created;
            var args = new NavigationEventArgs(created, parameter, type, mode);
            if (old != null)
            {
                old.NavigatedFrom(args);
                if (old.NavigationCacheMode != NavigationCacheMode.Disabled) cache[old.GetType()] = old;
                else cache.Remove(old.GetType());
            }
            var page = created as Page;
            if (page != null) page.NavigatedTo(this, args);
            return true;
        }

        private static object Create(Type type)
        {
            var provider = Application.Current as Windows.UI.Xaml.Markup.IXamlMetadataProvider;
            if (provider == null) return Activator.CreateInstance(type);
            var xamlType = provider.GetXamlType(type);
            if (xamlType == null) throw new ArgumentException("No XAML type for page " + type.FullName, "sourcePageType");
            return xamlType.ActivateInstance();
        }
    }
}

namespace Windows.UI.Xaml.Input
{
    public sealed partial class KeyRoutedEventArgs
    {
        private readonly global::System.Windows.Input.KeyEventArgs key;

        internal KeyRoutedEventArgs(object originalSource, global::System.Windows.Input.KeyEventArgs key) : base(originalSource) { this.key = key; }

        public Windows.System.VirtualKey Key
        {
            get
            {
                var value = key.Key == global::System.Windows.Input.Key.System ? key.SystemKey : key.Key;
                return (Windows.System.VirtualKey)global::System.Windows.Input.KeyInterop.VirtualKeyFromKey(value);
            }
        }

        public bool Handled { get { return key.Handled; } set { key.Handled = value; } }
    }
}

namespace Windows.UI.Xaml.Navigation
{
    public sealed partial class NavigationEventArgs
    {
        private readonly object content, parameter;
        private readonly Type sourcePageType;
        private readonly NavigationMode mode;

        internal NavigationEventArgs(object content, object parameter, Type sourcePageType, NavigationMode mode)
        {
            this.content = content; this.parameter = parameter; this.sourcePageType = sourcePageType; this.mode = mode;
        }

        public object Content { get { return content; } }
        public object Parameter { get { return parameter; } }
        public Type SourcePageType { get { return sourcePageType; } }
        public NavigationMode NavigationMode { get { return mode; } }
        public Uri Uri { get; set; }
    }

    public sealed partial class NavigatingCancelEventArgs
    {
        private readonly NavigationMode mode;
        private readonly Type sourcePageType;
        private readonly object parameter;

        internal NavigatingCancelEventArgs(NavigationMode mode, Type sourcePageType, object parameter)
        {
            this.mode = mode; this.sourcePageType = sourcePageType; this.parameter = parameter;
        }

        public bool Cancel { get; set; }
        public NavigationMode NavigationMode { get { return mode; } }
        public Type SourcePageType { get { return sourcePageType; } }
        public object Parameter { get { return parameter; } }
    }
}

namespace AppxPortable.WinRT.Xaml
{
    // Marks a UWP virtual the adapter calls; overriding any other one is named in the log.
    [AttributeUsage(AttributeTargets.Method)]
    internal sealed class RaisedAttribute : Attribute { }

    internal static class Overrides
    {
        private static readonly HashSet<Type> checkedTypes = new HashSet<Type>();

        // WPF input reaches UWP Control overrides through class handlers on every control.
        internal static void Register()
        {
            global::System.Windows.EventManager.RegisterClassHandler(typeof(global::System.Windows.Controls.Control), global::System.Windows.Input.Keyboard.KeyDownEvent,
                new global::System.Windows.Input.KeyEventHandler((sender, e) => Key(sender, e, true)));
            global::System.Windows.EventManager.RegisterClassHandler(typeof(global::System.Windows.Controls.Control), global::System.Windows.Input.Keyboard.KeyUpEvent,
                new global::System.Windows.Input.KeyEventHandler((sender, e) => Key(sender, e, false)));
        }

        private static void Key(object sender, global::System.Windows.Input.KeyEventArgs e, bool down)
        {
            var control = Peers.Existing(sender) as global::Windows.UI.Xaml.Controls.Control;
            if (control == null) return;
            var source = Peers.IsPeer(e.OriginalSource) ? Peers.Wrap(e.OriginalSource) : null;
            var args = new KeyRoutedEventArgs(source, e);
            Host.Callback(() => control.RaiseKey(down, args));
        }

        // Game overrides of UWP virtuals nothing calls yet: named once per type. An
        // override of a virtual the projection lacks has no base method at run time.
        internal static void Check(Type type)
        {
            lock (checkedTypes) if (!checkedTypes.Add(type)) return;
            Assembly projection = typeof(Overrides).Assembly;
            Type projected = type;
            while (projected.Assembly != projection) projected = projected.BaseType;
            for (Type current = type; current != projected; current = current.BaseType)
                foreach (MethodInfo method in current.GetMethods(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
                {
                    if (!method.IsVirtual) continue;
                    MethodInfo definition = method.GetBaseDefinition();
                    if (definition == method && (method.Attributes & MethodAttributes.NewSlot) == 0)
                        Errors.Unimplemented(projected.FullName + "." + method.Name + " override");
                    else if (definition != method && definition.DeclaringType.Assembly == projection && !definition.IsDefined(typeof(RaisedAttribute), false))
                        Errors.Unimplemented(definition.DeclaringType.FullName + "." + definition.Name + " override");
                }
        }
    }
}
