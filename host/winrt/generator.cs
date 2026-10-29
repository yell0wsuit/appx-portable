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
        if (!selected.Contains(type.FullName))
            throw new NotSupportedException("Type is not selected: " + type.FullName);
        return "global::" + type.FullName;
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
        return Name(result) + ".FromNative(" + receiver + ".Object(" + number + "))";
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

    // A selected runtime class passed by pointer, or a selected delegate passed as a managed callback.
    private static TypeDefinition Parameter(TypeReference type, string use)
    {
        TypeDefinition definition = Find(type.FullName);
        if (!selected.Contains(type.FullName) || definition.IsEnum || definition.IsValueType || definition.IsInterface)
            throw new NotSupportedException("Unsupported parameter type: " + type.FullName + " in " + use);
        return definition;
    }

    private static void Delegate(StringBuilder output, TypeDefinition type)
    {
        MethodDefinition invoke = type.Methods.Single(m => m.Name == "Invoke");
        if (invoke.ReturnType.FullName != "System.Void") throw new NotSupportedException("Unsupported delegate return: " + type.FullName);
        var parameters = invoke.Parameters.ToList();
        foreach (ParameterDefinition parameter in parameters)
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
            string.Join(", ", parameters.Select(p => Name(p.ParameterType) + ".FromNative(" + Runtime + "NativeObject.Borrow(@" + p.Name + "))")) + "));");
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
        else output.AppendLine("                    " + receiver + ".Call(" + Literal(slot) + ", @" + parameter.Name + " == null ? null : @" + parameter.Name + ".native);");
        output.AppendLine("            }");
        output.AppendLine("        }");
    }

    private static void Property(StringBuilder output, TypeDefinition type, string name, bool composable)
    {
        PropertyDefinition definition = type.Properties.SingleOrDefault(p => p.Name == name);
        if (definition == null || definition.GetMethod == null || definition.SetMethod != null || definition.Parameters.Count != 0)
            throw new NotSupportedException("Unsupported property: " + type.FullName + "." + name);
        MethodDefinition getter = definition.GetMethod;
        TypeDefinition owner = MemberInterface(type, getter);
        int slot = Slot(owner, getter);
        output.AppendLine("        // " + owner.FullName + "::" + getter.Name + ", slot " + Literal(slot));
        output.AppendLine("        public " + (getter.IsStatic ? "static " : "") + Name(getter.ReturnType) + " " + name);
        output.AppendLine("        {");
        output.AppendLine("            get");
        output.AppendLine("            {");
        string receiver = Receiver(output, type, owner, getter.IsStatic, composable);
        output.AppendLine("                    return " + Call(getter, receiver, slot) + ";");
        output.AppendLine("            }");
        output.AppendLine("        }");
    }

    private static void Class(StringBuilder output, TypeDefinition type, XmlElement surface)
    {
        CustomAttribute composition = Attribute(type, "Windows.Foundation.Metadata.ComposableAttribute");
        bool composable = composition != null;
        if (type.BaseType == null || type.BaseType.FullName != "System.Object" || (!type.IsSealed && !composable))
            throw new NotSupportedException("Unsupported runtime class shape: " + type.FullName);
        var members = new HashSet<string>(StringComparer.Ordinal);
        var overridden = new List<string>();
        bool constructor = false;
        foreach (XmlElement member in surface.ChildNodes.OfType<XmlElement>())
        {
            string kind = member.Name, name = member.GetAttribute("Name");
            if (kind != "Property" && kind != "Method" && kind != "Constructor" && kind != "Overridable")
                throw new NotSupportedException("Unsupported member kind: " + type.FullName + "." + kind);
            if (!members.Add(kind == "Constructor" ? ".ctor" : name))
                throw new InvalidDataException("Duplicate " + kind.ToLowerInvariant() + ": " + type.FullName + "." + name);
            if (kind == "Constructor") constructor = true;
            if (kind == "Overridable") overridden.Add(name);
        }
        TypeDefinition primary = DefaultInterface(type);
        if (members.Count == 0 && primary.Methods.Count != 0) throw new InvalidDataException("Empty runtime surface: " + type.FullName);
        if (constructor && !composable) throw new NotSupportedException("Constructor needs a composable class: " + type.FullName);
        if (overridden.Count != 0 && !constructor) throw new InvalidDataException("Overridable without Constructor: " + type.FullName);

        // Overridable interfaces with a selected member are answered by the managed outer;
        // the others keep the native behavior, and a managed override of one fails construction.
        var overridable = type.Interfaces.Where(i => Attribute(i, "Windows.Foundation.Metadata.OverridableAttribute") != null)
            .Select(i => Find(i.InterfaceType.FullName)).ToList();
        foreach (string name in overridden)
            if (overridable.Count(i => i.Methods.Any(m => m.Name == name)) != 1)
                throw new NotSupportedException("Unsupported overridable: " + type.FullName + "." + name);
        var exposed = overridable.Where(i => i.Methods.Any(m => overridden.Contains(m.Name))).ToList();
        var unsupported = overridable.SelectMany(i => i.Methods).Select(m => m.Name).Where(n => !overridden.Contains(n)).ToList();

        output.AppendLine("    public " + (type.IsSealed ? "sealed " : "") + "class " + type.Name);
        output.AppendLine("    {");
        output.AppendLine("        internal readonly " + Runtime + "NativeObject native;");
        if (constructor) output.AppendLine("        private readonly " + Runtime + "ManagedObject outer;");
        output.AppendLine("        private " + type.Name + "(" + Runtime + "NativeObject value) { native = value; }");
        output.AppendLine("        internal static " + type.Name + " FromNative(" + Runtime + "NativeObject value)");
        output.AppendLine("        {");
        output.AppendLine("            return " + Runtime + "NativeObject.Wrap<" + type.Name + ">(value, " + Guid(primary) + ", v => new " + type.Name + "(v));");
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
            output.AppendLine("        " + (kind == 2 ? "public " : "protected ") + type.Name + "()");
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
                    ? "((" + type.Name + ")target)." + method.Name + "(" + Name(method.Parameters[0].ParameterType) + ".FromNative(" +
                      Runtime + "NativeObject.Borrow(value)))"
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
                output.AppendLine("                overrides.Call(" + Literal(Slot(iface, method)) + ", @" + parameter.Name + " == null ? null : @" +
                    parameter.Name + ".native);");
                output.AppendLine("        }");
            }
        }
        foreach (XmlElement member in surface.ChildNodes.OfType<XmlElement>())
        {
            if (member.Name == "Property") Property(output, type, member.GetAttribute("Name"), composable);
            else if (member.Name == "Method") Method(output, type, member.GetAttribute("Name"), composable);
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
                var module = ModuleDefinition.ReadModule(path, new ReaderParameters {
                    ApplyWindowsRuntimeProjections = false, AssemblyResolver = resolver, InMemory = true });
                modules.Add(module);
                foreach (TypeDefinition type in module.Types.Where(t => t.Name != "<Module>"))
                {
                    if (types.ContainsKey(type.FullName)) throw new InvalidDataException("Duplicate metadata type: " + type.FullName);
                    types.Add(type.FullName, type);
                }
            }
            var document = new XmlDocument { XmlResolver = null };
            using (XmlReader reader = XmlReader.Create(args[1], new XmlReaderSettings {
                DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null })) document.Load(reader);
            if (document.DocumentElement == null || document.DocumentElement.Name != "Projection")
                throw new InvalidDataException("Expected Projection surface root");
            var elements = document.DocumentElement.ChildNodes.OfType<XmlElement>().ToList();
            foreach (XmlElement element in elements)
            {
                if (element.Name != "Class" && element.Name != "Enum" && element.Name != "Delegate")
                    throw new NotSupportedException("Unsupported surface kind: " + element.Name);
                if (!selected.Add(element.GetAttribute("Name"))) throw new InvalidDataException("Duplicate selected type");
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
