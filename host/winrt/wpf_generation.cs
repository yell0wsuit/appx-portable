// Windows.UI.Xaml classes backed by WPF (Adapter="Wpf"): UWP-shaped wrappers whose
// instances own a WPF peer. The surface maps members onto WPF dependency properties;
// hand-written partials in host/winrt/xaml/ supply the rest.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Xml;

using Mono.Cecil;
internal static partial class WinrtProjectionGenerator
{
    private const string XamlRuntime = "global::AppxPortable.WinRT.Xaml.";
    private const string DependencyObjectName = "Windows.UI.Xaml.DependencyObject";
    private static readonly HashSet<string> wpfTypes = new HashSet<string>(StringComparer.Ordinal);
    private static readonly List<KeyValuePair<TypeDefinition, string>> wpfPeers = new List<KeyValuePair<TypeDefinition, string>>();
    // Mapped members whose WPF name differs: markup keeps the UWP name.
    private static readonly List<string[]> wpfAliases = new List<string[]>();
    // Shim-backed XAML classes; every other Windows.UI.Xaml class is WPF-backed.
    private static readonly string[] nativeXaml = {
        "Windows.UI.Xaml.Application", "Windows.UI.Xaml.ApplicationInitializationCallbackParams",
        "Windows.UI.Xaml.Window", "Windows.UI.Xaml.DispatcherTimer" };

    private static bool IsWpf(string name) { return wpfTypes.Contains(name); }

    private static void RejectWpf(TypeReference type, string use)
    {
        if (IsWpf(type.FullName)) throw new NotSupportedException("Native member uses WPF-backed type: " + type.FullName + " in " + use);
    }

    private static void RejectNativeXaml(TypeDefinition type)
    {
        if (type.Namespace.StartsWith("Windows.UI.Xaml", StringComparison.Ordinal) && !nativeXaml.Contains(type.FullName))
            throw new NotSupportedException("Unsupported native XAML class: " + type.FullName);
    }

    private static bool WpfDependencyObject(TypeDefinition type)
    {
        for (TypeDefinition current = type; current != null;
             current = current.BaseType == null || current.BaseType.FullName == "System.Object" ? null : Find(current.BaseType.FullName))
            if (current.FullName == DependencyObjectName) return true;
        return false;
    }

    private static string WpfMember(XmlElement member, TypeDefinition type)
    {
        string path = member.GetAttribute("Wpf");
        if (path.Length == 0 || !path.EndsWith("Property", StringComparison.Ordinal) || path.IndexOf('.') < 0)
            throw new NotSupportedException("Unsupported WPF mapping: " + type.FullName + "." + member.GetAttribute("Name"));
        return "global::" + path;
    }

    private static void WpfDelegate(StringBuilder output, TypeDefinition type, XmlElement surface)
    {
        if (surface.HasChildNodes || !IsDelegate(type)) throw new NotSupportedException("Unsupported WPF delegate surface: " + type.FullName);
        MethodDefinition invoke = type.Methods.Single(m => m.Name == "Invoke");
        output.AppendLine("    public delegate " + (invoke.ReturnType.FullName == "System.Void" ? "void" : Name(invoke.ReturnType)) + " " + type.Name + "(" +
            string.Join(", ", invoke.Parameters.Select(p => Name(p.ParameterType) + " @" + p.Name)) + ");");
    }

    // An interface apps implement and the adapter calls (IValueConverter): methods only.
    private static void WpfInterface(StringBuilder output, TypeDefinition type, XmlElement surface)
    {
        if (surface.HasChildNodes || !type.IsInterface || type.HasProperties || type.HasEvents || type.HasGenericParameters)
            throw new NotSupportedException("Unsupported WPF interface surface: " + type.FullName);
        output.AppendLine("    public interface " + type.Name);
        output.AppendLine("    {");
        foreach (MethodDefinition method in type.Methods)
            output.AppendLine("        " + (method.ReturnType.FullName == "System.Void" ? "void" : Name(method.ReturnType)) + " " + method.Name + "(" +
                string.Join(", ", method.Parameters.Select(p => Name(p.ParameterType) + " @" + p.Name)) + ");");
        output.AppendLine("    }");
    }

