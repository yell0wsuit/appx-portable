// Build-time metadata reader only. No SDK metadata or Cecil assembly ships with games.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Xml;

using Mono.Cecil;

internal static class WinrtProjectionGenerator
{
    private const string Runtime = "global::AppxPortable.WinRT.";
    private const string StdCall = "[global::System.Runtime.InteropServices.UnmanagedFunctionPointer(" +
        "global::System.Runtime.InteropServices.CallingConvention.StdCall)]";
    private static readonly Dictionary<string, TypeDefinition> types =
        new Dictionary<string, TypeDefinition>(StringComparer.Ordinal);
    private static readonly HashSet<string> selected = new HashSet<string>(StringComparer.Ordinal);
    private static readonly List<ModuleDefinition> modules = new List<ModuleDefinition>();
    private static readonly Dictionary<string, XmlElement> interfaces = new Dictionary<string, XmlElement>(StringComparer.Ordinal);
    private static readonly List<TypeDefinition> classes = new List<TypeDefinition>();

    private static TypeDefinition Find(string name)
    {
        TypeDefinition value;
        if (!types.TryGetValue(name, out value)) throw new InvalidDataException("Metadata type not found: " + name);
        return value;
    }

    private static bool IsDelegate(TypeDefinition type)
    {
        return type.BaseType != null && type.BaseType.FullName == "System.MulticastDelegate";
    }

    private static string Name(TypeReference type)
    {
        if (type.FullName == "System.String") return "string";
        if (type.FullName == "System.Object") return "object";
        if (!selected.Contains(type.FullName))
            throw new NotSupportedException("Type is not selected: " + type.FullName);
        return "global::" + type.FullName;
    }

    // The expression that wraps a native pointer as a selected type: interfaces use
    // their generated wrapper, which also finds the most derived selected class.
    private static string Wrapper(TypeReference type)
    {
        if (type.FullName == "System.Object") throw new NotSupportedException("Object is wrapped by Unwrap");
        Name(type);
        return interfaces.ContainsKey(type.FullName) ? Projected(Find(type.FullName)) : "global::" + type.FullName;
    }

    // A native pointer argument of a callback as its managed parameter.
    private static string Unwrap(TypeReference type, string pointer)
    {
        string borrowed = Runtime + "NativeObject.Borrow(" + pointer + ")";
        return type.FullName == "System.Object" ? Runtime + "NativeObject.Inspectable(" + borrowed + ")"
            : Wrapper(type) + ".FromNative(" + borrowed + ")";
    }

    // The interface a class or interface parameter is passed as.
    private static string ArgumentIid(TypeReference type)
    {
        TypeDefinition definition = Find(type.FullName);
        return Guid(definition.IsInterface ? definition : DefaultInterface(definition));
    }

    private static string Argument(TypeReference type, string value)
    {
        return Runtime + "NativeObject.Argument(" + value + ", " + ArgumentIid(type) + ")";
    }

    private static string Literal(int value) { return value.ToString(CultureInfo.InvariantCulture); }

    private static string Guid(TypeDefinition type) { return "new global::System.Guid(\"" + Iid(type) + "\")"; }

    private static string Projected(TypeDefinition type)
    {
        return Runtime + "Projected." + type.FullName.Replace('.', '_');
    }

    private static CustomAttribute Attribute(ICustomAttributeProvider provider, string name)
    {
        return provider.CustomAttributes.SingleOrDefault(a => a.AttributeType.FullName == name);
    }

    private static string TypeArgument(CustomAttribute attribute)
    {
        object value = attribute.ConstructorArguments[0].Value;
        return value is TypeReference ? ((TypeReference)value).FullName : ((string)value).Split(',')[0];
    }

    private static string Iid(TypeDefinition type)
    {
        CustomAttribute attribute = type.CustomAttributes.SingleOrDefault(a =>
            a.AttributeType.FullName == "Windows.Foundation.Metadata.GuidAttribute" ||
            a.AttributeType.FullName == "System.Runtime.InteropServices.GuidAttribute");
        if (attribute == null) throw new InvalidDataException("No interface IID: " + type.FullName);
        var values = attribute.ConstructorArguments;
        if (values.Count == 1) return new System.Guid((string)values[0].Value).ToString("D");
        if (values.Count != 11) throw new InvalidDataException("Invalid interface IID: " + type.FullName);
        return new System.Guid(unchecked((int)Convert.ToUInt32(values[0].Value)),
            unchecked((short)Convert.ToUInt16(values[1].Value)), unchecked((short)Convert.ToUInt16(values[2].Value)),
            Convert.ToByte(values[3].Value), Convert.ToByte(values[4].Value), Convert.ToByte(values[5].Value),
            Convert.ToByte(values[6].Value), Convert.ToByte(values[7].Value), Convert.ToByte(values[8].Value),
            Convert.ToByte(values[9].Value), Convert.ToByte(values[10].Value)).ToString("D");
    }

