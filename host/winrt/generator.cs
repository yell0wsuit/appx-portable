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
    private static readonly Dictionary<string, XmlElement> classSurfaces = new Dictionary<string, XmlElement>(StringComparer.Ordinal);

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
        ValidateExternalIdentity(type);
        if (Scalar(type) != null) return type.FullName == "System.Boolean" ? "bool" : type.FullName == "System.Char" ? "char" : Scalar(type);
        var array = type as ArrayType;
        if (array != null && array.IsVector && array.ElementType.FullName == "System.Byte") return "byte[]";
        var generic = type as GenericInstanceType;
        if (generic != null)
        {
            if (!selected.Contains(generic.ElementType.FullName)) throw new NotSupportedException("Generic type is not selected: " + type.FullName);
            if (generic.ElementType.FullName == MapViewName) return "global::System.Collections.Generic.IReadOnlyDictionary<string, " + Name(MapViewValue(generic)) + ">";
            // The CLR projects Windows.Foundation.EventHandler<T> as System.EventHandler<T>.
            string element = generic.ElementType.FullName == EventHandlerName ? "System.EventHandler" : generic.ElementType.FullName.Split('`')[0];
            return "global::" + element + "<" + string.Join(", ", generic.GenericArguments.Select(Name)) + ">";
        }
        if (type.FullName == "System.Boolean") return "bool";
        if (type.FullName == "System.String") return "string";
        if (type.FullName == "System.Object") return "object";
        if (type.FullName == "Windows.Foundation.Uri") return "global::System.Uri";
        if (type.FullName == "Windows.UI.Xaml.Interop.TypeName") return "global::System.Type";
        if (type.FullName == "Windows.Foundation.TimeSpan") return "global::System.TimeSpan";
        if (type.FullName == DateTimeName) return "global::System.DateTimeOffset";
        if (!Available(type.FullName))
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
        // The hand-written storage adapter wraps native buffers and actions.
        if (type.FullName == "Windows.Storage.Streams.IBuffer") return "global::Windows.Storage.Streams.Buffer";
        if (type.FullName == "Windows.Foundation.IAsyncAction") return Runtime + "NativeAction";
        return interfaces.ContainsKey(type.FullName) ? Projected(Find(type.FullName)) : "global::" + type.FullName;
    }

    // Selected Windows object results share the connector's qualified type dispatch.
    private static string Inspectable(string value)
    {
        const string connector = "Windows.UI.Xaml.Markup.IComponentConnector";
        return selected.Contains(connector) ? Projected(Find(connector)) + ".FromNativeObject(" + value + ")"
            : Runtime + "NativeObject.Inspectable(" + value + ")";
    }

    // A native pointer argument of a callback as its managed parameter.
    private static string Unwrap(TypeReference type, string pointer)
    {
        string borrowed = Runtime + "NativeObject.Borrow(" + pointer + ")";
        return type.FullName == "System.Object" ? Inspectable(borrowed)
            : Wrapped(type, borrowed);
    }

    // The interface a class or interface parameter is passed as.
    private static string ArgumentIid(TypeReference type)
    {
        TypeDefinition definition = Find(type.FullName);
        if (external.ContainsKey(type.FullName))
            return "new global::System.Guid(\"" + (string)Attribute(definition, TypeDescriptor).ConstructorArguments[0].Value + "\")";
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

    // The ABI interface declaring a runtime class member: a static interface, the default
    // interface, or the one other interface the class implements that declares it.
    private static TypeDefinition MemberInterface(TypeDefinition type, MethodDefinition member)
    {
        if (!member.IsStatic)
        {
            TypeDefinition instance = DefaultInterface(type);
            if (instance.Methods.Any(m => SameSignature(m, member))) return instance;
            var declaring = type.Interfaces.Select(i => i.InterfaceType).Where(i => !(i is GenericInstanceType))
                .Select(i => Find(i.FullName)).Where(i => i.Methods.Any(m => SameSignature(m, member))).ToList();
            if (declaring.Count != 1)
                throw new NotSupportedException("Member not on default interface: " + type.FullName + "." + member.Name);
            return declaring[0];
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
        if (result.FullName == "System.Boolean") return receiver + ".Boolean(" + number + ")";
        if (result.FullName == "System.String") return receiver + ".String(" + number + ")";
        // Other scalars travel in the 32- or 64-bit slot getters; floats as their bits.
        switch (result.FullName)
        {
            case "System.UInt32": return receiver + ".UInt32(" + number + ")";
            case "System.Byte":
            case "System.SByte":
            case "System.Int16":
            case "System.UInt16":
            case "System.Int32":
            case "System.Char":
                return "unchecked((" + Name(result) + ")" + receiver + ".UInt32(" + number + "))";
            case "System.Int64": return receiver + ".Int64(" + number + ")";
            case "System.UInt64": return "unchecked((ulong)" + receiver + ".Int64(" + number + "))";
            case "System.Double": return "global::System.BitConverter.Int64BitsToDouble(" + receiver + ".Int64(" + number + "))";
            case "System.Single": return "global::System.BitConverter.ToSingle(global::System.BitConverter.GetBytes(" + receiver + ".UInt32(" + number + ")), 0)";
        }
        if (result.FullName == "System.Object") return Inspectable(receiver + ".Object(" + number + ")");
        if (result.FullName == "Windows.Foundation.TimeSpan") return "new global::System.TimeSpan(" + receiver + ".Int64(" + number + "))";
        // The CLR projects DateTime, FILETIME ticks in UTC, as a local DateTimeOffset.
        if (result.FullName == DateTimeName) return "global::System.DateTimeOffset.FromFileTime(" + receiver + ".Int64(" + number + "))";
        var view = result as GenericInstanceType;
        if (view != null && view.ElementType.FullName == MapViewName)
            return Runtime + "NativeStringMapView<" + Name(MapViewValue(view)) + ">.Wrap(" + receiver + ".Object(" + number + "), " +
                StringMapViewAbi(view) + ", v => " + Wrapped(MapViewValue(view), "v") + ")";
        TypeDefinition definition = Find(result.FullName);
        if (definition.IsEnum)
        {
            string width = definition.Fields.Single(f => f.Name == "value__").FieldType.FullName;
            if (width != "System.Int32" && width != "System.UInt32")
                throw new NotSupportedException("Unsupported enum ABI: " + definition.FullName);
            return "unchecked((" + Name(result) + ")" + receiver + ".UInt32(" + number + "))";
        }
        return Wrapped(result, receiver + ".Object(" + number + ")");
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
        if (!composable && owner == DefaultInterface(type)) return "native";
        output.AppendLine("                using (var self = native.QueryInterface(" + Guid(owner) + "))");
        return "self";
    }

    private static void Enum(StringBuilder output, TypeDefinition type)
    {
        string width = type.Fields.Single(f => f.Name == "value__").FieldType.FullName;
        string underlying = width == "System.Int32" ? "int" : width == "System.UInt32" ? "uint" : null;
        if (underlying == null) throw new NotSupportedException("Unsupported enum ABI: " + type.FullName);
        if (interop) output.AppendLine("    [" + Interop + "ProjectionType(\"\")]");
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
        if (!Available(type.FullName) || definition.IsEnum || definition.IsValueType)
            throw new NotSupportedException("Unsupported parameter type: " + type.FullName + " in " + use);
        return definition;
    }

    private static void Delegate(StringBuilder output, TypeDefinition type)
    {
        MethodDefinition invoke = type.Methods.Single(m => m.Name == "Invoke");
        var parameters = invoke.Parameters.ToList();
        foreach (ParameterDefinition parameter in parameters)
        {
            string full = parameter.ParameterType.FullName;
            if (Scalar(parameter.ParameterType) != null || full == "System.String" || full == "System.Object") continue;
            if (IsDelegate(Parameter(parameter.ParameterType, type.FullName)))
                throw new NotSupportedException("Unsupported delegate parameter: " + type.FullName);
        }
        bool returns = invoke.ReturnType.FullName != "System.Void";
        string resultAbi = Scalar(invoke.ReturnType);
        bool text = invoke.ReturnType.FullName == "System.String";
        if (returns && resultAbi == null && !text) throw new NotSupportedException("Unsupported delegate return: " + type.FullName);
        if (text) resultAbi = "global::System.IntPtr";
        output.AppendLine("    public delegate " + (returns ? Name(invoke.ReturnType) : "void") + " " + type.Name + "(" +
            string.Join(", ", parameters.Select(p => Name(p.ParameterType) + " @" + p.Name)) + ");");
        output.AppendLine("}");
        output.AppendLine("namespace AppxPortable.WinRT.Projected");
        output.AppendLine("{");
        output.AppendLine("    // " + type.FullName + " as a native callback; delegates are IUnknown-based.");
        output.AppendLine("    internal static class " + type.FullName.Replace('.', '_'));
        output.AppendLine("    {");
        string signature = "global::System.IntPtr self" + string.Concat(parameters.Select(p => ", " + (Scalar(p.ParameterType) ?? "global::System.IntPtr") + " @" + p.Name)) +
            (returns ? ", out " + resultAbi + " result" : "");
        output.AppendLine("        " + StdCall);
        output.AppendLine("        private delegate int InvokeCall(" + signature + ");");
        output.AppendLine("        private static readonly InvokeCall invoke = Invoke;");
        output.AppendLine("        private static int Invoke(" + signature + ")");
        output.AppendLine("        {");
        if (returns) output.AppendLine("            " + resultAbi + " value = default(" + resultAbi + ");");
        output.AppendLine("            int hr = " + Runtime + "ManagedObject.Call(self, target =>");
        output.AppendLine("            {");
        string call = "((global::" + type.FullName + ")target)(" + string.Join(", ", parameters.Select(p =>
            p.ParameterType.FullName == "System.Boolean" ? "@" + p.Name + " != 0" :
            p.ParameterType.FullName == "System.Char" ? "(char)@" + p.Name :
            Scalar(p.ParameterType) != null ? "@" + p.Name :
            p.ParameterType.FullName == "System.String" ? Runtime + "NativeString.Read(@" + p.Name + ")" : Unwrap(p.ParameterType, "@" + p.Name))) + ")";
        if (text) output.AppendLine("                using (var resultString = new " + Runtime + "NativeString(" + call + ")) value = resultString.Detach();");
        else if (returns) output.AppendLine("                value = " + (invoke.ReturnType.FullName == "System.Boolean" ? call + " ? (byte)1 : (byte)0" : invoke.ReturnType.FullName == "System.Char" ? "(ushort)" + call : call) + ";");
        else output.AppendLine("                " + call + ";");
        output.AppendLine("            });");
        if (returns) output.AppendLine("            result = value;");
        output.AppendLine("            return hr;");
        output.AppendLine("        }");
        output.AppendLine("        internal static readonly " + Runtime + "NativeInterface Interface = new " + Runtime +
            "NativeInterface(" + Guid(type) + ", false, invoke);");
        output.AppendLine("    }");
    }

    // Emit the selected pointer/TypeName/scalar/input-byte-array ABI directly.
    // Every IID and slot comes from WinMD; unsupported signatures fail generation.
    // Arities, when given, selects overloads by parameter count, each matching exactly one.
    private static void Method(StringBuilder output, TypeDefinition type, string name, bool composable, string arities = "")
    {
        var methods = type.Methods.Where(m => m.Name == name && !m.IsConstructor).ToList();
        if (methods.Count == 0) throw new NotSupportedException("Method not found: " + type.FullName + "." + name);
        bool overloaded = methods.Count > 1;
        if (arities.Length != 0)
        {
            var counts = arities.Split(' ').Select(a => int.Parse(a, CultureInfo.InvariantCulture)).ToList();
            if (counts.Distinct().Count() != counts.Count || counts.Any(c => methods.Count(m => m.Parameters.Count == c) != 1))
                throw new NotSupportedException("Unsupported overload arities: " + type.FullName + "." + name + " " + arities);
            methods = methods.Where(m => counts.Contains(m.Parameters.Count)).ToList();
        }
        foreach (MethodDefinition method in methods) MethodOverload(output, type, method, composable, overloaded);
    }

    // The ABI parameters, the using lines that prepare them, and the call arguments of a method.
    private static void MarshalArguments(TypeDefinition type, MethodDefinition method, List<string> abi, List<string> setup, List<string> arguments)
    {
        foreach (ParameterDefinition parameter in method.Parameters)
        {
            string full = parameter.ParameterType.FullName, value = "@" + parameter.Name;
            string local = "argument" + Literal(parameter.Index), prepare;
            // A fill array is [out] but passed by value; a receive array is by reference.
            if ((parameter.IsOut && !(parameter.ParameterType is ArrayType)) || parameter.ParameterType.IsByReference || parameter.ParameterType.IsPointer)
                throw new NotSupportedException("Unsupported parameter: " + method.FullName + " " + parameter.Name);
            if (full == DateTimeName)
            {
                abi.Add("long " + local);
                arguments.Add(value + ".ToFileTime()");
                continue;
            }
            string scalar = Scalar(parameter.ParameterType);
            if (scalar != null)
            {
                abi.Add(scalar + " " + local);
                arguments.Add(full == "System.Boolean" ? value + " ? (byte)1 : (byte)0" : full == "System.Char" ? "(ushort)" + value : value);
                continue;
            }
            var array = parameter.ParameterType as ArrayType;
            if (array != null)
            {
                // Pass and fill arrays share an ABI; the native side writes a fill array through the pin.
                if (!array.IsVector || array.ElementType.FullName != "System.Byte")
                    throw new NotSupportedException("Unsupported array: " + method.FullName + " " + parameter.Name);
                abi.Add("uint " + local + "Length"); abi.Add("global::System.IntPtr " + local);
                setup.Add("                using (var " + local + " = new " + Runtime + "PinnedBytes(" + value + "))");
                arguments.Add(local + ".Length"); arguments.Add(local + ".Pointer");
                continue;
            }
            string native = "global::System.IntPtr";
            if (full == "System.String") prepare = "new " + Runtime + "NativeString(" + value + ")";
            else if (full == "Windows.UI.Xaml.Interop.TypeName")
            {
                prepare = "new " + Runtime + "NativeTypeName(" + value + ")";
                native = Runtime + "NativeTypeName.Abi";
            }
            else if (full == "System.Object") prepare = Runtime + "NativeObject.Box(" + value + ")";
            else if (Available(full) && Find(full).IsEnum)
            {
                string width = Find(full).Fields.Single(f => f.Name == "value__").FieldType.FullName;
                if (width != "System.Int32" && width != "System.UInt32") throw new NotSupportedException("Unsupported enum parameter: " + full);
                string enumScalar = width == "System.Int32" ? "int" : "uint";
                abi.Add(enumScalar + " " + local);
                arguments.Add("unchecked((" + enumScalar + ")" + value + ")");
                continue;
            }
            else
            {
                TypeDefinition definition = Parameter(parameter.ParameterType, type.FullName + "." + method.Name);
                prepare = IsDelegate(definition) ? Runtime + "ManagedObject.Hold(" + value + ", " + Projected(definition) + ".Interface)"
                    : Argument(parameter.ParameterType, value);
            }
            bool pointer = full != "System.String" && full != "Windows.UI.Xaml.Interop.TypeName";
            bool held = !pointer ? false : full != "System.Object" && IsDelegate(Find(full));
            arguments.Add(!pointer ? local + ".Value" : held ? local + ".Pointer" : local + " == null ? global::System.IntPtr.Zero : " + local + ".DangerousGetHandle()");
            abi.Add(native + " " + local);
            setup.Add("                using (var " + local + " = " + prepare + ")");
        }
    }

    private static void MethodOverload(StringBuilder output, TypeDefinition type, MethodDefinition method, bool composable, bool overloaded)
    {
        string name = method.Name;
        bool returns = method.ReturnType.FullName != "System.Void";
        bool enumResult = returns && Available(method.ReturnType.FullName) && Find(method.ReturnType.FullName).IsEnum;
        string scalarResult = Scalar(method.ReturnType);
        bool stringResult = method.ReturnType.FullName == "System.String";
        string resultAbi = scalarResult ?? "global::System.IntPtr";
        if (enumResult)
        {
            string width = Find(method.ReturnType.FullName).Fields.Single(f => f.Name == "value__").FieldType.FullName;
            if (width != "System.Int32" && width != "System.UInt32") throw new NotSupportedException("Unsupported enum return: " + method.ReturnType.FullName);
            resultAbi = width == "System.Int32" ? "int" : "uint";
        }
        else if (returns && scalarResult == null && !stringResult && method.ReturnType.FullName != "System.Object") Parameter(method.ReturnType, type.FullName + "." + name);
        TypeDefinition owner = MemberInterface(type, method);
        int slot = Slot(owner, method);
        string nativeName = name + (overloaded ? "_" + Literal(slot) : "");
        var abi = new List<string>();
        var setup = new List<string>();
        var arguments = new List<string>();
        MarshalArguments(type, method, abi, setup, arguments);
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
        else if (scalarResult != null)
        {
            output.AppendLine("                    " + Runtime + "Errors.Check(hr);");
            output.AppendLine("                    return " + (method.ReturnType.FullName == "System.Boolean" ? "result != 0" : method.ReturnType.FullName == "System.Char" ? "(char)result" : "result") + ";");
        }
        else if (stringResult) output.AppendLine("                    return " + Runtime + "NativeString.TakeResult(hr, result);");
        else if (returns)
        {
            string result = Runtime + "NativeObject.ObjectResult(hr, result)";
            output.AppendLine("                    return " + (method.ReturnType.FullName == "System.Object" ? Inspectable(result) : Wrapped(method.ReturnType, result)) + ";");
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
    private const string EventHandlerName = "Windows.Foundation.EventHandler`1";
    private static void ValidateEventHandler(TypeDefinition type)
    {
        var invoke = type.Methods.SingleOrDefault(m => m.Name == "Invoke");
        if (type.FullName != EventHandlerName || !IsDelegate(type) || type.GenericParameters.Count != 1 ||
            invoke == null || !invoke.HasThis || invoke.IsStatic || invoke.HasGenericParameters || invoke.ReturnType.FullName != "System.Void" ||
            !invoke.Parameters.Select(p => CollectionType(p.ParameterType)).SequenceEqual(new[] { "System.Object", "!0" }))
            throw new NotSupportedException("Unsupported event handler delegate: " + type.FullName);
    }
    // EventHandler<T> has an IInspectable sender and a T argument.
    private static string EventHandlerCallback(StringBuilder output, GenericInstanceType handler, string eventName)
    {
        TypeDefinition definition = Find(handler.ElementType.FullName);
        ValidateEventHandler(definition);
        if (!selected.Contains(definition.FullName) || handler.GenericArguments.Count != 1)
            throw new NotSupportedException("Unselected event handler: " + handler.FullName);
        string iid = Parameterized(definition, EventSignature(handler.GenericArguments[0]));
        string helper = "Native_" + eventName + "_Handler";
        output.AppendLine("        private static class " + helper);
        output.AppendLine("        {");
        output.AppendLine("            " + StdCall);
        output.AppendLine("            private delegate int InvokeCall(global::System.IntPtr self, global::System.IntPtr sender, global::System.IntPtr args);");
        output.AppendLine("            private static readonly InvokeCall invoke = (self, sender, args) => " + Runtime + "ManagedObject.Call(self, target => ((" + Name(handler) + ")target)(" +
            Inspectable(Runtime + "NativeObject.Borrow(sender)") + ", " + Unwrap(handler.GenericArguments[0], "args") + "));");
        output.AppendLine("            internal static readonly " + Runtime + "NativeInterface Interface = new " + Runtime + "NativeInterface(new global::System.Guid(\"" + iid + "\"), false, invoke);");
        output.AppendLine("        }");
        return helper;
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
        string callback = closed == null ? Projected(handler)
            : handler.FullName == EventHandlerName ? EventHandlerCallback(output, closed, name) : TypedEventCallback(output, closed, name);
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
        output.AppendLine("        internal static object FromNativeObject(" + Runtime + "NativeObject value)");
        output.AppendLine("        {");
        output.AppendLine("            using (value) return value == null ? null : Unwrap(value.DangerousGetHandle());");
        output.AppendLine("        }");
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
        output.AppendLine("    }");
    }

    // CLR-redirected WinRT structs. Games bind them to System.Runtime.WindowsRuntime(.UI.Xaml),
    // whose appx-portable facades forward here so projected members can use them. Each keeps
    // its SDK field layout behind the public API .NET gives it.
    private static readonly Dictionary<string, string[]> ClrStructFields = new Dictionary<string, string[]>(StringComparer.Ordinal) {
        { "Windows.Foundation.Point", new[] { "X:System.Single", "Y:System.Single" } },
        { "Windows.Foundation.Size", new[] { "Width:System.Single", "Height:System.Single" } },
        { "Windows.Foundation.Rect", new[] { "X:System.Single", "Y:System.Single", "Width:System.Single", "Height:System.Single" } },
        { "Windows.UI.Color", new[] { "A:System.Byte", "R:System.Byte", "G:System.Byte", "B:System.Byte" } },
        { "Windows.UI.Xaml.Thickness", new[] { "Left:System.Double", "Top:System.Double", "Right:System.Double", "Bottom:System.Double" } },
        { "Windows.UI.Xaml.Duration", new[] { "TimeSpan:Windows.Foundation.TimeSpan", "Type:Windows.UI.Xaml.DurationType" } } };
    private static void ClrStruct(StringBuilder output, TypeDefinition type, XmlElement surface)
    {
        string[] fields;
        if (surface.Name != "Struct" || surface.HasChildNodes || !type.IsValueType || type.IsEnum ||
            !ClrStructFields.TryGetValue(type.FullName, out fields) ||
            !type.Fields.Where(f => !f.IsStatic).Select(f => f.Name + ":" + f.FieldType.FullName).SequenceEqual(fields))
            throw new NotSupportedException("Unsupported CLR-redirected struct: " + type.FullName);
        if (type.FullName == "Windows.UI.Xaml.Duration" && !selected.Contains("Windows.UI.Xaml.DurationType"))
            throw new NotSupportedException("Duration needs Windows.UI.Xaml.DurationType");
        output.AppendLine("    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Sequential)]");
        output.Append(ClrStructTemplates[type.FullName]);
    }
    private static readonly Dictionary<string, string> ClrStructTemplates = new Dictionary<string, string>(StringComparer.Ordinal) {
        { "Windows.Foundation.Point", @"    public struct Point
    {
        private float _x, _y;
        public Point(double x, double y) { _x = (float)x; _y = (float)y; }
        public double X { get { return _x; } set { _x = (float)value; } }
        public double Y { get { return _y; } set { _y = (float)value; } }
        public bool Equals(Point value) { return X == value.X && Y == value.Y; }
        public override bool Equals(object o) { return o is Point && Equals((Point)o); }
        public override int GetHashCode() { return X.GetHashCode() ^ Y.GetHashCode(); }
        public static bool operator ==(Point a, Point b) { return a.Equals(b); }
        public static bool operator !=(Point a, Point b) { return !a.Equals(b); }
        public override string ToString() { return X.ToString() + "","" + Y.ToString(); }
    }
" },
        { "Windows.Foundation.Size", @"    public struct Size
    {
        private float _width, _height;
        public Size(double width, double height)
        {
            if (width < 0 || height < 0) throw new global::System.ArgumentException(""Size dimensions cannot be negative"");
            _width = (float)width; _height = (float)height;
        }
        public double Width
        {
            get { return _width; }
            set { if (IsEmpty || value < 0) throw new global::System.ArgumentException(""Invalid Size width""); _width = (float)value; }
        }
        public double Height
        {
            get { return _height; }
            set { if (IsEmpty || value < 0) throw new global::System.ArgumentException(""Invalid Size height""); _height = (float)value; }
        }
        public static Size Empty { get { var value = new Size(); value._width = value._height = float.NegativeInfinity; return value; } }
        public bool IsEmpty { get { return _width < 0; } }
        public bool Equals(Size value) { return Width == value.Width && Height == value.Height; }
        public override bool Equals(object o) { return o is Size && Equals((Size)o); }
        public override int GetHashCode() { return Width.GetHashCode() ^ Height.GetHashCode(); }
        public static bool operator ==(Size a, Size b) { return a.Equals(b); }
        public static bool operator !=(Size a, Size b) { return !a.Equals(b); }
        public override string ToString() { return IsEmpty ? ""Empty"" : Width.ToString() + "","" + Height.ToString(); }
    }
" },
        { "Windows.Foundation.Rect", @"    public struct Rect
    {
        private float _x, _y, _width, _height;
        public Rect(double x, double y, double width, double height)
        {
            if (width < 0 || height < 0) throw new global::System.ArgumentException(""Rect dimensions cannot be negative"");
            _x = (float)x; _y = (float)y; _width = (float)width; _height = (float)height;
        }
        public Rect(Point point1, Point point2)
            : this(global::System.Math.Min(point1.X, point2.X), global::System.Math.Min(point1.Y, point2.Y),
                   global::System.Math.Abs(point2.X - point1.X), global::System.Math.Abs(point2.Y - point1.Y)) { }
        public Rect(Point location, Size size) : this(location.X, location.Y, size.Width, size.Height) { }
        public double X { get { return _x; } set { _x = (float)value; } }
        public double Y { get { return _y; } set { _y = (float)value; } }
        public double Width
        {
            get { return _width; }
            set { if (value < 0) throw new global::System.ArgumentException(""Rect width cannot be negative""); _width = (float)value; }
        }
        public double Height
        {
            get { return _height; }
            set { if (value < 0) throw new global::System.ArgumentException(""Rect height cannot be negative""); _height = (float)value; }
        }
        public double Left { get { return X; } }
        public double Top { get { return Y; } }
        public double Right { get { return IsEmpty ? double.NegativeInfinity : X + Width; } }
        public double Bottom { get { return IsEmpty ? double.NegativeInfinity : Y + Height; } }
        public static Rect Empty
        {
            get
            {
                var value = new Rect();
                value._x = value._y = float.PositiveInfinity;
                value._width = value._height = float.NegativeInfinity;
                return value;
            }
        }
        public bool IsEmpty { get { return _width < 0; } }
        public bool Contains(Point point) { return point.X >= X && point.X <= Right && point.Y >= Y && point.Y <= Bottom; }
        public bool Equals(Rect value) { return X == value.X && Y == value.Y && Width == value.Width && Height == value.Height; }
        public override bool Equals(object o) { return o is Rect && Equals((Rect)o); }
        public override int GetHashCode() { return X.GetHashCode() ^ Y.GetHashCode() ^ Width.GetHashCode() ^ Height.GetHashCode(); }
        public static bool operator ==(Rect a, Rect b) { return a.Equals(b); }
        public static bool operator !=(Rect a, Rect b) { return !a.Equals(b); }
        public override string ToString() { return IsEmpty ? ""Empty"" : X + "","" + Y + "","" + Width + "","" + Height; }
    }
" },
        { "Windows.UI.Color", @"    public struct Color
    {
        private byte _A, _R, _G, _B;
        public byte A { get { return _A; } set { _A = value; } }
        public byte R { get { return _R; } set { _R = value; } }
        public byte G { get { return _G; } set { _G = value; } }
        public byte B { get { return _B; } set { _B = value; } }
        public static Color FromArgb(byte a, byte r, byte g, byte b) { var value = new Color(); value._A = a; value._R = r; value._G = g; value._B = b; return value; }
        public bool Equals(Color value) { return A == value.A && R == value.R && G == value.G && B == value.B; }
        public override bool Equals(object o) { return o is Color && Equals((Color)o); }
        public override int GetHashCode() { return A << 24 | R << 16 | G << 8 | B; }
        public static bool operator ==(Color a, Color b) { return a.Equals(b); }
        public static bool operator !=(Color a, Color b) { return !a.Equals(b); }
        public override string ToString() { return ""#"" + A.ToString(""X2"") + R.ToString(""X2"") + G.ToString(""X2"") + B.ToString(""X2""); }
    }
" },
        { "Windows.UI.Xaml.Thickness", @"    public struct Thickness
    {
        private double _Left, _Top, _Right, _Bottom;
        public Thickness(double uniformLength) { _Left = _Top = _Right = _Bottom = uniformLength; }
        public Thickness(double left, double top, double right, double bottom) { _Left = left; _Top = top; _Right = right; _Bottom = bottom; }
        public double Left { get { return _Left; } set { _Left = value; } }
        public double Top { get { return _Top; } set { _Top = value; } }
        public double Right { get { return _Right; } set { _Right = value; } }
        public double Bottom { get { return _Bottom; } set { _Bottom = value; } }
        public bool Equals(Thickness value) { return Left == value.Left && Top == value.Top && Right == value.Right && Bottom == value.Bottom; }
        public override bool Equals(object o) { return o is Thickness && Equals((Thickness)o); }
        public override int GetHashCode() { return Left.GetHashCode() ^ Top.GetHashCode() ^ Right.GetHashCode() ^ Bottom.GetHashCode(); }
        public static bool operator ==(Thickness a, Thickness b) { return a.Equals(b); }
        public static bool operator !=(Thickness a, Thickness b) { return !a.Equals(b); }
        public override string ToString() { return Left + "","" + Top + "","" + Right + "","" + Bottom; }
    }
" },
        { "Windows.UI.Xaml.Duration", @"    public struct Duration
    {
        private global::System.TimeSpan _timeSpan;
        private DurationType _durationType;
        public Duration(global::System.TimeSpan timeSpan) { _timeSpan = timeSpan; _durationType = DurationType.TimeSpan; }
        public static implicit operator Duration(global::System.TimeSpan timeSpan) { return new Duration(timeSpan); }
        public static Duration Automatic { get { var value = new Duration(); value._durationType = DurationType.Automatic; return value; } }
        public static Duration Forever { get { var value = new Duration(); value._durationType = DurationType.Forever; return value; } }
        public bool HasTimeSpan { get { return _durationType == DurationType.TimeSpan; } }
        public global::System.TimeSpan TimeSpan
        {
            get { if (!HasTimeSpan) throw new global::System.InvalidOperationException(""Duration has no TimeSpan""); return _timeSpan; }
        }
        public bool Equals(Duration value) { return _durationType == value._durationType && (!HasTimeSpan || _timeSpan == value._timeSpan); }
        public override bool Equals(object o) { return o is Duration && Equals((Duration)o); }
        public override int GetHashCode() { return HasTimeSpan ? _timeSpan.GetHashCode() : (int)_durationType; }
        public static bool operator ==(Duration a, Duration b) { return a.Equals(b); }
        public static bool operator !=(Duration a, Duration b) { return !a.Equals(b); }
        public override string ToString() { return HasTimeSpan ? _timeSpan.ToString() : _durationType.ToString(); }
    }
" } };

    // Managed-implemented interfaces a composed object exposes on its controlling outer.
    private static readonly string[][] ManagedContractList = {
        new[] { "Windows.UI.Xaml.Markup.IComponentConnector", "Windows_UI_Xaml_Markup_IComponentConnector.Interface" },
        new[] { "Windows.UI.Xaml.Markup.IXamlMetadataProvider", "Windows_UI_Xaml_Markup_IXamlMetadataProvider.Provider" } };
    private static bool HasManagedContracts { get { return ManagedContractList.Any(c => selected.Contains(c[0])); } }
    private static void ManagedContracts(StringBuilder output)
    {
        if (!HasManagedContracts) return;
        output.AppendLine("namespace AppxPortable.WinRT.Projected");
        output.AppendLine("{");
        output.AppendLine("    internal static class ManagedContracts");
        output.AppendLine("    {");
        output.AppendLine("        internal static " + Runtime + "ManagedObject Compose(object target, params " + Runtime + "NativeInterface[] interfaces)");
        output.AppendLine("        {");
        output.AppendLine("            var all = new global::System.Collections.Generic.List<" + Runtime + "NativeInterface>(interfaces);");
        foreach (string[] contract in ManagedContractList.Where(c => selected.Contains(c[0])))
            output.AppendLine("            if (target is global::" + contract[0] + ") all.Add(" + contract[1] + ");");
        output.AppendLine("            return " + Runtime + "ManagedObject.For(target, all.ToArray());");
        output.AppendLine("        }");
        output.AppendLine("    }");
        output.AppendLine("}");
    }

    // Each method's WinMD name and signature, in vtable order.
    private static void MethodShapes(TypeDefinition type, params string[] shapes)
    {
        var actual = type.Methods.Select(m => m.Name + "(" + string.Join(",", m.Parameters.Select(p => CollectionType(p.ParameterType))) + ")->" +
            CollectionType(m.ReturnType)).ToArray();
        if (!type.IsInterface || type.HasGenericParameters || type.Methods.Any(m => m.IsStatic || !m.HasThis) || !actual.SequenceEqual(shapes))
            throw new NotSupportedException("Unsupported XAML metadata ABI: " + type.FullName);
    }

    // IXamlMetadataProvider with the IXamlType and IXamlMember it returns, implemented by
    // managed apps (XamlTypeInfo) and called by native XAML. Native callers receive CCWs.
    private static void XamlMetadataContract(StringBuilder output, TypeDefinition type, XmlElement surface)
    {
        const string ns = "Windows.UI.Xaml.Markup.";
        if (type.FullName != ns + "IXamlMetadataProvider" || surface.Name != "Interface" || surface.HasChildNodes)
            throw new NotSupportedException("Unsupported XAML metadata surface");
        TypeDefinition xamlType = Find(ns + "IXamlType"), member = Find(ns + "IXamlMember"), definition = Find(ns + "XmlnsDefinition");
        MethodShapes(type, "GetXamlType(Windows.UI.Xaml.Interop.TypeName)->" + ns + "IXamlType",
            "GetXamlType(System.String)->" + ns + "IXamlType", "GetXmlnsDefinitions()->" + ns + "XmlnsDefinition[]");
        MethodShapes(xamlType, "get_BaseType()->" + ns + "IXamlType", "get_ContentProperty()->" + ns + "IXamlMember",
            "get_FullName()->System.String", "get_IsArray()->System.Boolean", "get_IsCollection()->System.Boolean",
            "get_IsConstructible()->System.Boolean", "get_IsDictionary()->System.Boolean", "get_IsMarkupExtension()->System.Boolean",
            "get_IsBindable()->System.Boolean", "get_ItemType()->" + ns + "IXamlType", "get_KeyType()->" + ns + "IXamlType",
            "get_UnderlyingType()->Windows.UI.Xaml.Interop.TypeName", "ActivateInstance()->System.Object",
            "CreateFromString(System.String)->System.Object", "GetMember(System.String)->" + ns + "IXamlMember",
            "AddToVector(System.Object,System.Object)->System.Void", "AddToMap(System.Object,System.Object,System.Object)->System.Void",
            "RunInitializer()->System.Void");
        MethodShapes(member, "get_IsAttachable()->System.Boolean", "get_IsDependencyProperty()->System.Boolean",
            "get_IsReadOnly()->System.Boolean", "get_Name()->System.String", "get_TargetType()->" + ns + "IXamlType",
            "get_Type()->" + ns + "IXamlType", "GetValue(System.Object)->System.Object", "SetValue(System.Object,System.Object)->System.Void");
        if (!definition.IsValueType || !definition.Fields.Where(f => !f.IsStatic).Select(f => f.Name + ":" + f.FieldType.FullName)
                .SequenceEqual(new[] { "XmlNamespace:System.String", "Namespace:System.String" }))
            throw new NotSupportedException("Unsupported XAML metadata ABI: " + definition.FullName);
        string body = XamlMetadataTemplate
            .Replace("$StdCall", StdCall).Replace("$Runtime.", Runtime)
            .Replace("$Provider", Guid(type)).Replace("$Type", Guid(xamlType)).Replace("$Member", Guid(member));
        output.Append(body);
    }
    private const string XamlMetadataTemplate = @"    public interface IXamlMetadataProvider
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
namespace AppxPortable.WinRT.Projected
{
    // CCW thunks in SDK vtable order. Object arguments and results that need
    // boxing or unwrapping are not implemented yet and fail with E_NOTIMPL.
    internal static class Windows_UI_Xaml_Markup_IXamlMetadataProvider
    {
        $StdCall
        private delegate int TypeNameCall(global::System.IntPtr self, $Runtime.NativeTypeName.Abi type, out global::System.IntPtr result);
        $StdCall
        private delegate int PointerCall(global::System.IntPtr self, global::System.IntPtr value, out global::System.IntPtr result);
        $StdCall
        private delegate int ArrayCall(global::System.IntPtr self, out uint length, out global::System.IntPtr value);
        $StdCall
        private delegate int GetCall(global::System.IntPtr self, out global::System.IntPtr value);
        $StdCall
        private delegate int BooleanCall(global::System.IntPtr self, out byte value);
        $StdCall
        private delegate int GetTypeNameCall(global::System.IntPtr self, out $Runtime.NativeTypeName.Abi value);
        $StdCall
        private delegate int ActionCall(global::System.IntPtr self);
        $StdCall
        private delegate int PairCall(global::System.IntPtr self, global::System.IntPtr first, global::System.IntPtr second);
        $StdCall
        private delegate int TripleCall(global::System.IntPtr self, global::System.IntPtr first, global::System.IntPtr second, global::System.IntPtr third);

        private static global::Windows.UI.Xaml.Markup.IXamlMetadataProvider AsProvider(object target) { return (global::Windows.UI.Xaml.Markup.IXamlMetadataProvider)target; }
        private static global::Windows.UI.Xaml.Markup.IXamlType AsType(object target) { return (global::Windows.UI.Xaml.Markup.IXamlType)target; }
        private static global::Windows.UI.Xaml.Markup.IXamlMember AsMember(object target) { return (global::Windows.UI.Xaml.Markup.IXamlMember)target; }

        // An owned native reference for a returned managed object; null stays null.
        private static global::System.IntPtr Owned(object value, $Runtime.NativeInterface contract)
        {
            if (value == null) return global::System.IntPtr.Zero;
            if (value is $Runtime.INativeWrapper) throw new global::System.NotSupportedException(""Native XAML metadata objects cannot be returned to native code yet"");
            $Runtime.ManagedObject managed = $Runtime.ManagedObject.For(value, contract);
            global::System.Guid iid = contract.Iid;
            global::System.IntPtr pointer;
            int hr = global::System.Runtime.InteropServices.Marshal.QueryInterface(managed.Pointer, ref iid, out pointer);
            global::System.GC.KeepAlive(managed);
            global::System.Runtime.InteropServices.Marshal.ThrowExceptionForHR(hr);
            return pointer;
        }
        private static global::System.IntPtr OwnedObject(object value)
        {
            using ($Runtime.NativeObject boxed = $Runtime.NativeObject.Box(value))
            {
                if (boxed == null) return global::System.IntPtr.Zero;
                global::System.IntPtr pointer = boxed.DangerousGetHandle();
                global::System.Runtime.InteropServices.Marshal.AddRef(pointer);
                return pointer;
            }
        }
        private static int Result(global::System.IntPtr self, global::System.Func<object, global::System.IntPtr> call, out global::System.IntPtr result)
        {
            global::System.IntPtr value = global::System.IntPtr.Zero;
            int hr = $Runtime.ManagedObject.Call(self, target => value = call(target));
            result = hr < 0 ? global::System.IntPtr.Zero : value;
            if (hr < 0 && value != global::System.IntPtr.Zero) global::System.Runtime.InteropServices.Marshal.Release(value);
            return hr;
        }
        private static int Flag(global::System.IntPtr self, global::System.Func<object, bool> call, out byte result)
        {
            bool value = false;
            int hr = $Runtime.ManagedObject.Call(self, target => value = call(target));
            result = value ? (byte)1 : (byte)0;
            return hr;
        }
        private static int Text(global::System.IntPtr self, global::System.Func<object, string> call, out global::System.IntPtr result)
        {
            return Result(self, target => { using (var text = new $Runtime.NativeString(call(target))) return text.Detach(); }, out result);
        }
        private static int Unimplemented(global::System.IntPtr self, string member)
        {
            return $Runtime.ManagedObject.Call(self, target => { throw $Runtime.Errors.Unimplemented(member); });
        }

        private static int GetXamlType(global::System.IntPtr self, $Runtime.NativeTypeName.Abi type, out global::System.IntPtr result)
        {
            return Result(self, target => Owned(AsProvider(target).GetXamlType($Runtime.NativeTypeName.ToType(type)), Type), out result);
        }
        private static int GetXamlTypeByFullName(global::System.IntPtr self, global::System.IntPtr name, out global::System.IntPtr result)
        {
            return Result(self, target => Owned(AsProvider(target).GetXamlType($Runtime.NativeString.Read(name)), Type), out result);
        }
        private static int GetXmlnsDefinitions(global::System.IntPtr self, out uint length, out global::System.IntPtr value)
        {
            length = 0;
            value = global::System.IntPtr.Zero;
            return Unimplemented(self, ""Windows.UI.Xaml.Markup.IXamlMetadataProvider.GetXmlnsDefinitions"");
        }
        private static int GetUnderlyingType(global::System.IntPtr self, out $Runtime.NativeTypeName.Abi result)
        {
            var value = default($Runtime.NativeTypeName.Abi);
            int hr = $Runtime.ManagedObject.Call(self, target => value = $Runtime.NativeTypeName.Detach(AsType(target).UnderlyingType));
            result = value;
            return hr;
        }
        private static int GetMember(global::System.IntPtr self, global::System.IntPtr name, out global::System.IntPtr result)
        {
            return Result(self, target => Owned(AsType(target).GetMember($Runtime.NativeString.Read(name)), Member), out result);
        }
        private static int CreateFromString(global::System.IntPtr self, global::System.IntPtr text, out global::System.IntPtr result)
        {
            result = global::System.IntPtr.Zero;
            return Unimplemented(self, ""Windows.UI.Xaml.Markup.IXamlType.CreateFromString"");
        }
        private static int GetValue(global::System.IntPtr self, global::System.IntPtr instance, out global::System.IntPtr result)
        {
            result = global::System.IntPtr.Zero;
            return Unimplemented(self, ""Windows.UI.Xaml.Markup.IXamlMember.GetValue"");
        }

        // Windows.UI.Xaml.Markup.IXamlMetadataProvider
        internal static readonly $Runtime.NativeInterface Provider = new $Runtime.NativeInterface($Provider, true,
            new TypeNameCall(GetXamlType), new PointerCall(GetXamlTypeByFullName), new ArrayCall(GetXmlnsDefinitions));
        // Windows.UI.Xaml.Markup.IXamlType
        internal static readonly $Runtime.NativeInterface Type = new $Runtime.NativeInterface($Type, true,
            new GetCall((global::System.IntPtr self, out global::System.IntPtr r) => Result(self, t => Owned(AsType(t).BaseType, Type), out r)),
            new GetCall((global::System.IntPtr self, out global::System.IntPtr r) => Result(self, t => Owned(AsType(t).ContentProperty, Member), out r)),
            new GetCall((global::System.IntPtr self, out global::System.IntPtr r) => Text(self, t => AsType(t).FullName, out r)),
            new BooleanCall((global::System.IntPtr self, out byte r) => Flag(self, t => AsType(t).IsArray, out r)),
            new BooleanCall((global::System.IntPtr self, out byte r) => Flag(self, t => AsType(t).IsCollection, out r)),
            new BooleanCall((global::System.IntPtr self, out byte r) => Flag(self, t => AsType(t).IsConstructible, out r)),
            new BooleanCall((global::System.IntPtr self, out byte r) => Flag(self, t => AsType(t).IsDictionary, out r)),
            new BooleanCall((global::System.IntPtr self, out byte r) => Flag(self, t => AsType(t).IsMarkupExtension, out r)),
            new BooleanCall((global::System.IntPtr self, out byte r) => Flag(self, t => AsType(t).IsBindable, out r)),
            new GetCall((global::System.IntPtr self, out global::System.IntPtr r) => Result(self, t => Owned(AsType(t).ItemType, Type), out r)),
            new GetCall((global::System.IntPtr self, out global::System.IntPtr r) => Result(self, t => Owned(AsType(t).KeyType, Type), out r)),
            new GetTypeNameCall(GetUnderlyingType),
            new GetCall((global::System.IntPtr self, out global::System.IntPtr r) => Result(self, t => OwnedObject(AsType(t).ActivateInstance()), out r)),
            new PointerCall(CreateFromString),
            new PointerCall(GetMember),
            new PairCall((self, instance, value) => Unimplemented(self, ""Windows.UI.Xaml.Markup.IXamlType.AddToVector"")),
            new TripleCall((self, instance, key, value) => Unimplemented(self, ""Windows.UI.Xaml.Markup.IXamlType.AddToMap"")),
            new ActionCall(self => $Runtime.ManagedObject.Call(self, t => AsType(t).RunInitializer())));
        // Windows.UI.Xaml.Markup.IXamlMember
        internal static readonly $Runtime.NativeInterface Member = new $Runtime.NativeInterface($Member, true,
            new BooleanCall((global::System.IntPtr self, out byte r) => Flag(self, t => AsMember(t).IsAttachable, out r)),
            new BooleanCall((global::System.IntPtr self, out byte r) => Flag(self, t => AsMember(t).IsDependencyProperty, out r)),
            new BooleanCall((global::System.IntPtr self, out byte r) => Flag(self, t => AsMember(t).IsReadOnly, out r)),
            new GetCall((global::System.IntPtr self, out global::System.IntPtr r) => Text(self, t => AsMember(t).Name, out r)),
            new GetCall((global::System.IntPtr self, out global::System.IntPtr r) => Result(self, t => Owned(AsMember(t).TargetType, Type), out r)),
            new GetCall((global::System.IntPtr self, out global::System.IntPtr r) => Result(self, t => Owned(AsMember(t).Type, Type), out r)),
            new PointerCall(GetValue),
            new PairCall((self, instance, value) => Unimplemented(self, ""Windows.UI.Xaml.Markup.IXamlMember.SetValue"")));
    }
";

    private static void Property(StringBuilder output, TypeDefinition type, string name, bool composable)
    {
        PropertyDefinition definition = type.Properties.SingleOrDefault(p => p.Name == name);
        if (definition == null || definition.GetMethod == null || definition.Parameters.Count != 0)
            throw new NotSupportedException("Unsupported property: " + type.FullName + "." + name);
        MethodDefinition getter = definition.GetMethod, setter = definition.SetMethod;
        bool boolean = getter.ReturnType.FullName == "System.Boolean";
        bool text = getter.ReturnType.FullName == "System.String";
        bool inspectable = getter.ReturnType.FullName == "System.Object";
        bool interval = getter.ReturnType.FullName == "Windows.Foundation.TimeSpan";
        bool enumValue = Available(getter.ReturnType.FullName) && Find(getter.ReturnType.FullName).IsEnum;
        if (enumValue)
        {
            string width = Find(getter.ReturnType.FullName).Fields.Single(f => f.Name == "value__").FieldType.FullName;
            if (width != "System.Int32" && width != "System.UInt32")
                throw new NotSupportedException("Unsupported enum property ABI: " + getter.ReturnType.FullName);
        }
        TypeDefinition owner = MemberInterface(type, getter);
        int slot = Slot(owner, getter);
        output.AppendLine("        // " + owner.FullName + "::" + getter.Name + ", slot " + Literal(slot));
        if (setter != null)
        {
            if (setter.Parameters.Count != 1 || setter.ReturnType.FullName != "System.Void" || setter.IsStatic != getter.IsStatic)
                throw new NotSupportedException("Unsupported property setter signature: " + type.FullName + "." + name);
            ValidateExternalIdentity(setter.Parameters[0].ParameterType);
            if (boolean || text || inspectable || interval || enumValue)
            {
                if (setter.Parameters[0].ParameterType.FullName != getter.ReturnType.FullName)
                    throw new NotSupportedException("Primitive property setter type differs: " + type.FullName + "." + name);
            }
            else
            {
                Parameter(setter.Parameters[0].ParameterType, type.FullName + "." + setter.Name);
                if (IsDelegate(Find(setter.Parameters[0].ParameterType.FullName)))
                    throw new NotSupportedException("Unsupported property setter: " + type.FullName + "." + name);
            }
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
            if (boolean) output.AppendLine("                    " + receiver + ".SetBoolean(" + Literal(Slot(writer, setter)) + ", value);");
            else if (interval) output.AppendLine("                    " + receiver + ".SetInt64(" + Literal(Slot(writer, setter)) + ", value.Ticks);");
            else if (enumValue) output.AppendLine("                    " + receiver + ".SetUInt32(" + Literal(Slot(writer, setter)) + ", unchecked((uint)value));");
            else if (text)
            {
                output.AppendLine("                using (var argument = new " + Runtime + "NativeString(value))");
                output.AppendLine("                    " + receiver + ".Call(" + Literal(Slot(writer, setter)) + ", argument.Value);");
            }
            else
            {
                // Object values box primitives and pass managed objects by controlling identity, as method arguments do.
                string prepare = inspectable ? Runtime + "NativeObject.Box(value)" : Argument(setter.Parameters[0].ParameterType, "value");
                output.AppendLine("                using (var argument = " + prepare + ")");
                output.AppendLine("                    " + receiver + ".Call(" + Literal(Slot(writer, setter)) + ", argument);");
            }
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
            if (member.Name != "Property" && lenient)
            {
                Console.Error.WriteLine("Omitted " + iface.FullName + "." + member.GetAttribute("Name") + ": interface " + member.Name.ToLowerInvariant());
                continue;
            }
            if (member.Name != "Property") throw new NotSupportedException("Unsupported member kind: " + iface.FullName + "." + member.Name);
            if (members.Contains(member.GetAttribute("Name")))
                throw new InvalidDataException("Duplicate property: " + iface.FullName + "." + member.GetAttribute("Name"));
            members.Add(member.GetAttribute("Name"));
        }
        if (members.Count == 0 && iface.Methods.Count != 0 && !lenient) throw new InvalidDataException("Empty runtime surface: " + iface.FullName);
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
    // IIterable<IKeyValuePair<K, V>>, its iterator, and the pair, as maps and map views share them.
    private static void IterationContract()
    {
        foreach (var contract in new[] {
            new { Type = Find("Windows.Foundation.Collections.IIterable`1"), Names = new[] { "First" } },
            new { Type = Find("Windows.Foundation.Collections.IIterator`1"), Names = new[] { "get_Current", "get_HasCurrent", "MoveNext", "GetMany" } },
            new { Type = Find("Windows.Foundation.Collections.IKeyValuePair`2"), Names = new[] { "get_Key", "get_Value" } } })
            if (!contract.Type.Methods.Select(m => m.Name).SequenceEqual(contract.Names))
                throw new NotSupportedException("Unsupported collection ABI: " + contract.Type.FullName + " method order");
        CollectionMethod(Find("Windows.Foundation.Collections.IIterable`1"), 1, "First", "()->Windows.Foundation.Collections.IIterator`1<!0>");
        CollectionMethod(Find("Windows.Foundation.Collections.IIterator`1"), 1, "get_Current", "()->!0");
        CollectionMethod(Find("Windows.Foundation.Collections.IIterator`1"), 1, "get_HasCurrent", "()->System.Boolean");
        CollectionMethod(Find("Windows.Foundation.Collections.IIterator`1"), 1, "MoveNext", "()->System.Boolean");
        CollectionMethod(Find("Windows.Foundation.Collections.IKeyValuePair`2"), 2, "get_Key", "()->!0");
        CollectionMethod(Find("Windows.Foundation.Collections.IKeyValuePair`2"), 2, "get_Value", "()->!1");
    }
    private const string MapViewName = "Windows.Foundation.Collections.IMapView`2";
    private static void ValidateMapView()
    {
        if (!selected.Contains(MapViewName) || interfaces[MapViewName].GetAttribute("Adapter") != "StringMapView")
            throw new NotSupportedException("Map view adapter is not selected: " + MapViewName);
        var view = Find(MapViewName);
        if (!view.Methods.Select(m => m.Name).SequenceEqual(new[] { "Lookup", "get_Size", "HasKey", "Split" }))
            throw new NotSupportedException("Unsupported collection ABI: " + view.FullName + " method order");
        CollectionMethod(view, 2, "Lookup", "(!0)->!1");
        CollectionMethod(view, 2, "get_Size", "()->System.UInt32");
        CollectionMethod(view, 2, "HasKey", "(!0)->System.Boolean");
        IterationContract();
    }
    // IMapView<String, V> projects as IReadOnlyDictionary<string, V> over a selected class or interface V.
    private static TypeReference MapViewValue(GenericInstanceType view)
    {
        ValidateMapView();
        TypeReference value = view.GenericArguments[1];
        if (view.GenericArguments[0].FullName != "System.String" || IsDelegate(Parameter(value, view.FullName)))
            throw new NotSupportedException("Unsupported map view: " + view.FullName);
        return value;
    }
    private static string StringMapViewAbi(GenericInstanceType view)
    {
        TypeReference value = MapViewValue(view);
        var iterable = Find("Windows.Foundation.Collections.IIterable`1");
        var iterator = Find("Windows.Foundation.Collections.IIterator`1");
        var pair = Find("Windows.Foundation.Collections.IKeyValuePair`2");
        var map = Find(MapViewName);
        string pairSignature = "pinterface({" + Iid(pair) + "};string;" + EventSignature(value) + ")";
        return "new " + Runtime + "StringMapViewAbi {\n" +
            "                    View = new global::System.Guid(\"" + Parameterized(map, "string", EventSignature(value)) + "\"),\n" +
            "                    Iterable = new global::System.Guid(\"" + Parameterized(iterable, pairSignature) + "\"),\n" +
            "                    Lookup = " + Literal(NamedSlot(map, "Lookup")) + ", Size = " + Literal(NamedSlot(map, "get_Size")) +
            ", HasKey = " + Literal(NamedSlot(map, "HasKey")) + ", First = " + Literal(NamedSlot(iterable, "First")) + ",\n" +
            "                    Current = " + Literal(NamedSlot(iterator, "get_Current")) + ", HasCurrent = " + Literal(NamedSlot(iterator, "get_HasCurrent")) +
            ", MoveNext = " + Literal(NamedSlot(iterator, "MoveNext")) + ", Key = " + Literal(NamedSlot(pair, "get_Key")) +
            ", Value = " + Literal(NamedSlot(pair, "get_Value")) + " }";
    }
    private static void ValidateMap()
    {
        var map = Find("Windows.Foundation.Collections.IMap`2");
        if (!map.Methods.Select(m => m.Name).SequenceEqual(new[] { "Lookup", "get_Size", "HasKey", "GetView", "Insert", "Remove", "Clear" }))
            throw new NotSupportedException("Unsupported collection ABI: " + map.FullName + " method order");
        IterationContract();
        CollectionMethod(map, 2, "Lookup", "(!0)->!1");
        CollectionMethod(map, 2, "get_Size", "()->System.UInt32");
        CollectionMethod(map, 2, "HasKey", "(!0)->System.Boolean");
        CollectionMethod(map, 2, "Insert", "(!0,!1)->System.Boolean");
        CollectionMethod(map, 2, "Remove", "(!0)->System.Void");
        CollectionMethod(map, 2, "Clear", "()->System.Void");
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
        ValidateMap();
        string pairSignature = "pinterface({" + Iid(pair) + "};string;cinterface(IInspectable))";
        string wrapper = type.FullName.Replace('.', '_');
        output.AppendLine("    public interface " + type.Name + " : global::System.Collections.Generic.IDictionary<string, object> { }");
        output.AppendLine("}");
        output.AppendLine("namespace AppxPortable.WinRT.Projected");
        output.AppendLine("{");
        output.AppendLine("    internal sealed class " + wrapper + " : " + Runtime + "NativeStringObjectMap, global::" + type.FullName);
        output.AppendLine("    {");
        output.AppendLine("        private static readonly " + Runtime + "MapAbi abi = new " + Runtime + "MapAbi {");
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

    private static IEnumerable<string> SelectedOverrides(TypeDefinition type)
    {
        XmlElement surface;
        return classSurfaces.TryGetValue(type.FullName, out surface)
            ? surface.ChildNodes.OfType<XmlElement>().Where(m => m.Name == "Overridable").Select(m => m.GetAttribute("Name"))
            : Enumerable.Empty<string>();
    }

    // Component mode: a member the generator cannot express becomes a stub with its
    // exact signature that names itself and throws, as the completed Windows.dll's
    // do, so one unsupported member does not cost the game the whole component.
    private static bool lenient;

    private static void Member(StringBuilder output, TypeDefinition type, XmlElement member, Action<StringBuilder> emit, MethodDefinition constructor = null)
    {
        var buffer = new StringBuilder();
        try { emit(buffer); output.Append(buffer); return; }
        catch (Exception error)
        {
            if (!lenient || !(error is NotSupportedException || error is InvalidDataException)) throw;
            string name = constructor != null ? ".ctor" : member.GetAttribute("Name");
            Console.Error.WriteLine("Stubbed " + type.FullName + "." + name + ": " + error.Message);
            var stubs = constructor != null ? new[] { constructor }
                : member.Name == "Property" ? type.Properties.Where(p => p.Name == name).SelectMany(p => new[] { p.GetMethod, p.SetMethod }).Where(m => m != null).ToArray()
                : member.Name == "Event" ? type.Events.Where(e => e.Name == name).SelectMany(e => new[] { e.AddMethod, e.RemoveMethod }).ToArray()
                : type.Methods.Where(m => m.Name == name && !m.IsConstructor).ToArray();
            foreach (MethodDefinition method in stubs) Stub(output, type, method);
        }
    }

    private static void Stub(StringBuilder output, TypeDefinition type, MethodDefinition method)
    {
        string signature;
        try
        {
            string parameters = string.Join(", ", method.Parameters.Select(p => (p.ParameterType.IsByReference
                ? (p.IsOut ? "out " : "ref ") + StubName(((ByReferenceType)p.ParameterType).ElementType) : StubName(p.ParameterType)) + " @" + p.Name));
            signature = method.IsConstructor ? type.Name + "(" + parameters + ")"
                : (method.IsStatic ? "static " : "") + (method.ReturnType.FullName == "System.Void" ? "void" : StubName(method.ReturnType)) + " " + method.Name + "(" + parameters + ")";
        }
        catch (Exception error)
        {
            if (!(error is NotSupportedException || error is InvalidDataException)) throw;
            Console.Error.WriteLine("Omitted " + type.FullName + "." + method.Name + ": " + error.Message);
            return;
        }
        output.AppendLine("        public " + signature + " { throw " + Runtime + "Errors.Unimplemented(\"" + type.FullName + "." + method.Name + "\"); }");
    }

    private const string ActivatableName = "Windows.Foundation.Metadata.ActivatableAttribute";
    private const string DateTimeName = "Windows.Foundation.DateTime";

    // A constructor with arguments calls the matching method of an [Activatable(typeof(IFactory))] interface.
    private static void FactoryConstructor(StringBuilder output, TypeDefinition type, TypeDefinition primary, MethodDefinition constructor)
    {
        var signature = constructor.Parameters.Select(p => p.ParameterType.FullName).ToList();
        var matches = type.CustomAttributes
            .Where(a => a.AttributeType.FullName == ActivatableName && a.ConstructorArguments.Count >= 2 && a.ConstructorArguments[0].Value is TypeReference)
            .Select(a => Find(((TypeReference)a.ConstructorArguments[0].Value).FullName))
            .SelectMany(f => f.Methods.Where(m => m.ReturnType.FullName == type.FullName &&
                m.Parameters.Select(p => p.ParameterType.FullName).SequenceEqual(signature)).Select(m => new { Factory = f, Method = m }))
            .ToList();
        if (matches.Count != 1) throw new NotSupportedException("No activation factory method for: " + constructor.FullName);
        TypeDefinition factory = matches[0].Factory;
        MethodDefinition create = matches[0].Method;
        int slot = Slot(factory, create);
        var abi = new List<string>();
        var setup = new List<string>();
        var arguments = new List<string>();
        MarshalArguments(type, create, abi, setup, arguments);
        string native = "Native_" + factory.Name + "_" + Literal(slot);
        output.AppendLine("        " + StdCall);
        output.AppendLine("        private delegate int " + native + "(global::System.IntPtr self" + string.Concat(abi.Select(p => ", " + p)) + ", out global::System.IntPtr result);");
        output.AppendLine("        // " + factory.FullName + "::" + create.Name + ", slot " + Literal(slot));
        output.AppendLine("        public " + type.Name + "(" + string.Join(", ", create.Parameters.Select(p => Name(p.ParameterType) + " @" + p.Name)) + ")");
        output.AppendLine("        {");
        output.AppendLine("            using (var invocation = " + Runtime + "Errors.Begin())");
        output.AppendLine("            {");
        output.AppendLine("                using (var activation = " + Runtime + "NativeObject.Factory(\"" + type.FullName + "\", " + Guid(factory) + "))");
        foreach (string line in setup) output.AppendLine(line);
        output.AppendLine("                using (var call = new " + Runtime + "NativeObject.Access(activation))");
        output.AppendLine("                {");
        output.AppendLine("                    global::System.IntPtr result;");
        output.AppendLine("                    int hr = call.Method<" + native + ">(" + Literal(slot) + ")(call.Self" + string.Concat(arguments.Select(p => ", " + p)) + ", out result);");
        output.AppendLine("                    using (var created = " + Runtime + "NativeObject.TakeResult(hr, result))");
        output.AppendLine("                        native = created.QueryInterface(" + Guid(primary) + ");");
        output.AppendLine("                }");
        output.AppendLine("            }");
        output.AppendLine("            " + Runtime + "NativeObject.Remember(this, native);");
        output.AppendLine("        }");
    }

    private const string ClosableName = "Windows.Foundation.IClosable";
    private static bool Closable(TypeDefinition type)
    {
        if (!type.Interfaces.Any(i => i.InterfaceType.FullName == ClosableName)) return false;
        TypeDefinition closable = Find(ClosableName);
        if (!closable.Methods.Select(m => m.Name).SequenceEqual(new[] { "Close" }) || closable.Methods[0].Parameters.Count != 0 ||
            closable.Methods[0].ReturnType.FullName != "System.Void")
            throw new NotSupportedException("Unsupported closable ABI: " + ClosableName);
        return true;
    }
    // The CLR projects IClosable.Close as IDisposable.Dispose.
    private static void Dispose(StringBuilder output)
    {
        TypeDefinition closable = Find(ClosableName);
        output.AppendLine("        // " + ClosableName + "::Close, slot " + Literal(Slot(closable, closable.Methods[0])));
        output.AppendLine("        public void Dispose()");
        output.AppendLine("        {");
        output.AppendLine("            using (var invocation = " + Runtime + "Errors.Begin())");
        output.AppendLine("            using (var self = native.QueryInterface(" + Guid(closable) + "))");
        output.AppendLine("                self.Call(" + Literal(Slot(closable, closable.Methods[0])) + ");");
        output.AppendLine("        }");
    }
    private static string ObjectMapAbi()
    {
        var map = Find("Windows.Foundation.Collections.IMap`2");
        var iterable = Find("Windows.Foundation.Collections.IIterable`1");
        var iterator = Find("Windows.Foundation.Collections.IIterator`1");
        var pair = Find("Windows.Foundation.Collections.IKeyValuePair`2");
        string inspectable = "cinterface(IInspectable)";
        string pairSignature = "pinterface({" + Iid(pair) + "};" + inspectable + ";" + inspectable + ")";
        return "new " + Runtime + "MapAbi {\n" +
            "            Map = new global::System.Guid(\"" + Parameterized(map, inspectable, inspectable) + "\"),\n" +
            "            Iterable = new global::System.Guid(\"" + Parameterized(iterable, pairSignature) + "\"),\n" +
            "            Lookup = " + Literal(NamedSlot(map, "Lookup")) + ", Insert = " + Literal(NamedSlot(map, "Insert")) +
            ", Remove = " + Literal(NamedSlot(map, "Remove")) + ", Clear = " + Literal(NamedSlot(map, "Clear")) + ",\n" +
            "            Size = " + Literal(NamedSlot(map, "get_Size")) + ", HasKey = " + Literal(NamedSlot(map, "HasKey")) + ",\n" +
            "            First = " + Literal(NamedSlot(iterable, "First")) + ", Current = " + Literal(NamedSlot(iterator, "get_Current")) + ",\n" +
            "            HasCurrent = " + Literal(NamedSlot(iterator, "get_HasCurrent")) + ", MoveNext = " + Literal(NamedSlot(iterator, "MoveNext")) + ",\n" +
            "            Key = " + Literal(NamedSlot(pair, "get_Key")) + ", Value = " + Literal(NamedSlot(pair, "get_Value")) + " };";
    }
    private static void ObjectMapMembers(StringBuilder output)
    {
        output.AppendLine("        private static readonly " + Runtime + "MapAbi dictionaryAbi = " + ObjectMapAbi());
        output.AppendLine("        private " + Runtime + "NativeObjectMap dictionaryMap;");
        output.AppendLine("        private " + Runtime + "NativeObjectMap CollectionMap { get { return dictionaryMap ?? (dictionaryMap = new " + Runtime + "NativeObjectMap(native, dictionaryAbi, v => " + Inspectable("v") + ")); } }");
        output.AppendLine("        public object this[object key] { get { return CollectionMap[key]; } set { CollectionMap[key] = value; } }");
        output.AppendLine("        public int Count { get { return CollectionMap.Count; } }");
        output.AppendLine("        public bool IsReadOnly { get { return false; } }");
        output.AppendLine("        public global::System.Collections.Generic.ICollection<object> Keys { get { return CollectionMap.Keys; } }");
        output.AppendLine("        public global::System.Collections.Generic.ICollection<object> Values { get { return CollectionMap.Values; } }");
        output.AppendLine("        public bool ContainsKey(object key) { return CollectionMap.ContainsKey(key); }");
        output.AppendLine("        public bool TryGetValue(object key, out object value) { return CollectionMap.TryGetValue(key, out value); }");
        output.AppendLine("        public void Add(object key, object value) { CollectionMap.Add(key, value); }");
        output.AppendLine("        public bool Remove(object key) { return CollectionMap.Remove(key); }");
        output.AppendLine("        public void Clear() { CollectionMap.Clear(); }");
        output.AppendLine("        public void Add(global::System.Collections.Generic.KeyValuePair<object, object> item) { CollectionMap.Add(item); }");
        output.AppendLine("        public bool Contains(global::System.Collections.Generic.KeyValuePair<object, object> item) { return CollectionMap.Contains(item); }");
        output.AppendLine("        public bool Remove(global::System.Collections.Generic.KeyValuePair<object, object> item) { return CollectionMap.Remove(item); }");
        output.AppendLine("        public void CopyTo(global::System.Collections.Generic.KeyValuePair<object, object>[] array, int index) { CollectionMap.CopyTo(array, index); }");
        output.AppendLine("        public global::System.Collections.Generic.IEnumerator<global::System.Collections.Generic.KeyValuePair<object, object>> GetEnumerator() { return CollectionMap.GetEnumerator(); }");
        output.AppendLine("        global::System.Collections.IEnumerator global::System.Collections.IEnumerable.GetEnumerator() { return GetEnumerator(); }");
    }
    private static void Class(StringBuilder output, TypeDefinition type, XmlElement surface)
    {
        bool objectMap = surface.GetAttribute("Adapter") == "ObjectMap";
        if (surface.HasAttribute("Adapter") && !objectMap)
            throw new NotSupportedException("Unsupported class adapter: " + type.FullName);
        if (objectMap)
        {
            if (!type.Interfaces.Any(i => i.InterfaceType.FullName == "Windows.Foundation.Collections.IMap`2<System.Object,System.Object>"))
                throw new NotSupportedException("Unsupported object map adapter: " + type.FullName);
            ValidateMap();
        }
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
                if (member.Name == "Property" && type.Properties.Any(p => p.Name == name && p.GetMethod.IsStatic)) Member(output, type, member, o => Property(o, type, name, false));
                else if (member.Name == "Method" && type.Methods.Any(m => m.Name == name && m.IsStatic)) Member(output, type, member, o => Method(o, type, name, false));
                else if (member.Name == "Event" && type.Events.Any(e => e.Name == name && e.AddMethod != null && e.AddMethod.IsStatic)) Member(output, type, member, o => Event(o, type, name, false));
                else throw new NotSupportedException("Unsupported static member: " + type.FullName + "." + name);
            }
            output.AppendLine("    }");
            return;
        }
        TypeDefinition primary = DefaultInterface(type);
        bool direct = type.Methods.Any(m => m.IsConstructor && !m.IsStatic && m.Parameters.Count == 0) &&
            type.CustomAttributes.Any(a => a.AttributeType.FullName == ActivatableName && a.ConstructorArguments[0].Type.FullName == "System.UInt32");
        var factored = type.Methods.Where(m => m.IsConstructor && !m.IsStatic && m.IsPublic && m.Parameters.Count != 0).ToList();
        if (constructor && !composable && (baseClass != null || !direct && factored.Count == 0))
            throw new NotSupportedException("No activation constructor: " + type.FullName);
        if (overridden.Count != 0 && !constructor && !composable) throw new InvalidDataException("Overridable without Constructor: " + type.FullName);

        // Overridable interfaces with a selected member are answered by the managed outer;
        // the others, across the class chain, keep the native behavior, and a managed
        // override of one fails construction.
        List<TypeDefinition> overridable = Overridables(type);
        foreach (string name in overridden)
            if (overridable.Count(i => i.Methods.Any(m => m.Name == name)) != 1)
                throw new NotSupportedException("Unsupported overridable: " + type.FullName + "." + name);
        var inheritedSelections = Chain(type).SelectMany(SelectedOverrides).Distinct().ToList();
        var inheritedInterfaces = Chain(type).SelectMany(Overridables).Distinct().ToList();
        var exposed = inheritedInterfaces.Where(i => i.Methods.Any(m => inheritedSelections.Contains(m.Name))).ToList();
        var unsupported = inheritedInterfaces.SelectMany(i => i.Methods).Select(m => m.Name)
            .Where(n => !inheritedSelections.Contains(n)).Distinct().ToList();
        // Unsealed classes may be composed or wrap a more derived object: query per call.
        bool query = !type.IsSealed;
        bool root = baseClass == null;
        string hide = root ? "" : "new ";

        var implemented = type.Interfaces.Select(i => i.InterfaceType.FullName).Where(interfaces.ContainsKey).ToList();
        bool closable = members.Contains("Close") && Closable(type);
        var bases = new List<string>();
        if (!root) bases.Add("global::" + baseClass.FullName);
        bases.AddRange(implemented.Select(i => "global::" + i));
        if (closable) bases.Add("global::System.IDisposable");
        if (objectMap) bases.Add("global::System.Collections.Generic.IDictionary<object, object>");
        if (root) bases.Add(Runtime + "INativeWrapper");
        if (interop)
        {
            providers.Add(type);
            output.AppendLine("    [" + Interop + "ProjectionType(\"" + Iid(primary) + "\")]");
            if (root) bases.Add(Interop + "INativeObject");
        }
        output.AppendLine("    public " + (type.IsSealed ? "sealed " : "") + "class " + type.Name +
            (bases.Count == 0 ? "" : " : " + string.Join(", ", bases)));
        output.AppendLine("    {");
        if (objectMap) ObjectMapMembers(output);
        if (root)
        {
            // The non-delegating inner of a composed object, or a native object's interface.
            output.AppendLine("        internal " + Runtime + "NativeObject native;");
            if (composable) output.AppendLine("        internal " + Runtime + "ManagedObject outer;");
            output.AppendLine("        " + Runtime + "NativeObject " + Runtime + "INativeWrapper.Native { get { return native; } }");
            if (interop) output.AppendLine("        global::System.Runtime.InteropServices.SafeHandle " + Interop + "INativeObject.AcquireInterface(global::System.Guid iid) { return " + Runtime + "NativeObject.Argument(this, iid); }");
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
            foreach (MethodDefinition factoryConstructor in factored)
                Member(output, type, null, o => FactoryConstructor(o, type, primary, factoryConstructor), factoryConstructor);
        if (constructor && !composable && direct)
        {
            output.AppendLine("        // IActivationFactory::ActivateInstance, slot 6; default interface from WinMD.");
            output.AppendLine("        public " + type.Name + "()");
            output.AppendLine("        {");
            output.AppendLine("            using (var factory = " + Runtime + "NativeObject.Factory(\"" + type.FullName + "\", new global::System.Guid(\"00000035-0000-0000-c000-000000000046\")))");
            output.AppendLine("            using (var value = factory.Object(6))");
            output.AppendLine("                native = value.QueryInterface(" + Guid(primary) + ");");
            output.AppendLine("            " + Runtime + "NativeObject.Remember(this, native);");
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
            string compose = HasManagedContracts ? "global::AppxPortable.WinRT.Projected.ManagedContracts.Compose" : Runtime + "ManagedObject.For";
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
                string body = inheritedSelections.Contains(method.Name)
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
            if (member.Name == "Method" && closable && member.GetAttribute("Name") == "Close") Dispose(output);
            else if (member.Name == "Property") Member(output, type, member, o => Property(o, type, member.GetAttribute("Name"), query));
            else if (member.Name == "Method") Member(output, type, member, o => Method(o, type, member.GetAttribute("Name"), query, member.GetAttribute("Arities")));
            else if (member.Name == "Event") Member(output, type, member, o => Event(o, type, member.GetAttribute("Name"), query));
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
            if (args.Length < 3) throw new ArgumentException("generator METADATA_DIRECTORY SURFACE_XML OUTPUT_CS | generator --component INPUT_WINMD OUTPUT_CS");
            bool component = args[0] == "--component";
            lenient = component;
            string metadata = component ? Path.GetDirectoryName(Path.GetFullPath(args[1])) : args[0];
            var resolver = new DefaultAssemblyResolver();
            resolver.AddSearchDirectory(metadata);
            foreach (string path in component ? new[] { args[1] } : Directory.GetFiles(metadata, "*.winmd").OrderBy(p => p, StringComparer.Ordinal).ToArray())
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
            var sourceModule = component ? modules.Single() : null;
            for (int index = 3; index < args.Length; ++index)
            {
                if (args[index] == "--interop") interop = true;
                else if (args[index] == "--reference" && index + 1 < args.Length) { interop = true; ReferenceProjection(args[++index]); }
                else throw new ArgumentException("Unsupported generator option: " + args[index]);
            }
            if (component && (sourceModule.Assembly.Name.HasPublicKey || sourceModule.Assembly.Name.PublicKeyToken.Length != 0 ||
                !string.IsNullOrEmpty(sourceModule.Assembly.Name.Culture)))
                throw new NotSupportedException("Unsupported component assembly identity: " + sourceModule.Assembly.Name.FullName);
            if (component && !string.Equals(Path.GetFileNameWithoutExtension(args[1]), sourceModule.Assembly.Name.Name, StringComparison.Ordinal))
                throw new InvalidDataException("Component metadata filename does not match assembly identity: " + sourceModule.Assembly.Name.Name);
            var document = component ? ComponentSurface(sourceModule) : new XmlDocument { XmlResolver = null };
            if (!component) using (XmlReader reader = XmlReader.Create(args[1], new XmlReaderSettings
            {
                DtdProcessing = DtdProcessing.Prohibit,
                XmlResolver = null
            })) document.Load(reader);
            if (document.DocumentElement == null || document.DocumentElement.Name != "Projection")
                throw new InvalidDataException("Expected Projection surface root");
            var elements = document.DocumentElement.ChildNodes.OfType<XmlElement>().ToList();
            foreach (XmlElement element in elements)
            {
                if (element.Name != "Class" && element.Name != "Enum" && element.Name != "Delegate" && element.Name != "Interface" && element.Name != "Struct")
                    throw new NotSupportedException("Unsupported surface kind: " + element.Name);
                if (!selected.Add(element.GetAttribute("Name"))) throw new InvalidDataException("Duplicate selected type");
                if (element.Name == "Interface") interfaces.Add(element.GetAttribute("Name"), element);
                if (element.Name == "Class") {
                    classes.Add(Find(element.GetAttribute("Name")));
                    classSurfaces.Add(element.GetAttribute("Name"), element);
                }
            }
            var output = new StringBuilder();
            output.AppendLine(component ? "// Generated from package component metadata. Do not edit." : "// Generated from SDK metadata. Do not edit.");
            output.AppendLine((component ? "// Component SHA256 " : "// Surface SHA256 ") + Hash(args[1]));
            foreach (ModuleDefinition module in selected.Select(n => Find(n).Module).Distinct().OrderBy(m => m.Name, StringComparer.Ordinal))
                output.AppendLine("// " + module.Name + " SHA256 " + Hash(module.FileName));
            output.AppendLine("[assembly: global::System.Reflection.AssemblyVersion(\"" + (component ? sourceModule.Assembly.Name.Version.ToString() : "255.255.255.255") + "\")]");
            if (interop) output.AppendLine("[assembly: " + Interop + "ProjectionProvider(typeof(global::AppxPortable.WinRT.Projected.CrossAssemblyProvider))]");
            foreach (var reference in external.Values.Select(t => t.Module).Distinct()) output.AppendLine("// Reference " + reference.Name + " SHA256 " + Hash(reference.FileName));
            foreach (XmlElement element in elements.OrderBy(e => e.GetAttribute("Name"), StringComparer.Ordinal))
            {
                TypeDefinition type = Find(element.GetAttribute("Name"));
                output.AppendLine("namespace " + type.Namespace);
                output.AppendLine("{");
                if (element.GetAttribute("Adapter") == "Uri")
                    UriContract(output, type, element);
                else if (element.GetAttribute("Adapter") == "ComponentConnector")
                    ComponentConnectorContract(output, type, element);
                else if (element.GetAttribute("Adapter") == "XamlMetadata")
                    XamlMetadataContract(output, type, element);
                else if (element.GetAttribute("Adapter") == "Clr")
                {
                    if (component) throw new NotSupportedException("CLR-redirected structs belong to Windows.dll: " + type.FullName);
                    ClrStruct(output, type, element);
                }
                else if (element.GetAttribute("Adapter") == "Storage")
                    StorageContract(output, type, element);
                else if (element.GetAttribute("Adapter") == "StringMapView")
                {
                    // IReadOnlyDictionary<string, V> is the projection; each use emits its closed ABI.
                    if (element.Name != "Interface" || element.HasChildNodes || type.FullName != MapViewName)
                        throw new NotSupportedException("Unsupported map view surface: " + type.FullName);
                    ValidateMapView();
                }
                else if (element.GetAttribute("Adapter") == "EventHandler")
                {
                    // System.EventHandler<T> is the projection; only its ABI shape is checked.
                    if (element.Name != "Delegate" || element.HasChildNodes) throw new NotSupportedException("Unsupported event handler surface");
                    ValidateEventHandler(type);
                }
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
            ManagedContracts(output);
            if (interop) Provider(output);
            File.WriteAllText(args[2], output.ToString(), new UTF8Encoding(false));
            Console.WriteLine("Generated " + selected.Count + " metadata types into " + args[2]);
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error.Message); return 1; }
        finally { foreach (ModuleDefinition module in modules) module.Dispose(); }
    }
}
