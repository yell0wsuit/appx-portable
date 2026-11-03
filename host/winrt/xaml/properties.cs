using System;
using System.Collections.Generic;

using AppxPortable.WinRT.Xaml;

namespace Windows.UI.Xaml
{
    public partial class DependencyObject
    {
        // Peers that are not WPF DispatcherObjects (ResourceDictionary) keep their creating thread.
        private readonly int thread = global::System.Threading.Thread.CurrentThread.ManagedThreadId;

        // WinRT XAML objects belong to their thread: RPC_E_WRONG_THREAD elsewhere.
        internal void CheckThread()
        {
            var owner = peer as global::System.Windows.Threading.DispatcherObject;
            if (owner != null ? !owner.CheckAccess() : global::System.Threading.Thread.CurrentThread.ManagedThreadId != thread)
                throw new global::System.Runtime.InteropServices.COMException("The XAML object belongs to another thread.", unchecked((int)0x8001010e));
        }

        public object GetValue(DependencyProperty dp)
        {
            return Values.Get(this, DependencyProperty.WpfOf(dp), dp.Type);
        }

        public void SetValue(DependencyProperty dp, object value)
        {
            Values.Set(this, DependencyProperty.WpfOf(dp), value);
        }

        public void ClearValue(DependencyProperty dp)
        {
            Values.Dependency(this).ClearValue(DependencyProperty.WpfOf(dp));
        }

        public object ReadLocalValue(DependencyProperty dp)
        {
            return Values.FromWpf(Values.Dependency(this).ReadLocalValue(DependencyProperty.WpfOf(dp)), dp.Type);
        }

        public object GetAnimationBaseValue(DependencyProperty dp)
        {
            var peer = Values.Dependency(this);
            var animatable = peer as global::System.Windows.Media.Animation.IAnimatable;
            var property = DependencyProperty.WpfOf(dp);
            return Values.FromWpf(animatable != null ? animatable.GetAnimationBaseValue(property) : peer.GetValue(property), dp.Type);
        }
    }

    // A WPF dependency property and the UWP type its values are read as. Game
    // properties are WPF attached properties, so styles and bindings reach them.
    public sealed partial class DependencyProperty
    {
        private static readonly Dictionary<global::System.Windows.DependencyProperty, DependencyProperty> known =
            new Dictionary<global::System.Windows.DependencyProperty, DependencyProperty>();
        private static readonly object unset = new object();
        internal readonly global::System.Windows.DependencyProperty Wpf;
        internal readonly Type Type;

        private DependencyProperty(global::System.Windows.DependencyProperty wpf, Type type) { Wpf = wpf; Type = type; }

        internal static DependencyProperty Of(global::System.Windows.DependencyProperty wpf, Type type)
        {
            lock (known)
            {
                DependencyProperty value;
                if (!known.TryGetValue(wpf, out value)) known.Add(wpf, value = new DependencyProperty(wpf, type));
                return value;
            }
        }

        internal static global::System.Windows.DependencyProperty WpfOf(DependencyProperty dp)
        {
            if (dp == null) throw new ArgumentNullException("dp");
            return dp.Wpf;
        }

        public static object UnsetValue { get { return unset; } }

        public static DependencyProperty Register(string name, Type propertyType, Type ownerType, PropertyMetadata typeMetadata)
        {
            return Game(name, propertyType, ownerType, typeMetadata);
        }

        public static DependencyProperty RegisterAttached(string name, Type propertyType, Type ownerType, PropertyMetadata defaultMetadata)
        {
            return Game(name, propertyType, ownerType, defaultMetadata);
        }

        private static DependencyProperty Game(string name, Type propertyType, Type ownerType, PropertyMetadata metadata)
        {
            if (name == null) throw new ArgumentNullException("name");
            if (propertyType == null) throw new ArgumentNullException("propertyType");
            if (ownerType == null) throw new ArgumentNullException("ownerType");
            Type wpfType = Values.WpfType(propertyType);
            object defaultValue = metadata == null ? (wpfType.IsValueType ? Activator.CreateInstance(wpfType) : null)
                : Values.ToWpf(metadata.Default, wpfType);
            DependencyProperty result = null;
            PropertyChangedCallback callback = metadata == null ? null : metadata.Callback;
            var wpfMetadata = new global::System.Windows.PropertyMetadata(defaultValue, callback == null ? null : new global::System.Windows.PropertyChangedCallback((d, e) =>
                callback(Peers.Wrap(d), new DependencyPropertyChangedEventArgs(result, Values.FromWpf(e.OldValue, propertyType), Values.FromWpf(e.NewValue, propertyType)))));
            result = Of(global::System.Windows.DependencyProperty.RegisterAttached(ownerType.FullName + "." + name, wpfType, typeof(GameProperties), wpfMetadata), propertyType);
            GameProperties.Add(ownerType, name, result);
            return result;
        }
    }

    public partial class PropertyMetadata
    {
        internal readonly object Default;
        internal readonly PropertyChangedCallback Callback;

        public PropertyMetadata(object defaultValue) : this(defaultValue, null) { }

        public PropertyMetadata(object defaultValue, PropertyChangedCallback propertyChangedCallback)
        {
            Default = defaultValue;
            Callback = propertyChangedCallback;
        }

        public object DefaultValue { get { return Default; } }

        public static PropertyMetadata Create(object defaultValue) { return new PropertyMetadata(defaultValue); }

        public static PropertyMetadata Create(object defaultValue, PropertyChangedCallback propertyChangedCallback)
        {
            return new PropertyMetadata(defaultValue, propertyChangedCallback);
        }
    }

    public sealed partial class DependencyPropertyChangedEventArgs
    {
        private readonly DependencyProperty property;
        private readonly object oldValue, newValue;

        internal DependencyPropertyChangedEventArgs(DependencyProperty property, object oldValue, object newValue)
        {
            this.property = property; this.oldValue = oldValue; this.newValue = newValue;
        }

        public DependencyProperty Property { get { return property; } }
        public object OldValue { get { return oldValue; } }
        public object NewValue { get { return newValue; } }
    }
}

namespace AppxPortable.WinRT.Xaml
{
    // Owner of game-registered properties, found again by owner type and name.
    internal static class GameProperties
    {
        private static readonly Dictionary<string, global::Windows.UI.Xaml.DependencyProperty> registered =
            new Dictionary<string, global::Windows.UI.Xaml.DependencyProperty>(StringComparer.Ordinal);

        internal static void Add(Type owner, string name, global::Windows.UI.Xaml.DependencyProperty property)
        {
            lock (registered) registered[owner.FullName + "." + name] = property;
        }

        internal static global::Windows.UI.Xaml.DependencyProperty Find(Type owner, string name)
        {
            global::Windows.UI.Xaml.DependencyProperty value;
            lock (registered)
                for (Type type = owner; type != null; type = type.BaseType)
                    if (registered.TryGetValue(type.FullName + "." + name, out value)) return value;
            return null;
        }
    }
}