    private static TypeDefinition DefaultInterface(TypeDefinition type)
    {
        InterfaceImplementation implementation = type.Interfaces.SingleOrDefault(i =>
            i.CustomAttributes.Any(a => a.AttributeType.FullName == "Windows.Foundation.Metadata.DefaultAttribute"));
        if (implementation == null) throw new InvalidDataException("No default interface: " + type.FullName);
        return Find(implementation.InterfaceType.FullName);
    }

    private static bool SameSignature(MethodDefinition a, MethodDefinition b)
    {
        return a.Name == b.Name && a.ReturnType.FullName == b.ReturnType.FullName &&
            a.Parameters.Select(p => p.ParameterType.FullName).SequenceEqual(b.Parameters.Select(p => p.ParameterType.FullName));
    }

    // The ABI interface declaring a runtime class member: a static interface, or the default interface.
    private static TypeDefinition MemberInterface(TypeDefinition type, MethodDefinition member)
    {
        if (!member.IsStatic)
        {
            TypeDefinition instance = DefaultInterface(type);
            if (!instance.Methods.Any(m => SameSignature(m, member)))
                throw new NotSupportedException("Member not on default interface: " + type.FullName + "." + member.Name);
            return instance;
        }
        var candidates = new List<TypeDefinition>();
        foreach (CustomAttribute attribute in type.CustomAttributes.Where(a =>
            a.AttributeType.FullName == "Windows.Foundation.Metadata.StaticAttribute"))
        {
            TypeDefinition candidate = Find(TypeArgument(attribute));
            if (candidate.Methods.Any(m => SameSignature(m, member))) candidates.Add(candidate);
        }
        if (candidates.Count != 1) throw new InvalidDataException("Ambiguous static interface: " + type.FullName + "." + member.Name);
        return candidates[0];
    }

    // IInspectable has six slots; WinMD order determines the interface's own slots.
    // Interfaces a WinRT interface requires are separate QueryInterface targets, not vtable bases.
    private static int Slot(TypeDefinition owner, MethodDefinition member)
    {
        var matches = owner.Methods.Where(m => SameSignature(m, member)).ToList();
        if (matches.Count != 1) throw new InvalidDataException("Ambiguous ABI member: " + owner.FullName + "." + member.Name);
        return 6 + owner.Methods.IndexOf(matches[0]);
    }

    private static string Call(MethodDefinition method, string receiver, int slot)
    {
        string number = Literal(slot);
        TypeReference result = method.ReturnType;
        if (result.FullName == "System.String") return receiver + ".String(" + number + ")";
        TypeDefinition definition = Find(result.FullName);
        if (definition.IsEnum)
        {
            string width = definition.Fields.Single(f => f.Name == "value__").FieldType.FullName;
            if (width != "System.Int32" && width != "System.UInt32")
                throw new NotSupportedException("Unsupported enum ABI: " + definition.FullName);
            return "unchecked((" + Name(result) + ")" + receiver + ".UInt32(" + number + "))";
        }
        return Wrapper(result) + ".FromNative(" + receiver + ".Object(" + number + "))";
    }

    // Opens the receiver for an ABI call and returns its name: a static factory,
    // a per-call interface of a composed object, or the default interface.
    private static string Receiver(StringBuilder output, TypeDefinition type, TypeDefinition owner, bool isStatic, bool composable)
    {
        if (isStatic)
        {
            output.AppendLine("                using (var factory = " + Runtime + "NativeObject.Factory(\"" + type.FullName + "\", " + Guid(owner) + "))");
            return "factory";
        }
        if (!composable) return "native";
        output.AppendLine("                using (var self = native.QueryInterface(" + Guid(owner) + "))");
        return "self";
    }

    private static void Enum(StringBuilder output, TypeDefinition type)
    {
        string width = type.Fields.Single(f => f.Name == "value__").FieldType.FullName;
        string underlying = width == "System.Int32" ? "int" : width == "System.UInt32" ? "uint" : null;
        if (underlying == null) throw new NotSupportedException("Unsupported enum ABI: " + type.FullName);
        output.AppendLine("    public enum " + type.Name + " : " + underlying);
        output.AppendLine("    {");
        foreach (FieldDefinition field in type.Fields.Where(f => f.IsLiteral))
            output.AppendLine("        " + field.Name + " = " + Convert.ToString(field.Constant, CultureInfo.InvariantCulture) +
                (underlying == "uint" ? "U" : "") + ",");
        output.AppendLine("    }");
    }

