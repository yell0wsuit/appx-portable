using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Windows.Data;
using System.Windows.Markup;

namespace AppxPortable.WinRT.Xaml
{
    // {Binding} as UWP markup writes it, built as a WPF Binding: values pass through
    // ValueAdapter (UWP converters, wrappers, string-keyed maps), and RelativeSource
    // Self paths name the element's UWP members.
    [MarkupExtensionReturnType(typeof(object))]
    public sealed class UwpBinding : MarkupExtension
    {
        public UwpBinding() { Mode = BindingMode.OneWay; }
        public UwpBinding(string path) : this() { Path = path; }

        [ConstructorArgument("path")] public string Path { get; set; }
        public BindingMode Mode { get; set; }
        public object Converter { get; set; }
        public object ConverterParameter { get; set; }
        public string ConverterLanguage { get; set; }
        public string ElementName { get; set; }
        public RelativeSource RelativeSource { get; set; }
        public object Source { get; set; }
        public object FallbackValue { get; set; }
        public object TargetNullValue { get; set; }

        public override object ProvideValue(IServiceProvider serviceProvider)
        {
            var binding = new Binding { Mode = Mode, ConverterParameter = ConverterParameter };
            if (Path != null) binding.Path = new System.Windows.PropertyPath(Path);
            if (ElementName != null) binding.ElementName = ElementName;
            object self = RelativeSource != null && RelativeSource.Mode == RelativeSourceMode.Self && Path != null ? Self(binding, serviceProvider) : null;
            if (self != null) binding.Source = self;
            else if (RelativeSource != null) binding.RelativeSource = RelativeSource;
            if (Source != null) binding.Source = Source;
            if (FallbackValue != null) binding.FallbackValue = FallbackValue;
            if (TargetNullValue != null) binding.TargetNullValue = TargetNullValue;
            var converter = Converter as global::Windows.UI.Xaml.Data.IValueConverter;
            if (Converter != null && converter == null)
                throw new NotSupportedException("Binding.Converter is not a UWP IValueConverter: " + Converter.GetType().FullName);
            binding.Converter = new ValueAdapter(converter, ConverterLanguage);
            return binding.ProvideValue(serviceProvider);
        }

        // A Self path the peer lacks names a member of the wrapper (an app page's
        // DefaultViewModel): an app dependency property binds on the peer, else the
        // wrapper itself is the source (returned).
        private object Self(Binding binding, IServiceProvider serviceProvider)
        {
            var target = serviceProvider.GetService(typeof(IProvideValueTarget)) as IProvideValueTarget;
            var wrapper = target == null ? null : Peers.Existing(target.TargetObject);
            string first = Path.Split('.')[0], rest = Path.Substring(first.Length);
            if (wrapper == null || target.TargetObject.GetType().GetProperty(first) != null) return null;
            var property = GameProperties.Find(wrapper.GetType(), first);
            if (property == null) return wrapper;
            binding.Path = new System.Windows.PropertyPath("(0)" + rest, property.Wpf);
            return null;
        }
    }

    // Between a WPF binding and its target: UWP converters see UWP values and types
    // (a language string, not a culture); results become WPF values.
    internal sealed class ValueAdapter : IValueConverter
    {
        private readonly global::Windows.UI.Xaml.Data.IValueConverter converter;
        private readonly string language;

        internal ValueAdapter(global::Windows.UI.Xaml.Data.IValueConverter converter, string language)
        {
            this.converter = converter; this.language = language;
        }

        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            value = Uwp(value);
            if (converter != null)
            {
                object result = null;
                // A failing converter is logged and the binding falls back, rather than
                // ending the app inside WPF's binding engine.
                try { Host.Callback(() => result = converter.Convert(value, UwpType(targetType), parameter, language ?? culture.Name)); }
                catch (Exception) { return System.Windows.DependencyProperty.UnsetValue; }
                value = result;
            }
            return Wpf(value, targetType, culture);
        }

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        {
            value = Uwp(value);
            if (converter == null) return value;
            object result = null;
            try { Host.Callback(() => result = converter.ConvertBack(value, UwpType(targetType), parameter, language ?? culture.Name)); }
            catch (Exception) { return System.Windows.DependencyProperty.UnsetValue; }
            return result;
        }

        private static object Uwp(object value)
        {
            var proxy = value as MapProxy;
            if (proxy != null) return proxy.Map;
            return Peers.IsPeer(value) ? Peers.Wrap(value) : value;
        }

        // The UWP type a WPF target type stands for: a wrapper type or a same-named enum.
        internal static Type UwpType(Type type)
        {
            if (type == null) return null;
            foreach (var pair in Peers.PeerTypes) if (pair.Value == type) return pair.Key;
            if (type.IsEnum && type.Namespace == "System.Windows")
                return typeof(global::Windows.UI.Xaml.UIElement).Assembly.GetType("Windows.UI.Xaml." + type.Name, false) ?? type;
            return type;
        }