    private static void WpfClass(StringBuilder output, TypeDefinition type, XmlElement surface)
    {
        if (surface.Name == "Delegate") { WpfDelegate(output, type, surface); return; }
        if (surface.Name == "Interface") { WpfInterface(output, type, surface); return; }
        if (surface.Name != "Class" || type.IsEnum || type.IsValueType || type.IsInterface || IsDelegate(type))
            throw new NotSupportedException("Unsupported WPF surface: " + type.FullName);
        bool isStatic = type.IsAbstract && type.IsSealed;
        string peer = surface.GetAttribute("Peer");
        TypeDefinition baseClass = BaseClass(type);
        if (baseClass != null && !IsWpf(baseClass.FullName))
            throw new NotSupportedException("Unsupported WPF base: " + type.FullName + " : " + baseClass.FullName);
        bool dependency = WpfDependencyObject(type);
        if (peer.Length != 0 && (!dependency || peer.IndexOf('.') < 0))
            throw new NotSupportedException("Unsupported WPF peer: " + type.FullName + " -> " + peer);
        if (peer.Length != 0) wpfPeers.Add(new KeyValuePair<TypeDefinition, string>(type, peer));
        output.AppendLine("    public " + (isStatic ? "static " : type.IsSealed ? "sealed " : "") + "partial class " + type.Name +
            (baseClass == null ? "" : " : global::" + baseClass.FullName));
        output.AppendLine("    {");
        if (dependency && baseClass == null)
        {
            output.AppendLine("        internal readonly object peer;");
            output.AppendLine("        internal " + type.Name + "(object peer) { this.peer = peer; }");
        }
        else if (dependency)
            output.AppendLine("        internal " + type.Name + "(object peer) : base(peer) { }");
        foreach (XmlElement member in surface.ChildNodes.OfType<XmlElement>())
        {
            string name = member.GetAttribute("Name");
            if (member.Name == "Constructor") WpfConstructor(output, type, peer);
            else if (member.Name == "Property") WpfProperty(output, type, member, name);
            else if (member.Name == "Method") WpfAttached(output, type, member, name);
            else throw new NotSupportedException("Unsupported WPF mapping: " + type.FullName + "." + name);
        }
        output.AppendLine("    }");
    }

    private static void WpfConstructor(StringBuilder output, TypeDefinition type, string peer)
    {
        MethodDefinition constructor = type.Methods.SingleOrDefault(m => m.IsConstructor && !m.IsStatic && m.Parameters.Count == 0);
        if (peer.Length == 0 || constructor == null || !(constructor.IsPublic || constructor.IsFamily))
            throw new NotSupportedException("Unsupported WPF mapping: " + type.FullName + "..ctor");
        output.AppendLine("        " + (constructor.IsPublic ? "public " : "protected ") + type.Name + "() : this(new global::" + peer + "()) { " +
            XamlRuntime + "Peers.Attach(this); }");
    }

    // The UWP value type of a dependency property: its instance property, else its attached getter.
    private static string WpfValueType(TypeDefinition type, string propertyName)
    {
        string name = propertyName.Substring(0, propertyName.Length - "Property".Length);
        PropertyDefinition instance = type.Properties.FirstOrDefault(p => p.Name == name && !p.GetMethod.IsStatic);
        if (instance != null) return Name(instance.PropertyType);
        MethodDefinition getter = type.Methods.FirstOrDefault(m => m.Name == "Get" + name && m.IsStatic && m.Parameters.Count == 1);
        return getter != null ? Name(getter.ReturnType) : "object";
    }