    // A selected runtime class or interface passed by pointer, or a selected delegate passed as a managed callback.
    private static TypeDefinition Parameter(TypeReference type, string use)
    {
        TypeDefinition definition = Find(type.FullName);
        if (!selected.Contains(type.FullName) || definition.IsEnum || definition.IsValueType)
            throw new NotSupportedException("Unsupported parameter type: " + type.FullName + " in " + use);
        return definition;
    }

    private static void Delegate(StringBuilder output, TypeDefinition type)
    {
        MethodDefinition invoke = type.Methods.Single(m => m.Name == "Invoke");
        if (invoke.ReturnType.FullName != "System.Void") throw new NotSupportedException("Unsupported delegate return: " + type.FullName);
        var parameters = invoke.Parameters.ToList();
        foreach (ParameterDefinition parameter in parameters.Where(p => p.ParameterType.FullName != "System.Object"))
            if (IsDelegate(Parameter(parameter.ParameterType, type.FullName)))
                throw new NotSupportedException("Unsupported delegate parameter: " + type.FullName);
        output.AppendLine("    public delegate void " + type.Name + "(" +
            string.Join(", ", parameters.Select(p => Name(p.ParameterType) + " @" + p.Name)) + ");");
        output.AppendLine("}");
        output.AppendLine("namespace AppxPortable.WinRT.Projected");
        output.AppendLine("{");
        output.AppendLine("    // " + type.FullName + " as a native callback; delegates are IUnknown-based.");
        output.AppendLine("    internal static class " + type.FullName.Replace('.', '_'));
        output.AppendLine("    {");
        output.AppendLine("        " + StdCall);
        output.AppendLine("        private delegate int InvokeCall(global::System.IntPtr self" +
            string.Concat(parameters.Select(p => ", global::System.IntPtr @" + p.Name)) + ");");
        output.AppendLine("        private static readonly InvokeCall invoke = (self" + string.Concat(parameters.Select(p => ", @" + p.Name)) +
            ") => " + Runtime + "ManagedObject.Call(self, target => ((global::" + type.FullName + ")target)(" +
            string.Join(", ", parameters.Select(p => Unwrap(p.ParameterType, "@" + p.Name))) + "));");
        output.AppendLine("        internal static readonly " + Runtime + "NativeInterface Interface = new " + Runtime +
            "NativeInterface(" + Guid(type) + ", false, invoke);");
        output.AppendLine("    }");
    }

    private static void Method(StringBuilder output, TypeDefinition type, string name, bool composable)
    {
        var methods = type.Methods.Where(m => m.Name == name && !m.IsConstructor).ToList();
        if (methods.Count != 1) throw new NotSupportedException("Method not found or overloaded: " + type.FullName + "." + name);
        MethodDefinition method = methods[0];
        if (method.ReturnType.FullName != "System.Void") throw new NotSupportedException("Unsupported method return: " + type.FullName + "." + name);
        if (method.Parameters.Count > 1) throw new NotSupportedException("Unsupported method arity: " + type.FullName + "." + name);
        TypeDefinition owner = MemberInterface(type, method);
        int slot = Slot(owner, method);
        ParameterDefinition parameter = method.Parameters.SingleOrDefault();
        TypeDefinition argument = parameter == null ? null : Parameter(parameter.ParameterType, type.FullName + "." + name);
        output.AppendLine("        // " + owner.FullName + "::" + name + ", slot " + Literal(slot));
        output.AppendLine("        public " + (method.IsStatic ? "static " : "") + "void " + name + "(" +
            (parameter == null ? "" : Name(parameter.ParameterType) + " @" + parameter.Name) + ")");
        output.AppendLine("        {");
        output.AppendLine("            using (var invocation = " + Runtime + "Errors.Begin())");
        output.AppendLine("            {");
        string receiver = Receiver(output, type, owner, method.IsStatic, composable);
        if (argument == null) output.AppendLine("                    " + receiver + ".Call(" + Literal(slot) + ");");
        else if (IsDelegate(argument))
        {
            output.AppendLine("                using (var argument = " + Runtime + "ManagedObject.Hold(@" + parameter.Name + ", " + Projected(argument) + ".Interface))");
            output.AppendLine("                    " + receiver + ".Call(" + Literal(slot) + ", argument.Pointer);");
        }
        else
        {
            output.AppendLine("                using (var argument = " + Argument(parameter.ParameterType, "@" + parameter.Name) + ")");
            output.AppendLine("                    " + receiver + ".Call(" + Literal(slot) + ", argument);");
        }
        output.AppendLine("            }");
        output.AppendLine("        }");
    }