        private static object Wpf(object value, Type targetType, CultureInfo culture)
        {
            if (value == null || value == Binding.DoNothing || value == System.Windows.DependencyProperty.UnsetValue) return value;
            if (value == global::Windows.UI.Xaml.DependencyProperty.UnsetValue) return System.Windows.DependencyProperty.UnsetValue;
            var map = value as IDictionary<string, object>;
            if (map != null) value = MapProxy.For(map);
            var wrapper = value as global::Windows.UI.Xaml.DependencyObject;
            if (wrapper != null) value = Peers.Of(wrapper);
            if (targetType == null || targetType.IsInstanceOfType(value)) return value;
            try { return Values.ToWpf(value, targetType); }
            catch (ArgumentException) { }
            if (targetType == typeof(string)) return System.Convert.ToString(value, culture);
            var typeConverter = TypeDescriptor.GetConverter(targetType);
            if (typeConverter.CanConvertFrom(value.GetType())) return typeConverter.ConvertFrom(null, culture, value);
            if (value is IConvertible && typeof(IConvertible).IsAssignableFrom(targetType)) return System.Convert.ChangeType(value, targetType, culture);
            return value;
        }
    }

    // A string-keyed map (LayoutAwarePage.DefaultViewModel) as a WPF binding source:
    // its keys are properties, and its MapChanged event raises PropertyChanged.
    internal sealed class MapProxy : ICustomTypeDescriptor, INotifyPropertyChanged
    {
        private static readonly ConditionalWeakTable<IDictionary<string, object>, MapProxy> proxies =
            new ConditionalWeakTable<IDictionary<string, object>, MapProxy>();
        internal readonly IDictionary<string, object> Map;

        private MapProxy(IDictionary<string, object> map)
        {
            Map = map;
            var changed = map.GetType().GetEvent("MapChanged");
            if (changed == null) return;
            var handler = Delegate.CreateDelegate(changed.EventHandlerType, this,
                typeof(MapProxy).GetMethod("Changed", BindingFlags.Instance | BindingFlags.NonPublic));
            changed.GetAddMethod().Invoke(map, new object[] { handler });
        }

        internal static MapProxy For(IDictionary<string, object> map) { return proxies.GetValue(map, m => new MapProxy(m)); }

        public event PropertyChangedEventHandler PropertyChanged;

        private void Changed(object sender, object args)
        {
            var key = args == null ? null : args.GetType().GetProperty("Key");
            var handler = PropertyChanged;
            if (handler != null) handler(this, new PropertyChangedEventArgs(key == null ? string.Empty : key.GetValue(args, null) as string));
        }

        // Keys come and go, and WPF keeps a path it failed to resolve: every name
        // resolves, reading null until the map has that key.
        public PropertyDescriptorCollection GetProperties() { return new Keys(Map.Keys.Select(k => (PropertyDescriptor)new Key(k)).ToArray()); }

        private sealed class Keys : PropertyDescriptorCollection
        {
            internal Keys(PropertyDescriptor[] keys) : base(keys) { }

            public override PropertyDescriptor this[string name] { get { return base[name] ?? new Key(name); } }

            public override PropertyDescriptor Find(string name, bool ignoreCase) { return base.Find(name, ignoreCase) ?? new Key(name); }
        }

        public PropertyDescriptorCollection GetProperties(Attribute[] attributes) { return GetProperties(); }
        public AttributeCollection GetAttributes() { return AttributeCollection.Empty; }
        public string GetClassName() { return Map.GetType().FullName; }
        public string GetComponentName() { return null; }
        public TypeConverter GetConverter() { return null; }
        public EventDescriptor GetDefaultEvent() { return null; }
        public PropertyDescriptor GetDefaultProperty() { return null; }
        public object GetEditor(Type editorBaseType) { return null; }
        public EventDescriptorCollection GetEvents() { return EventDescriptorCollection.Empty; }
        public EventDescriptorCollection GetEvents(Attribute[] attributes) { return EventDescriptorCollection.Empty; }
        public object GetPropertyOwner(PropertyDescriptor pd) { return this; }

        private sealed class Key : PropertyDescriptor
        {
            internal Key(string name) : base(name, null) { }

            public override Type ComponentType { get { return typeof(MapProxy); } }
            public override bool IsReadOnly { get { return false; } }
            public override Type PropertyType { get { return typeof(object); } }
            public override bool CanResetValue(object component) { return false; }
            public override void ResetValue(object component) { }
            public override bool ShouldSerializeValue(object component) { return false; }

            public override object GetValue(object component)
            {
                object value;
                if (!((MapProxy)component).Map.TryGetValue(Name, out value)) return null;
                var map = value as IDictionary<string, object>;
                return map != null ? For(map) : value;
            }

            public override void SetValue(object component, object value) { ((MapProxy)component).Map[Name] = value; }
        }
    }
}