    private static void WpfProperty(StringBuilder output, TypeDefinition type, XmlElement member, string name)
    {
        PropertyDefinition property = type.Properties.SingleOrDefault(p => p.Name == name);
        if (property == null) throw new NotSupportedException("Unsupported WPF mapping: " + type.FullName + "." + name);
        string dp = WpfMember(member, type), value = Name(property.PropertyType);
        if (property.GetMethod.IsStatic)
        {
            if (property.PropertyType.FullName != "Windows.UI.Xaml.DependencyProperty" || property.SetMethod != null || !name.EndsWith("Property", StringComparison.Ordinal))
                throw new NotSupportedException("Unsupported WPF mapping: " + type.FullName + "." + name);
            output.AppendLine("        public static global::Windows.UI.Xaml.DependencyProperty " + name + " { get { return global::Windows.UI.Xaml.DependencyProperty.Of(" +
                dp + ", typeof(" + WpfValueType(type, name) + ")); } }");
            return;
        }
        string wpfName = dp.Substring(dp.LastIndexOf('.') + 1);
        wpfName = wpfName.Substring(0, wpfName.Length - "Property".Length);
        if (wpfName != name) wpfAliases.Add(new[] { type.FullName, name, wpfName });
        output.AppendLine("        public " + value + " " + name);
        output.AppendLine("        {");
        output.AppendLine("            get { return (" + value + ")" + XamlRuntime + "Values.Get(this, " + dp + ", typeof(" + value + ")); }");
        if (property.SetMethod != null)
            output.AppendLine("            set { " + XamlRuntime + "Values.Set(this, " + dp + ", value); }");
        output.AppendLine("        }");
    }

    // Grid.GetRow(element) / SetRow(element, value): attached property accessors.
    private static void WpfAttached(StringBuilder output, TypeDefinition type, XmlElement member, string name)
    {
        MethodDefinition method = type.Methods.SingleOrDefault(m => m.Name == name);
        string dp = WpfMember(member, type);
        bool getter = method != null && method.IsStatic && name.StartsWith("Get", StringComparison.Ordinal) && method.Parameters.Count == 1 &&
            method.ReturnType.FullName != "System.Void";
        bool setter = method != null && method.IsStatic && name.StartsWith("Set", StringComparison.Ordinal) && method.Parameters.Count == 2 &&
            method.ReturnType.FullName == "System.Void";
        if (!getter && !setter) throw new NotSupportedException("Unsupported WPF mapping: " + type.FullName + "." + name);
        string element = Name(method.Parameters[0].ParameterType);
        if (getter)
        {
            string value = Name(method.ReturnType);
            output.AppendLine("        public static " + value + " " + name + "(" + element + " @" + method.Parameters[0].Name + ") { return (" + value + ")" +
                XamlRuntime + "Values.Get(@" + method.Parameters[0].Name + ", " + dp + ", typeof(" + value + ")); }");
        }
        else
            output.AppendLine("        public static void " + name + "(" + element + " @" + method.Parameters[0].Name + ", " + Name(method.Parameters[1].ParameterType) +
                " @" + method.Parameters[1].Name + ") { " + XamlRuntime + "Values.Set(@" + method.Parameters[0].Name + ", " + dp + ", @" + method.Parameters[1].Name + "); }");
    }

    // Wrapper factories for peers WPF creates itself (templates, visual children).
    private static void WpfRegistry(StringBuilder output)
    {
        if (wpfPeers.Count == 0) return;
        output.AppendLine("namespace AppxPortable.WinRT.Xaml");
        output.AppendLine("{");
        output.AppendLine("    internal static class WpfWrappers");
        output.AppendLine("    {");
        output.AppendLine("        internal static void Register()");
        output.AppendLine("        {");
        foreach (var pair in wpfPeers)
            output.AppendLine("            Peers.Register(typeof(global::" + pair.Key.FullName + "), typeof(global::" + pair.Value + "), p => new global::" +
                pair.Key.FullName + "(p));");
        foreach (string[] alias in wpfAliases)
            output.AppendLine("            Peers.Alias(typeof(global::" + alias[0] + "), \"" + alias[1] + "\", \"" + alias[2] + "\");");
        output.AppendLine("        }");
        output.AppendLine("    }");
        output.AppendLine("}");
    }
}