    // A WinRT event as consumers bind it: EventRegistrationToken add_X(handler) and
    // remove_X(token). The handler is a managed callback that native code retains.
    private static void Event(StringBuilder output, TypeDefinition type, string name, bool composable)
    {
        EventDefinition definition = type.Events.SingleOrDefault(e => e.Name == name);
        if (definition == null || definition.AddMethod == null || definition.RemoveMethod == null)
            throw new NotSupportedException("Unsupported event: " + type.FullName + "." + name);
        TypeDefinition handler = Find(definition.EventType.FullName);
        if (!IsDelegate(handler) || !selected.Contains(handler.FullName))
            throw new NotSupportedException("Unsupported event handler: " + type.FullName + "." + name);
        const string Token = "global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken";
        foreach (MethodDefinition accessor in new[] { definition.AddMethod, definition.RemoveMethod })
        {
            TypeDefinition owner = MemberInterface(type, accessor);
            int slot = Slot(owner, accessor);
            bool add = accessor == definition.AddMethod;
            output.AppendLine("        // " + owner.FullName + "::" + accessor.Name + ", slot " + Literal(slot));
            output.AppendLine("        public " + (accessor.IsStatic ? "static " : "") + (add ? Token : "void") + " " + accessor.Name + "(" +
                (add ? Name(handler) + " @handler" : Token + " @token") + ")");
            output.AppendLine("        {");
            output.AppendLine("            using (var invocation = " + Runtime + "Errors.Begin())");
            output.AppendLine("            {");
            string receiver = Receiver(output, type, owner, accessor.IsStatic, composable);
            if (add)
            {
                output.AppendLine("                using (var argument = " + Runtime + "ManagedObject.Hold(@handler, " + Projected(handler) + ".Interface))");
                output.AppendLine("                    return " + receiver + ".AddHandler(" + Literal(slot) + ", argument.Pointer);");
            }
            else output.AppendLine("                    " + receiver + ".RemoveHandler(" + Literal(slot) + ", @token);");
            output.AppendLine("            }");
            output.AppendLine("        }");
        }
    }

    private static void Property(StringBuilder output, TypeDefinition type, string name, bool composable)
    {
        PropertyDefinition definition = type.Properties.SingleOrDefault(p => p.Name == name);
        if (definition == null || definition.GetMethod == null || definition.Parameters.Count != 0)
            throw new NotSupportedException("Unsupported property: " + type.FullName + "." + name);
        MethodDefinition getter = definition.GetMethod, setter = definition.SetMethod;
        TypeDefinition owner = MemberInterface(type, getter);
        int slot = Slot(owner, getter);
        output.AppendLine("        // " + owner.FullName + "::" + getter.Name + ", slot " + Literal(slot));
        if (setter != null)
        {
            Parameter(setter.Parameters[0].ParameterType, type.FullName + "." + setter.Name);
            if (IsDelegate(Find(setter.Parameters[0].ParameterType.FullName)))
                throw new NotSupportedException("Unsupported property setter: " + type.FullName + "." + name);
            output.AppendLine("        // " + MemberInterface(type, setter).FullName + "::" + setter.Name + ", slot " +
                Literal(Slot(MemberInterface(type, setter), setter)));
        }
        output.AppendLine("        public " + (getter.IsStatic ? "static " : "") + Name(getter.ReturnType) + " " + name);
        output.AppendLine("        {");
        output.AppendLine("            get");
        output.AppendLine("            {");
        string receiver = Receiver(output, type, owner, getter.IsStatic, composable);
        output.AppendLine("                    return " + Call(getter, receiver, slot) + ";");
        output.AppendLine("            }");
        if (setter != null)
        {
            TypeDefinition writer = MemberInterface(type, setter);
            output.AppendLine("            set");
            output.AppendLine("            {");
            output.AppendLine("                using (var invocation = " + Runtime + "Errors.Begin())");
            output.AppendLine("                {");
            receiver = Receiver(output, type, writer, setter.IsStatic, composable);
            output.AppendLine("                using (var argument = " + Argument(setter.Parameters[0].ParameterType, "value") + ")");
            output.AppendLine("                    " + receiver + ".Call(" + Literal(Slot(writer, setter)) + ", argument);");
            output.AppendLine("                }");
            output.AppendLine("            }");
        }
        output.AppendLine("        }");
    }

    private static PropertyDefinition Getter(TypeDefinition owner, string name)
    {
        PropertyDefinition definition = owner.Properties.SingleOrDefault(p => p.Name == name);
        if (definition == null || definition.GetMethod == null || definition.SetMethod != null || definition.Parameters.Count != 0)
            throw new NotSupportedException("Unsupported property: " + owner.FullName + "." + name);
        return definition;
    }

