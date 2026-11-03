using System;

namespace AppxPortable.WinRT.Xaml
{
    // UWP values to and from WPF: wrappers become their peers, enums convert by
    // name. Anything else must already be the target type, or the call fails.
    internal static class Values
    {
        internal static System.Windows.DependencyObject Dependency(global::Windows.UI.Xaml.DependencyObject owner)
        {
            if (owner == null) throw new ArgumentNullException("owner");
            owner.CheckThread();
            var peer = owner.peer as System.Windows.DependencyObject;
            if (peer == null) throw new NotSupportedException("No WPF dependency object behind " + owner.GetType().FullName);
            return peer;
        }

        internal static object Get(global::Windows.UI.Xaml.DependencyObject owner, System.Windows.DependencyProperty property, Type type)
        {
            return FromWpf(Dependency(owner).GetValue(property), type);
        }

        internal static void Set(global::Windows.UI.Xaml.DependencyObject owner, System.Windows.DependencyProperty property, object value)
        {
            Dependency(owner).SetValue(property, ToWpf(value, property.PropertyType));
        }

        // The WPF type a game-registered property stores: wrappers are stored as peers.
        internal static Type WpfType(Type type)
        {
            return typeof(global::Windows.UI.Xaml.DependencyObject).IsAssignableFrom(type) ? typeof(object) : type;
        }

        internal static object ToWpf(object value, Type type)
        {
            if (value == null) return null;
            var wrapper = value as global::Windows.UI.Xaml.DependencyObject;
            if (wrapper != null) value = wrapper.peer;
            if (type.IsInstanceOfType(value)) return value;
            if (value.GetType().IsEnum && type.IsEnum) return Enumeration(value, type);
            if (value is global::Windows.UI.Color && type == typeof(System.Windows.Media.Color))
            {
                var color = (global::Windows.UI.Color)value;
                return System.Windows.Media.Color.FromArgb(color.A, color.R, color.G, color.B);
            }
            if (value is global::Windows.UI.Xaml.Thickness && type == typeof(System.Windows.Thickness))
            {
                var thickness = (global::Windows.UI.Xaml.Thickness)value;
                return new System.Windows.Thickness(thickness.Left, thickness.Top, thickness.Right, thickness.Bottom);
            }
            // As SetValue on either platform: a value of the wrong type is an invalid argument.
            throw new ArgumentException("No WPF conversion: " + value.GetType().FullName + " -> " + type.FullName);
        }

        internal static object FromWpf(object value, Type type)
        {
            if (value == null) return null;
            if (value == System.Windows.DependencyProperty.UnsetValue) return global::Windows.UI.Xaml.DependencyProperty.UnsetValue;
            if (Peers.IsPeer(value)) value = Peers.Wrap(value);
            if (type.IsInstanceOfType(value) && !IsWpf(value.GetType())) return value;
            if (value.GetType().IsEnum && type.IsEnum) return Enumeration(value, type);
            if (value is System.Windows.Media.Color && type.IsAssignableFrom(typeof(global::Windows.UI.Color)))
            {
                var color = (System.Windows.Media.Color)value;
                return global::Windows.UI.Color.FromArgb(color.A, color.R, color.G, color.B);
            }
            if (value is System.Windows.Thickness && type.IsAssignableFrom(typeof(global::Windows.UI.Xaml.Thickness)))
            {
                var thickness = (System.Windows.Thickness)value;
                return new global::Windows.UI.Xaml.Thickness(thickness.Left, thickness.Top, thickness.Right, thickness.Bottom);
            }
            throw new NotSupportedException("No UWP conversion: " + value.GetType().FullName + " -> " + type.FullName);
        }

        private static bool IsWpf(Type type)
        {
            return type.Namespace != null && type.Namespace.StartsWith("System.Windows", StringComparison.Ordinal);
        }

        private static object Enumeration(object value, Type type)
        {
            string name = Enum.GetName(value.GetType(), value);
            if (name == null || !Enum.IsDefined(type, name))
                throw new ArgumentException("No " + type.FullName + " value for " + value.GetType().FullName + "." + (name ?? value.ToString()));
            return Enum.Parse(type, name);
        }
    }
}
