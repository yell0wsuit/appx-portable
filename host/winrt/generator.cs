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

internal static partial class WinrtProjectionGenerator
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
        var generic = type as GenericInstanceType;
        if (generic != null)
        {
            if (!selected.Contains(generic.ElementType.FullName)) throw new NotSupportedException("Generic type is not selected: " + type.FullName);
            return "global::" + generic.ElementType.FullName.Split('`')[0] + "<" + string.Join(", ", generic.GenericArguments.Select(Name)) + ">";
        }
        if (type.FullName == "System.String") return "string";
        if (type.FullName == "System.Object") return "object";
        if (type.FullName == "Windows.Foundation.Uri") return "global::System.Uri";
        if (type.FullName == "Windows.UI.Xaml.Interop.TypeName") return "global::System.Type";
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
        if (type.FullName == "Windows.Foundation.Uri") return Projected(Find(type.FullName));
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
        if (type.FullName == "Windows.Foundation.Uri") return Wrapper(type) + ".ToNative(" + value + ")";
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
        if (result.FullName == "System.Object") return Runtime + "NativeObject.Inspectable(" + receiver + ".Object(" + number + "))";
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

    // Emit the selected pointer/TypeName ABI directly. Every IID and method slot
    // comes from WinMD; unsupported value/array/generic signatures fail generation.
    private static void Method(StringBuilder output, TypeDefinition type, string name, bool composable)
    {
        var methods = type.Methods.Where(m => m.Name == name && !m.IsConstructor).ToList();
        if (methods.Count == 0) throw new NotSupportedException("Method not found: " + type.FullName + "." + name);
        foreach (MethodDefinition method in methods) MethodOverload(output, type, method, composable, methods.Count > 1);
    }

    private static void MethodOverload(StringBuilder output, TypeDefinition type, MethodDefinition method, bool composable, bool overloaded)
    {
        string name = method.Name;
        bool returns = method.ReturnType.FullName != "System.Void";
        bool enumResult = returns && selected.Contains(method.ReturnType.FullName) && Find(method.ReturnType.FullName).IsEnum;
        string resultAbi = "global::System.IntPtr";
        if (enumResult)
        {
            string width = Find(method.ReturnType.FullName).Fields.Single(f => f.Name == "value__").FieldType.FullName;
            if (width != "System.Int32" && width != "System.UInt32") throw new NotSupportedException("Unsupported enum return: " + method.ReturnType.FullName);
            resultAbi = width == "System.Int32" ? "int" : "uint";
        }
        else if (returns && method.ReturnType.FullName != "System.Object") Parameter(method.ReturnType, type.FullName + "." + name);
        TypeDefinition owner = MemberInterface(type, method);
        int slot = Slot(owner, method);
        string nativeName = name + (overloaded ? "_" + Literal(slot) : "");
        var abi = new List<string>();
        var setup = new List<string>();
        var arguments = new List<string>();
        foreach (ParameterDefinition parameter in method.Parameters)
        {
            string full = parameter.ParameterType.FullName, value = "@" + parameter.Name;
            string local = "argument" + Literal(parameter.Index), prepare;
            string native = "global::System.IntPtr";
            if (full == "System.String") prepare = "new " + Runtime + "NativeString(" + value + ")";
            else if (full == "Windows.UI.Xaml.Interop.TypeName")
            {
                prepare = "new " + Runtime + "NativeTypeName(" + value + ")";
                native = Runtime + "NativeTypeName.Abi";
            }
            else if (full == "System.Object") prepare = Runtime + "NativeObject.Box(" + value + ")";
            else if (selected.Contains(full) && Find(full).IsEnum)
            {
                string width = Find(full).Fields.Single(f => f.Name == "value__").FieldType.FullName;
                if (width != "System.Int32" && width != "System.UInt32") throw new NotSupportedException("Unsupported enum parameter: " + full);
                string scalar = width == "System.Int32" ? "int" : "uint";
                abi.Add(scalar + " " + local);
                arguments.Add("unchecked((" + scalar + ")" + value + ")");
                continue;
            }
            else
            {
                TypeDefinition definition = Parameter(parameter.ParameterType, type.FullName + "." + name);
                prepare = IsDelegate(definition) ? Runtime + "ManagedObject.Hold(" + value + ", " + Projected(definition) + ".Interface)"
                    : Argument(parameter.ParameterType, value);
            }
            bool pointer = full != "System.String" && full != "Windows.UI.Xaml.Interop.TypeName";
            bool held = !pointer ? false : full != "System.Object" && IsDelegate(Find(full));
            arguments.Add(!pointer ? local + ".Value" : held ? local + ".Pointer" : local + " == null ? global::System.IntPtr.Zero : " + local + ".DangerousGetHandle()");
            abi.Add(native + " " + local);
            setup.Add("                using (var " + local + " = " + prepare + ")");
        }
        output.AppendLine("        " + StdCall);
        output.AppendLine("        private delegate int Native_" + nativeName + "(global::System.IntPtr self" +
            string.Concat(abi.Select(p => ", " + p)) + (returns ? ", out " + resultAbi + " result" : "") + ");");
        CustomAttribute overload = Attribute(method, "Windows.Foundation.Metadata.OverloadAttribute");
        string abiName = overload == null ? name : (string)overload.ConstructorArguments[0].Value;
        output.AppendLine("        // " + owner.FullName + "::" + abiName + ", slot " + Literal(slot));
        output.AppendLine("        public " + (method.IsStatic ? "static " : "") + (returns ? Name(method.ReturnType) : "void") + " " + name + "(" +
            string.Join(", ", method.Parameters.Select(p => Name(p.ParameterType) + " @" + p.Name)) + ")");
        output.AppendLine("        {");
        output.AppendLine("            using (var invocation = " + Runtime + "Errors.Begin())");
        output.AppendLine("            {");
        string receiver = Receiver(output, type, owner, method.IsStatic, composable);
        foreach (string line in setup) output.AppendLine(line);
        output.AppendLine("                using (var call = new " + Runtime + "NativeObject.Access(" + receiver + "))");
        output.AppendLine("                {");
        if (returns) output.AppendLine("                    " + resultAbi + " result;");
        output.AppendLine("                    int hr = call.Method<Native_" + nativeName + ">(" + Literal(slot) + ")(call.Self" +
            string.Concat(arguments.Select(p => ", " + p)) + (returns ? ", out result" : "") + ");");
        if (enumResult)
        {
            output.AppendLine("                    " + Runtime + "Errors.Check(hr);");
            output.AppendLine("                    return unchecked((" + Name(method.ReturnType) + ")result);");
        }
        else if (returns)
        {
            string result = Runtime + "NativeObject.ObjectResult(hr, result)";
            output.AppendLine("                    return " + (method.ReturnType.FullName == "System.Object" ? Runtime + "NativeObject.Inspectable(" + result + ")" : Wrapper(method.ReturnType) + ".FromNative(" + result + ")") + ";");
        }
        else output.AppendLine("                    " + Runtime + "Errors.Check(hr);");
        output.AppendLine("                }");
        output.AppendLine("            }");
        output.AppendLine("        }");
    }

    private static void ValidateTypedEvent(TypeDefinition type)
    {
        var invoke = type.Methods.SingleOrDefault(m => m.Name == "Invoke");
        if (type.FullName != "Windows.Foundation.TypedEventHandler`2" || !IsDelegate(type) || type.GenericParameters.Count != 2 ||
            invoke == null || !invoke.HasThis || invoke.IsStatic || invoke.HasGenericParameters || invoke.ReturnType.FullName != "System.Void" ||
            !invoke.Parameters.Select(p => CollectionType(p.ParameterType)).SequenceEqual(new[] { "!0", "!1" }))
            throw new NotSupportedException("Unsupported typed event delegate: " + type.FullName);
    }
    private static string EventSignature(TypeReference type)
    {
        if (type.FullName == "System.Object") return "cinterface(IInspectable)";
        TypeDefinition definition = Parameter(type, "typed event callback");
        if (IsDelegate(definition)) throw new NotSupportedException("Delegate argument in typed event: " + type.FullName);
        return definition.IsInterface ? "{" + Iid(definition) + "}" : "rc(" + type.FullName + ";{" + Iid(DefaultInterface(definition)) + "})";
    }
    private static string TypedEventCallback(StringBuilder output, GenericInstanceType handler, string eventName)
    {
        TypeDefinition definition = Find(handler.ElementType.FullName);
        ValidateTypedEvent(definition);
        if (!selected.Contains(definition.FullName) || handler.GenericArguments.Count != 2)
            throw new NotSupportedException("Unselected typed event: " + handler.FullName);
        string iid = Parameterized(definition, handler.GenericArguments.Select(EventSignature).ToArray());
        string helper = "Native_" + eventName + "_Handler";
        output.AppendLine("        private static class " + helper);
        output.AppendLine("        {");
        output.AppendLine("            " + StdCall);
        output.AppendLine("            private delegate int InvokeCall(global::System.IntPtr self, global::System.IntPtr sender, global::System.IntPtr args);");
        output.AppendLine("            private static readonly InvokeCall invoke = (self, sender, args) => " + Runtime + "ManagedObject.Call(self, target => ((" + Name(handler) + ")target)(" +
            Unwrap(handler.GenericArguments[0], "sender") + ", " + Unwrap(handler.GenericArguments[1], "args") + "));");
        output.AppendLine("            internal static readonly " + Runtime + "NativeInterface Interface = new " + Runtime + "NativeInterface(new global::System.Guid(\"" + iid + "\"), false, invoke);");
        output.AppendLine("        }");
        return helper;
    }

    // A WinRT event as consumers bind it: EventRegistrationToken add_X(handler) and
    // remove_X(token). The handler is a managed callback that native code retains.
    private static void Event(StringBuilder output, TypeDefinition type, string name, bool composable)
    {
        EventDefinition definition = type.Events.SingleOrDefault(e => e.Name == name);
        if (definition == null || definition.AddMethod == null || definition.RemoveMethod == null)
            throw new NotSupportedException("Unsupported event: " + type.FullName + "." + name);
        var closed = definition.EventType as GenericInstanceType;
        TypeDefinition handler = Find(definition.EventType.GetElementType().FullName);
        if (!IsDelegate(handler) || !selected.Contains(handler.FullName))
            throw new NotSupportedException("Unsupported event handler: " + type.FullName + "." + name);
        string callback = closed == null ? Projected(handler) : TypedEventCallback(output, closed, name);
        const string Token = "global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken";
        foreach (MethodDefinition accessor in new[] { definition.AddMethod, definition.RemoveMethod })
        {
            TypeDefinition owner = MemberInterface(type, accessor);
            int slot = Slot(owner, accessor);
            bool add = accessor == definition.AddMethod;
            output.AppendLine("        // " + owner.FullName + "::" + accessor.Name + ", slot " + Literal(slot));
            output.AppendLine("        public " + (accessor.IsStatic ? "static " : "") + (add ? Token : "void") + " " + accessor.Name + "(" +
                (add ? Name(definition.EventType) + " @handler" : Token + " @token") + ")");
            output.AppendLine("        {");
            output.AppendLine("            using (var invocation = " + Runtime + "Errors.Begin())");
            output.AppendLine("            {");
            string receiver = Receiver(output, type, owner, accessor.IsStatic, composable);
            if (add)
            {
                output.AppendLine("                using (var argument = " + Runtime + "ManagedObject.Hold(@handler, " + callback + ".Interface))");
                output.AppendLine("                    return " + receiver + ".AddHandler(" + Literal(slot) + ", argument.Pointer);");
            }
            else output.AppendLine("                    " + receiver + ".RemoveHandler(" + Literal(slot) + ", @token);");
            output.AppendLine("            }");
            output.AppendLine("        }");
        }
    }

    // CLR projects Windows.Foundation.Uri as System.Uri, not a Windows.dll class.
    private static void UriContract(StringBuilder output, TypeDefinition type, XmlElement surface)
    {
        if (type.FullName != "Windows.Foundation.Uri" || surface.Name != "Class" || surface.HasChildNodes)
            throw new NotSupportedException("Unsupported Uri surface");
        TypeDefinition uri = DefaultInterface(type), factory = Find("Windows.Foundation.IUriRuntimeClassFactory");
        CollectionMethod(uri, 0, "get_AbsoluteUri", "()->System.String");
        CollectionMethod(factory, 0, "CreateUri", "(System.String)->Windows.Foundation.Uri");
        output.AppendLine("}");
        output.AppendLine("namespace AppxPortable.WinRT.Projected");
        output.AppendLine("{");
        output.AppendLine("    internal static class Windows_Foundation_Uri");
        output.AppendLine("    {");
        output.AppendLine("        [global::System.Runtime.InteropServices.DllImport(\"combase.dll\")]");
        output.AppendLine("        private static extern int RoGetActivationFactory(global::System.IntPtr name, ref global::System.Guid iid, out global::System.IntPtr value);");
        output.AppendLine("        " + StdCall);
        output.AppendLine("        private delegate int CreateUri(global::System.IntPtr self, global::System.IntPtr text, out global::System.IntPtr value);");
        output.AppendLine("        internal static global::System.Uri FromNative(" + Runtime + "NativeObject value)");
        output.AppendLine("        {");
        output.AppendLine("            if (value == null) return null;");
        output.AppendLine("            using (value)");
        output.AppendLine("            using (var uri = value.QueryInterface(" + Guid(uri) + "))");
        output.AppendLine("                // " + uri.FullName + "::get_AbsoluteUri, slot " + NamedSlot(uri, "get_AbsoluteUri"));
        output.AppendLine("                return new global::System.Uri(uri.String(" + NamedSlot(uri, "get_AbsoluteUri") + "), global::System.UriKind.Absolute);");
        output.AppendLine("        }");
        output.AppendLine("        internal static " + Runtime + "NativeObject ToNative(global::System.Uri value)");
        output.AppendLine("        {");
        output.AppendLine("            if (value == null) return null;");
        output.AppendLine("            if (!value.IsAbsoluteUri) throw new global::System.ArgumentException(\"WinRT requires an absolute URI\", \"value\");");
        output.AppendLine("            using (var name = new " + Runtime + "NativeString(\"Windows.Foundation.Uri\"))");
        output.AppendLine("            using (var text = new " + Runtime + "NativeString(value.AbsoluteUri))");
        output.AppendLine("            {");
        output.AppendLine("                var iid = " + Guid(factory) + ";");
        output.AppendLine("                global::System.IntPtr pointer;");
        output.AppendLine("                int hr = RoGetActivationFactory(name.Value, ref iid, out pointer);");
        output.AppendLine("                using (var factory = " + Runtime + "NativeObject.ObjectResult(hr, pointer))");
        output.AppendLine("                using (var call = new " + Runtime + "NativeObject.Access(factory))");
        output.AppendLine("                {");
        output.AppendLine("                    // " + factory.FullName + "::CreateUri, slot " + NamedSlot(factory, "CreateUri"));
        output.AppendLine("                    hr = call.Method<CreateUri>(" + NamedSlot(factory, "CreateUri") + ")(call.Self, text.Value, out pointer);");
        output.AppendLine("                    return " + Runtime + "NativeObject.ObjectResult(hr, pointer);");
        output.AppendLine("                }");
        output.AppendLine("            }");
        output.AppendLine("        }");
        output.AppendLine("    }");
    }

    private static void ComponentConnectorContract(StringBuilder output, TypeDefinition type, XmlElement surface)
    {
        if (type.FullName != "Windows.UI.Xaml.Markup.IComponentConnector" || surface.Name != "Interface" ||
            surface.HasChildNodes || type.Methods.Count != 1)
            throw new NotSupportedException("Unsupported component connector surface");
        CollectionMethod(type, 0, "Connect", "(System.Int32,System.Object)->System.Void");
        output.AppendLine("    public interface IComponentConnector { void Connect(int connectionId, object target); }");
        output.AppendLine("}");
        output.AppendLine("namespace AppxPortable.WinRT.Projected");
        output.AppendLine("{");
        output.AppendLine("    internal static class Windows_UI_Xaml_Markup_IComponentConnector");
        output.AppendLine("    {");
        output.AppendLine("        " + StdCall);
        output.AppendLine("        private delegate int ConnectCall(global::System.IntPtr self, int id, global::System.IntPtr value);");
        output.AppendLine("        // " + type.FullName + "::Connect, slot " + NamedSlot(type, "Connect"));
        output.AppendLine("        private static readonly ConnectCall connect = (self, id, value) => " + Runtime + "ManagedObject.Call(self, target =>");
        output.AppendLine("            ((global::Windows.UI.Xaml.Markup.IComponentConnector)target).Connect(id, Unwrap(value)));");
        output.AppendLine("        internal static readonly " + Runtime + "NativeInterface Interface = new " + Runtime + "NativeInterface(" + Guid(type) + ", true, connect);");
        output.AppendLine("        private static object Unwrap(global::System.IntPtr pointer)");
        output.AppendLine("        {");
        output.AppendLine("            using (var value = " + Runtime + "NativeObject.Borrow(pointer))");
        output.AppendLine("            {");
        output.AppendLine("                if (value == null) return null;");
        output.AppendLine("                object unboxed;");
        output.AppendLine("                if (" + Runtime + "NativeObject.Unbox(value, out unboxed)) return unboxed;");
        if (selected.Contains("Windows.Foundation.Uri"))
        {
            output.AppendLine("                // CLR redirects are recognized by IID; Wine Uri has no runtime class name.");
            output.AppendLine("                global::System.IntPtr uri;");
            output.AppendLine("                int hr = value.TryQueryInterface(" + Guid(DefaultInterface(Find("Windows.Foundation.Uri"))) + ", out uri);");
            output.AppendLine("                if (hr != unchecked((int)0x80004002)) return " + Wrapper(Find("Windows.Foundation.Uri")) + ".FromNative(" + Runtime + "NativeObject.TakeResult(hr, uri));");
            output.AppendLine("                if (uri != global::System.IntPtr.Zero) global::System.Runtime.InteropServices.Marshal.Release(uri);");
        }
        output.AppendLine("                string name = null;");
        output.AppendLine("                try { name = value.RuntimeClassName(); }");
        output.AppendLine("                catch (global::System.NotImplementedException) { }");
        foreach (TypeDefinition target in classes.Where(c => !c.IsAbstract || !c.IsSealed))
            output.AppendLine("                if (name == \"" + target.FullName + "\") return " + Wrapper(target) + ".FromNative(" + Runtime + "NativeObject.Borrow(pointer));");
        output.AppendLine("                return " + Runtime + "NativeObject.Inspectable(" + Runtime + "NativeObject.Borrow(pointer));");
        output.AppendLine("            }");
        output.AppendLine("        }");
        output.AppendLine("        internal static " + Runtime + "ManagedObject Compose(object target, params " + Runtime + "NativeInterface[] interfaces)");
        output.AppendLine("        {");
        output.AppendLine("            if (target is global::Windows.UI.Xaml.Markup.IComponentConnector)");
        output.AppendLine("            {");
        output.AppendLine("                var extended = new " + Runtime + "NativeInterface[interfaces.Length + 1];");
        output.AppendLine("                global::System.Array.Copy(interfaces, extended, interfaces.Length);");
        output.AppendLine("                extended[interfaces.Length] = Interface;");
        output.AppendLine("                interfaces = extended;");
        output.AppendLine("            }");
        output.AppendLine("            return " + Runtime + "ManagedObject.For(target, interfaces);");
        output.AppendLine("        }");
        output.AppendLine("    }");
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

    // CLR-facing async contracts come from WinMD; they do not provide native
    // wrappers. Each native closed generic operation needs its own ABI adapter.
    private static string AsyncName(TypeReference type)
    {
        if (type is GenericParameter) return "T" + ((GenericParameter)type).Position;
        var generic = type as GenericInstanceType;
        if (generic != null)
            return AsyncName(generic.ElementType) + "<" + string.Join(", ", generic.GenericArguments.Select(AsyncName)) + ">";
        if (type.FullName == "Windows.Foundation.HResult") return "global::System.Exception";
        if (type.FullName == "System.Void") return "void";
        if (type.Namespace == "System" && new[] { "UInt32", "Int32", "Object" }.Contains(type.Name)) return "global::" + type.FullName;
        if (!selected.Contains(type.FullName)) throw new NotSupportedException("Unselected async contract type: " + type.FullName);
        return "global::" + type.FullName.Split('`')[0];
    }
    private static void AsyncContract(StringBuilder output, TypeDefinition type)
    {
        if (type.Namespace != "Windows.Foundation" || (!type.IsInterface && !IsDelegate(type)))
            throw new NotSupportedException("Unsupported async contract: " + type.FullName);
        string name = type.Name.Split('`')[0] + (type.HasGenericParameters ? "<" + string.Join(", ", type.GenericParameters.Select(p => "T" + p.Position)) + ">" : "");
        if (IsDelegate(type))
        {
            MethodDefinition invoke = type.Methods.Single(m => m.Name == "Invoke");
            if (invoke.ReturnType.FullName != "System.Void") throw new NotSupportedException("Async delegate return: " + type.FullName);
            output.AppendLine("    public delegate void " + name + "(" + string.Join(", ", invoke.Parameters.Select(p => AsyncName(p.ParameterType) + " @" + p.Name)) + ");");
            return;
        }
        output.AppendLine("    public interface " + name + (type.Interfaces.Count == 0 ? "" : " : " + string.Join(", ", type.Interfaces.Select(i => AsyncName(i.InterfaceType)))));
        output.AppendLine("    {");
        foreach (PropertyDefinition property in type.Properties)
            output.AppendLine("        " + AsyncName(property.PropertyType) + " " + property.Name + " { " + (property.GetMethod != null ? "get; " : "") + (property.SetMethod != null ? "set; " : "") + "}");
        foreach (MethodDefinition method in type.Methods.Where(m => !m.IsGetter && !m.IsSetter))
        {
            if (method.IsStatic || method.HasGenericParameters || method.Parameters.Any(p => p.ParameterType.IsByReference))
                throw new NotSupportedException("Unsupported async method: " + method.FullName);
            output.AppendLine("        " + AsyncName(method.ReturnType) + " " + method.Name + "(" + string.Join(", ", method.Parameters.Select(p => AsyncName(p.ParameterType) + " @" + p.Name)) + ");");
        }
        output.AppendLine("    }");
    }

    private static string Parameterized(TypeDefinition type, params string[] arguments)
    {
        string signature = "pinterface({" + Iid(type) + "};" + string.Join(";", arguments) + ")";
        byte[] space = { 0x11, 0xf4, 0x7a, 0xd5, 0x7b, 0x73, 0x42, 0xc0, 0xab, 0xae, 0x87, 0x8b, 0x1e, 0x16, 0xad, 0xee };
        byte[] bytes;
        using (var hash = SHA1.Create()) bytes = hash.ComputeHash(space.Concat(Encoding.UTF8.GetBytes(signature)).ToArray());
        bytes[6] = (byte)((bytes[6] & 0x0f) | 0x50);
        bytes[8] = (byte)((bytes[8] & 0x3f) | 0x80);
        return new System.Guid(unchecked((int)((uint)bytes[0] << 24 | (uint)bytes[1] << 16 | (uint)bytes[2] << 8 | bytes[3])),
            unchecked((short)(bytes[4] << 8 | bytes[5])), unchecked((short)(bytes[6] << 8 | bytes[7])), bytes.Skip(8).Take(8).ToArray()).ToString("D");
    }
    private static string CollectionType(TypeReference type)
    {
        var parameter = type as GenericParameter;
        if (parameter != null) return parameter.Type == GenericParameterType.Type ? "!" + parameter.Position : "unsupported";
        var generic = type as GenericInstanceType;
        if (generic != null) return generic.ElementType.FullName + "<" + string.Join(",", generic.GenericArguments.Select(CollectionType)) + ">";
        return type.FullName;
    }
    private static void CollectionMethod(TypeDefinition type, int arity, string name, string signature)
    {
        var found = type.Methods.Where(m => m.Name == name).ToList();
        string actual = found.Count == 1 ? "(" + string.Join(",", found[0].Parameters.Select(p => CollectionType(p.ParameterType))) + ")->" + CollectionType(found[0].ReturnType) : null;
        if (!type.IsInterface || type.GenericParameters.Count != arity || found.Count != 1 || found[0].IsStatic ||
            !found[0].HasThis || found[0].HasGenericParameters || actual != signature)
            throw new NotSupportedException("Unsupported collection ABI: " + type.FullName + "." + name);
    }
    private static int NamedSlot(TypeDefinition owner, string method)
    {
        return Slot(owner, owner.Methods.Single(m => m.Name == method));
    }
    private static void DictionaryInterface(StringBuilder output, TypeDefinition type, XmlElement surface)
    {
        if (surface.GetAttribute("Adapter") != "StringObjectMap" || type.Methods.Count != 0 || surface.HasChildNodes ||
            !type.Interfaces.Any(i => i.InterfaceType.FullName == "Windows.Foundation.Collections.IMap`2<System.String,System.Object>"))
            throw new NotSupportedException("Unsupported dictionary adapter: " + type.FullName);
        var map = Find("Windows.Foundation.Collections.IMap`2");
        var iterable = Find("Windows.Foundation.Collections.IIterable`1");
        var iterator = Find("Windows.Foundation.Collections.IIterator`1");
        var pair = Find("Windows.Foundation.Collections.IKeyValuePair`2");
        foreach (var contract in new[] {
            new { Type = map, Names = new[] { "Lookup", "get_Size", "HasKey", "GetView", "Insert", "Remove", "Clear" } },
            new { Type = iterable, Names = new[] { "First" } },
            new { Type = iterator, Names = new[] { "get_Current", "get_HasCurrent", "MoveNext", "GetMany" } },
            new { Type = pair, Names = new[] { "get_Key", "get_Value" } } })
            if (!contract.Type.Methods.Select(m => m.Name).SequenceEqual(contract.Names))
                throw new NotSupportedException("Unsupported collection ABI: " + contract.Type.FullName + " method order");
        CollectionMethod(map, 2, "Lookup", "(!0)->!1");
        CollectionMethod(map, 2, "get_Size", "()->System.UInt32");
        CollectionMethod(map, 2, "HasKey", "(!0)->System.Boolean");
        CollectionMethod(map, 2, "Insert", "(!0,!1)->System.Boolean");
        CollectionMethod(map, 2, "Remove", "(!0)->System.Void");
        CollectionMethod(map, 2, "Clear", "()->System.Void");
        CollectionMethod(iterable, 1, "First", "()->Windows.Foundation.Collections.IIterator`1<!0>");
        CollectionMethod(iterator, 1, "get_Current", "()->!0");
        CollectionMethod(iterator, 1, "get_HasCurrent", "()->System.Boolean");
        CollectionMethod(iterator, 1, "MoveNext", "()->System.Boolean");
        CollectionMethod(pair, 2, "get_Key", "()->!0");
        CollectionMethod(pair, 2, "get_Value", "()->!1");
        string pairSignature = "pinterface({" + Iid(pair) + "};string;cinterface(IInspectable))";
        string wrapper = type.FullName.Replace('.', '_');
        output.AppendLine("    public interface " + type.Name + " : global::System.Collections.Generic.IDictionary<string, object> { }");
        output.AppendLine("}");
        output.AppendLine("namespace AppxPortable.WinRT.Projected");
        output.AppendLine("{");
        output.AppendLine("    internal sealed class " + wrapper + " : " + Runtime + "NativeStringObjectMap, global::" + type.FullName);
        output.AppendLine("    {");
        output.AppendLine("        private static readonly " + Runtime + "StringObjectMapAbi abi = new " + Runtime + "StringObjectMapAbi {");
        output.AppendLine("            Map = new global::System.Guid(\"" + Parameterized(map, "string", "cinterface(IInspectable)") + "\"),");
        output.AppendLine("            Iterable = new global::System.Guid(\"" + Parameterized(iterable, pairSignature) + "\"),");
        foreach (string name in new[] { "Lookup", "Insert", "Remove", "Clear" }) output.AppendLine("            " + name + " = " + Literal(NamedSlot(map, name)) + ",");
        output.AppendLine("            Size = " + Literal(NamedSlot(map, "get_Size")) + ", HasKey = " + Literal(NamedSlot(map, "HasKey")) + ",");
        output.AppendLine("            First = " + Literal(NamedSlot(iterable, "First")) + ", Current = " + Literal(NamedSlot(iterator, "get_Current")) + ",");
        output.AppendLine("            HasCurrent = " + Literal(NamedSlot(iterator, "get_HasCurrent")) + ", MoveNext = " + Literal(NamedSlot(iterator, "MoveNext")) + ",");
        output.AppendLine("            Key = " + Literal(NamedSlot(pair, "get_Key")) + ", Value = " + Literal(NamedSlot(pair, "get_Value")) + " };");
        output.AppendLine("        private " + wrapper + "(" + Runtime + "NativeObject value) : base(value, abi) { }");
        output.AppendLine("        internal static global::" + type.FullName + " FromNative(" + Runtime + "NativeObject value)");
        output.AppendLine("        {");
        output.AppendLine("            return " + Runtime + "NativeObject.Wrap<global::" + type.FullName + ">(value, " + Guid(type) + ", v => new " + wrapper + "(v));");
        output.AppendLine("        }");
        output.AppendLine("    }");
    }

    private static void Interface(StringBuilder output, TypeDefinition type, XmlElement surface)
    {
        if (type.HasGenericParameters) throw new NotSupportedException("Unsupported generic interface: " + type.FullName);
        if (surface.HasAttribute("Adapter")) { DictionaryInterface(output, type, surface); return; }
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
        if (type.IsAbstract && type.IsSealed)
        {
            if (constructor || overridden.Count != 0 || baseClass != null)
                throw new NotSupportedException("Unsupported static class surface: " + type.FullName);
            output.AppendLine("    public static class " + type.Name);
            output.AppendLine("    {");
            foreach (XmlElement member in surface.ChildNodes.OfType<XmlElement>())
            {
                string name = member.GetAttribute("Name");
                if (member.Name == "Property" && type.Properties.Any(p => p.Name == name && p.GetMethod.IsStatic)) Property(output, type, name, false);
                else if (member.Name == "Method" && type.Methods.Any(m => m.Name == name && m.IsStatic)) Method(output, type, name, false);
                else if (member.Name == "Event" && type.Events.Any(e => e.Name == name && e.AddMethod != null && e.AddMethod.IsStatic)) Event(output, type, name, false);
                else throw new NotSupportedException("Unsupported static member: " + type.FullName + "." + name);
            }
            output.AppendLine("    }");
            return;
        }
        TypeDefinition primary = DefaultInterface(type);
        if (constructor && !composable && (baseClass != null || !type.Methods.Any(m => m.IsConstructor && m.Parameters.Count == 0) ||
            !type.CustomAttributes.Any(a => a.AttributeType.FullName == "Windows.Foundation.Metadata.ActivatableAttribute" &&
                a.ConstructorArguments[0].Type.FullName == "System.UInt32")))
            throw new NotSupportedException("No default activation constructor: " + type.FullName);
        if (overridden.Count != 0 && !constructor) throw new InvalidDataException("Overridable without Constructor: " + type.FullName);

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
        if (constructor && !composable)
        {
            output.AppendLine("        // IActivationFactory::ActivateInstance, slot 6; default interface from WinMD.");
            output.AppendLine("        public " + type.Name + "()");
            output.AppendLine("        {");
            output.AppendLine("            using (var factory = " + Runtime + "NativeObject.Factory(\"" + type.FullName + "\", new global::System.Guid(\"00000035-0000-0000-c000-000000000046\")))");
            output.AppendLine("            using (var value = factory.Object(6))");
            output.AppendLine("                native = value.QueryInterface(" + Guid(primary) + ");");
            output.AppendLine("        }");
        }
        if (constructor && composable)
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
            string compose = selected.Contains("Windows.UI.Xaml.Markup.IComponentConnector")
                ? Projected(Find("Windows.UI.Xaml.Markup.IComponentConnector")) + ".Compose" : Runtime + "ManagedObject.For";
            output.AppendLine("            outer = " + compose + "(this, " + Runtime + "ManagedObject.Identity" +
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
                if (element.GetAttribute("Adapter") == "Uri")
                    UriContract(output, type, element);
                else if (element.GetAttribute("Adapter") == "ComponentConnector")
                    ComponentConnectorContract(output, type, element);
                else if (element.GetAttribute("Adapter") == "Storage")
                    StorageContract(output, type, element);
                else if (element.GetAttribute("Adapter") == "TypedEvent")
                {
                    if (element.Name != "Delegate" || element.HasChildNodes) throw new NotSupportedException("Unsupported typed event surface");
                    ValidateTypedEvent(type);
                    AsyncContract(output, type);
                }
                else if (element.GetAttribute("Adapter") == "Async")
                {
                    if (element.HasChildNodes || (element.Name != "Interface" && element.Name != "Delegate") ||
                        (element.Name == "Interface") != type.IsInterface)
                        throw new NotSupportedException("Unsupported async surface: " + type.FullName);
                    AsyncContract(output, type);
                }
                else if (element.Name == "Enum" && type.IsEnum) Enum(output, type);
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