    // A property of a selected interface on an implementing wrapper. A class
    // queries for the interface per call; the interface's own wrapper holds it.
    private static void InterfaceProperty(StringBuilder output, TypeDefinition iface, string name, bool query)
    {
        MethodDefinition getter = Getter(iface, name).GetMethod;
        int slot = Slot(iface, getter);
        output.AppendLine("        // " + iface.FullName + "::" + getter.Name + ", slot " + Literal(slot));
        output.AppendLine("        public " + Name(getter.ReturnType) + " " + name);
        output.AppendLine("        {");
        output.AppendLine("            get");
        output.AppendLine("            {");
        if (query) output.AppendLine("                using (var self = native.QueryInterface(" + Guid(iface) + "))");
        output.AppendLine("                    return " + Call(getter, query ? "self" : "native", slot) + ";");
        output.AppendLine("            }");
        output.AppendLine("        }");
    }

    private static List<string> InterfaceMembers(TypeDefinition iface, XmlElement surface)
    {
        var members = new List<string>();
        foreach (XmlElement member in surface.ChildNodes.OfType<XmlElement>())
        {
            if (member.Name != "Property") throw new NotSupportedException("Unsupported member kind: " + iface.FullName + "." + member.Name);
            if (members.Contains(member.GetAttribute("Name")))
                throw new InvalidDataException("Duplicate property: " + iface.FullName + "." + member.GetAttribute("Name"));
            members.Add(member.GetAttribute("Name"));
        }
        if (members.Count == 0 && iface.Methods.Count != 0) throw new InvalidDataException("Empty runtime surface: " + iface.FullName);
        return members;
    }

    private static void Interface(StringBuilder output, TypeDefinition type, XmlElement surface)
    {
        if (type.HasGenericParameters) throw new NotSupportedException("Unsupported generic interface: " + type.FullName);
        List<string> members = InterfaceMembers(type, surface);
        output.AppendLine("    public interface " + type.Name);
        output.AppendLine("    {");
        foreach (string name in members) output.AppendLine("        " + Name(Getter(type, name).GetMethod.ReturnType) + " " + name + " { get; }");
        output.AppendLine("    }");
        output.AppendLine("}");
        output.AppendLine("namespace AppxPortable.WinRT.Projected");
        output.AppendLine("{");
        string wrapper = type.FullName.Replace('.', '_');
        output.AppendLine("    // " + type.FullName + " over a native object of no selected class.");
        output.AppendLine("    internal sealed class " + wrapper + " : global::" + type.FullName + ", " + Runtime + "INativeWrapper");
        output.AppendLine("    {");
        output.AppendLine("        private readonly " + Runtime + "NativeObject native;");
        output.AppendLine("        private " + wrapper + "(" + Runtime + "NativeObject value) { native = value; }");
        output.AppendLine("        " + Runtime + "NativeObject " + Runtime + "INativeWrapper.Native { get { return native; } }");
        output.AppendLine("        internal static global::" + type.FullName + " FromNative(" + Runtime + "NativeObject value)");
        output.AppendLine("        {");
        output.AppendLine("            return " + Runtime + "NativeObject.Wrap<global::" + type.FullName + ">(value, " + Guid(type) + ", v =>");
        output.AppendLine("            {");
        output.AppendLine("                string name = v.RuntimeClassName();");
        foreach (TypeDefinition implementer in classes.Where(c => c.Interfaces.Any(i => i.InterfaceType.FullName == type.FullName)))
            output.AppendLine("                if (name == \"" + implementer.FullName + "\") return global::" + implementer.FullName + ".Create(v);");
        output.AppendLine("                return new " + wrapper + "(v);");
        output.AppendLine("            });");
        output.AppendLine("        }");
        foreach (string name in members) InterfaceProperty(output, type, name, false);
        output.AppendLine("    }");
    }

    private static TypeDefinition BaseClass(TypeDefinition type)
    {
        if (type.BaseType == null || type.BaseType.FullName == "System.Object") return null;
        if (!selected.Contains(type.BaseType.FullName))
            throw new NotSupportedException("Base class not selected: " + type.FullName + " : " + type.BaseType.FullName);
        return Find(type.BaseType.FullName);
    }

    private static IEnumerable<TypeDefinition> Chain(TypeDefinition type)
    {
        for (TypeDefinition current = type; current != null; current = BaseClass(current)) yield return current;
    }

    private static List<TypeDefinition> Overridables(TypeDefinition type)
    {
        return type.Interfaces.Where(i => Attribute(i, "Windows.Foundation.Metadata.OverridableAttribute") != null)
            .Select(i => Find(i.InterfaceType.FullName)).ToList();
    }

    private static void Class(StringBuilder output, TypeDefinition type, XmlElement surface)
    {
        CustomAttribute composition = Attribute(type, "Windows.Foundation.Metadata.ComposableAttribute");
        bool composable = composition != null;
        if (type.BaseType == null || (!type.IsSealed && !composable))
            throw new NotSupportedException("Unsupported runtime class shape: " + type.FullName);
        TypeDefinition baseClass = BaseClass(type);
        var members = new HashSet<string>(StringComparer.Ordinal);
        var overridden = new List<string>();
        bool constructor = false;
        foreach (XmlElement member in surface.ChildNodes.OfType<XmlElement>())
        {
            string kind = member.Name, name = member.GetAttribute("Name");
            if (kind != "Property" && kind != "Method" && kind != "Constructor" && kind != "Overridable" && kind != "Event")
                throw new NotSupportedException("Unsupported member kind: " + type.FullName + "." + kind);
            if (!members.Add(kind == "Constructor" ? ".ctor" : name))
                throw new InvalidDataException("Duplicate " + kind.ToLowerInvariant() + ": " + type.FullName + "." + name);
            if (kind == "Constructor") constructor = true;
            if (kind == "Overridable") overridden.Add(name);
        }
        TypeDefinition primary = DefaultInterface(type);
        if (constructor && !composable) throw new NotSupportedException("Constructor needs a composable class: " + type.FullName);
        if (overridden.Count != 0 && !constructor) throw new InvalidDataException("Overridable without Constructor: " + type.FullName);
        if (overridden.Count != 0 && baseClass != null) throw new NotSupportedException("Overridable on a derived class: " + type.FullName);

        // Overridable interfaces with a selected member are answered by the managed outer;
        // the others, across the class chain, keep the native behavior, and a managed
        // override of one fails construction.
        List<TypeDefinition> overridable = Overridables(type);
        foreach (string name in overridden)
            if (overridable.Count(i => i.Methods.Any(m => m.Name == name)) != 1)
                throw new NotSupportedException("Unsupported overridable: " + type.FullName + "." + name);
        var exposed = overridable.Where(i => i.Methods.Any(m => overridden.Contains(m.Name))).ToList();
        var unsupported = Chain(type).SelectMany(Overridables).SelectMany(i => i.Methods).Select(m => m.Name)
            .Where(n => !overridden.Contains(n)).Distinct().ToList();
        // Unsealed classes may be composed or wrap a more derived object: query per call.
        bool query = !type.IsSealed;
        bool root = baseClass == null;
        string hide = root ? "" : "new ";

        var implemented = type.Interfaces.Select(i => i.InterfaceType.FullName).Where(interfaces.ContainsKey).ToList();
        var bases = new List<string>();
        if (!root) bases.Add("global::" + baseClass.FullName);
        bases.AddRange(implemented.Select(i => "global::" + i));
        if (root) bases.Add(Runtime + "INativeWrapper");
        output.AppendLine("    public " + (type.IsSealed ? "sealed " : "") + "class " + type.Name +
            (bases.Count == 0 ? "" : " : " + string.Join(", ", bases)));
        output.AppendLine("    {");
        if (root)
        {
            // The non-delegating inner of a composed object, or a native object's interface.
            output.AppendLine("        internal " + Runtime + "NativeObject native;");
            if (composable) output.AppendLine("        internal " + Runtime + "ManagedObject outer;");
            output.AppendLine("        " + Runtime + "NativeObject " + Runtime + "INativeWrapper.Native { get { return native; } }");
            output.AppendLine("        internal " + type.Name + "(" + Runtime + "NativeObject value) { native = value; }");
            if (composable) output.AppendLine("        internal " + type.Name + "(" + Runtime + "Composing marker) { }");
        }
        else
        {
            output.AppendLine("        internal " + type.Name + "(" + Runtime + "NativeObject value) : base(value) { }");
            output.AppendLine("        internal " + type.Name + "(" + Runtime + "Composing marker) : base(marker) { }");
        }
        var derived = classes.Where(c => c != type && Chain(c).Skip(1).Contains(type)).ToList();
        output.AppendLine("        internal static " + hide + type.Name + " FromNative(" + Runtime + "NativeObject value)");
        output.AppendLine("        {");
        output.AppendLine("            return " + Runtime + "NativeObject.Wrap<" + type.Name + ">(value, " + Guid(primary) + ", v =>");
        output.AppendLine("            {");
        if (derived.Count != 0)
        {
            output.AppendLine("                string name = v.RuntimeClassName();");
            foreach (TypeDefinition subclass in derived)
                output.AppendLine("                if (name == \"" + subclass.FullName + "\") return global::" + subclass.FullName + ".Create(v);");
        }
        output.AppendLine("                return new " + type.Name + "(v);");
        output.AppendLine("            });");
        output.AppendLine("        }");
        output.AppendLine("        // A wrapper for an identity already known to be this class; disposes value.");
        output.AppendLine("        internal static " + hide + type.Name + " Create(" + Runtime + "NativeObject value)");
        output.AppendLine("        {");
        output.AppendLine("            using (value) return new " + type.Name + "(value.QueryInterface(" + Guid(primary) + "));");
        output.AppendLine("        }");
        if (constructor)
        {
            int kind = Convert.ToInt32(composition.ConstructorArguments[1].Value, CultureInfo.InvariantCulture);
            if (kind != 1 && kind != 2) throw new NotSupportedException("Unsupported composition type: " + type.FullName);
            TypeDefinition factory = Find(TypeArgument(composition));
            var create = factory.Methods.Where(m => m.Name == "CreateInstance" && m.Parameters.Count == 2).ToList();
            if (create.Count != 1 || factory.Methods.Count != 1)
                throw new NotSupportedException("Unsupported composable factory: " + factory.FullName);
            int slot = Slot(factory, create[0]);
            output.AppendLine("        // " + factory.FullName + "::CreateInstance, slot " + Literal(slot));
            output.AppendLine("        " + (kind == 2 ? "public " : "protected ") + type.Name + "()" +
                (root ? "" : " : base(" + Runtime + "Composing.Instance)"));
            output.AppendLine("        {");
            if (unsupported.Count != 0)
                output.AppendLine("            " + Runtime + "ManagedObject.RequireOverrides(GetType(), typeof(" + type.Name + "), " +
                    string.Join(", ", unsupported.Select(n => "\"" + n + "\"")) + ");");
            output.AppendLine("            outer = " + Runtime + "ManagedObject.For(this, " + Runtime + "ManagedObject.Identity" +
                string.Concat(exposed.Select((i, n) => ", Overrides" + Literal(n) + ".Interface")) + ");");
            output.AppendLine("            using (var factory = " + Runtime + "NativeObject.Factory(\"" + type.FullName + "\", " + Guid(factory) + "))");
            output.AppendLine("                native = factory.Compose(" + Literal(slot) + ", outer.Pointer);");
            output.AppendLine("            outer.Aggregate(native);");
            output.AppendLine("        }");
        }
        if (exposed.Count != 0)
        {
            output.AppendLine("        private void BaseOverride(global::System.Guid iid, int slot, global::System.IntPtr value)");
            output.AppendLine("        {");
            output.AppendLine("            using (var overrides = native.QueryInterface(iid)) overrides.Call(slot, value);");
            output.AppendLine("        }");
        }
        for (int n = 0; n < exposed.Count; ++n)
        {
            TypeDefinition iface = exposed[n];
            output.AppendLine("        // " + iface.FullName + ", answered by the managed outer.");
            output.AppendLine("        private static class Overrides" + Literal(n));
            output.AppendLine("        {");
            output.AppendLine("            " + StdCall);
            output.AppendLine("            private delegate int Call1(global::System.IntPtr self, global::System.IntPtr value);");
            var thunks = new List<string>();
            foreach (MethodDefinition method in iface.Methods)
            {
                int slot = Slot(iface, method);
                TypeDefinition parameter = method.Parameters.Count == 1 ? Find(method.Parameters[0].ParameterType.FullName) : null;
                if (method.ReturnType.FullName != "System.Void" || parameter == null || parameter.IsValueType)
                    throw new NotSupportedException("Unsupported overridable shape: " + iface.FullName + "." + method.Name);
                string thunk = "on" + method.Name;
                string body = overridden.Contains(method.Name)
                    ? "((" + type.Name + ")target)." + method.Name + "(" + Unwrap(method.Parameters[0].ParameterType, "value") + ")"
                    : "((" + type.Name + ")target).BaseOverride(" + Guid(iface) + ", " + Literal(slot) + ", value)";
                output.AppendLine("            // slot " + Literal(slot));
                output.AppendLine("            private static readonly Call1 " + thunk + " = (self, value) => " + Runtime +
                    "ManagedObject.Call(self, target => " + body + ");");
                thunks.Add(thunk);
            }
            output.AppendLine("            internal static readonly " + Runtime + "NativeInterface Interface = new " + Runtime +
                "NativeInterface(" + Guid(iface) + ", true, " + string.Join(", ", thunks) + ");");
            output.AppendLine("        }");
            foreach (MethodDefinition method in iface.Methods.Where(m => overridden.Contains(m.Name)))
            {
                ParameterDefinition parameter = method.Parameters[0];
                Parameter(parameter.ParameterType, iface.FullName + "." + method.Name);
                output.AppendLine("        // " + iface.FullName + "::" + method.Name + ", slot " + Literal(Slot(iface, method)));
                output.AppendLine("        protected virtual void " + method.Name + "(" + Name(parameter.ParameterType) + " @" + parameter.Name + ")");
                output.AppendLine("        {");
                output.AppendLine("            using (var overrides = native.QueryInterface(" + Guid(iface) + "))");
                output.AppendLine("            using (var argument = " + Argument(parameter.ParameterType, "@" + parameter.Name) + ")");
                output.AppendLine("                overrides.Call(" + Literal(Slot(iface, method)) + ", argument);");
                output.AppendLine("        }");
            }
        }
        foreach (XmlElement member in surface.ChildNodes.OfType<XmlElement>())
        {
            if (member.Name == "Property") Property(output, type, member.GetAttribute("Name"), query);
            else if (member.Name == "Method") Method(output, type, member.GetAttribute("Name"), query);
            else if (member.Name == "Event") Event(output, type, member.GetAttribute("Name"), query);
        }
        foreach (string name in implemented)
        {
            TypeDefinition iface = Find(name);
            foreach (string property in InterfaceMembers(iface, interfaces[name]).Where(p => !members.Contains(p)))
                InterfaceProperty(output, iface, property, true);
        }
        output.AppendLine("    }");
    }

    private static string Hash(string path)
    {
        using (var stream = File.OpenRead(path))
        using (var sha = SHA256.Create())
            return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();
    }

    private static int Main(string[] args)
    {
        try
        {
            if (args.Length != 3) throw new ArgumentException("generator METADATA_DIRECTORY SURFACE_XML OUTPUT_CS");
            var resolver = new DefaultAssemblyResolver();
            resolver.AddSearchDirectory(args[0]);
            foreach (string path in Directory.GetFiles(args[0], "*.winmd").OrderBy(p => p, StringComparer.Ordinal))
            {
                var module = ModuleDefinition.ReadModule(path, new ReaderParameters
                {
                    ApplyWindowsRuntimeProjections = false,
                    AssemblyResolver = resolver,
                    InMemory = true
                });
                modules.Add(module);
                foreach (TypeDefinition type in module.Types.Where(t => t.Name != "<Module>"))
                {
                    if (types.ContainsKey(type.FullName)) throw new InvalidDataException("Duplicate metadata type: " + type.FullName);
                    types.Add(type.FullName, type);
                }
            }
            var document = new XmlDocument { XmlResolver = null };
            using (XmlReader reader = XmlReader.Create(args[1], new XmlReaderSettings
            {
                DtdProcessing = DtdProcessing.Prohibit,
                XmlResolver = null
            })) document.Load(reader);
            if (document.DocumentElement == null || document.DocumentElement.Name != "Projection")
                throw new InvalidDataException("Expected Projection surface root");
            var elements = document.DocumentElement.ChildNodes.OfType<XmlElement>().ToList();
            foreach (XmlElement element in elements)
            {
                if (element.Name != "Class" && element.Name != "Enum" && element.Name != "Delegate" && element.Name != "Interface")
                    throw new NotSupportedException("Unsupported surface kind: " + element.Name);
                if (!selected.Add(element.GetAttribute("Name"))) throw new InvalidDataException("Duplicate selected type");
                if (element.Name == "Interface") interfaces.Add(element.GetAttribute("Name"), element);
                if (element.Name == "Class") classes.Add(Find(element.GetAttribute("Name")));
            }
            var output = new StringBuilder();
            output.AppendLine("// Generated from SDK metadata. Do not edit.");
            output.AppendLine("// Surface SHA256 " + Hash(args[1]));
            foreach (ModuleDefinition module in selected.Select(n => Find(n).Module).Distinct().OrderBy(m => m.Name, StringComparer.Ordinal))
                output.AppendLine("// " + module.Name + " SHA256 " + Hash(module.FileName));
            output.AppendLine("[assembly: global::System.Reflection.AssemblyVersion(\"255.255.255.255\")]");
            foreach (XmlElement element in elements.OrderBy(e => e.GetAttribute("Name"), StringComparer.Ordinal))
            {
                TypeDefinition type = Find(element.GetAttribute("Name"));
                output.AppendLine("namespace " + type.Namespace);
                output.AppendLine("{");
                if (element.Name == "Enum" && type.IsEnum) Enum(output, type);
                else if (element.Name == "Delegate" && IsDelegate(type)) Delegate(output, type);
                else if (element.Name == "Interface" && type.IsInterface) Interface(output, type, element);
                else if (element.Name == "Class" && !type.IsEnum && !IsDelegate(type) && !type.IsValueType && !type.IsInterface)
                    Class(output, type, element);
                else throw new InvalidDataException("Surface kind does not match metadata: " + type.FullName);
                output.AppendLine("}");
            }
            File.WriteAllText(args[2], output.ToString(), new UTF8Encoding(false));
            Console.WriteLine("Generated " + selected.Count + " metadata types into " + args[2]);
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error.Message); return 1; }
        finally { foreach (ModuleDefinition module in modules) module.Dispose(); }
    }
}
