// Automatic package-component surface selection. No per-application member lists.
using System.Linq;

internal static partial class WinrtProjectionGenerator
{
    private static string Scalar(Mono.Cecil.TypeReference type)
    {
        switch (type.FullName)
        {
            case "System.Boolean": return "byte"; // WinRT boolean is one byte.
            case "System.Byte": return "byte";
            case "System.SByte": return "sbyte";
            case "System.Int16": return "short";
            case "System.UInt16": return "ushort";
            case "System.Int32": return "int";
            case "System.UInt32": return "uint";
            case "System.Int64": return "long";
            case "System.UInt64": return "ulong";
            case "System.Single": return "float";
            case "System.Double": return "double";
            case "System.Char": return "ushort";
            default: return null;
        }
    }

    private static System.Xml.XmlDocument ComponentSurface(Mono.Cecil.ModuleDefinition module)
    {
        var document = new System.Xml.XmlDocument { XmlResolver = null };
        var root = document.CreateElement("Projection"); document.AppendChild(root);
        foreach (var type in module.Types.Where(t => t.IsPublic).OrderBy(t => t.FullName, System.StringComparer.Ordinal))
        {
            if (!type.IsWindowsRuntime || type.HasGenericParameters || type.HasNestedTypes)
                throw new System.NotSupportedException("Unsupported component type: " + type.FullName);
            string kind = type.IsEnum ? "Enum" : IsDelegate(type) ? "Delegate" : type.IsInterface ? "Interface" : "Class";
            if (type.IsValueType && !type.IsEnum)
                throw new System.NotSupportedException("Unsupported component struct: " + type.FullName);
            var element = document.CreateElement(kind); element.SetAttribute("Name", type.FullName); root.AppendChild(element);
            if (kind != "Class" && kind != "Interface") continue;
            System.Action<string, string> add = (member, name) => {
                var item = document.CreateElement(member);
                if (name != null) item.SetAttribute("Name", name);
                element.AppendChild(item);
            };
            foreach (var constructor in type.Methods.Where(m => m.IsConstructor && m.IsPublic))
            {
                if (constructor.HasParameters) throw new System.NotSupportedException("Unsupported component constructor: " + constructor);
                add("Constructor", null);
            }
            foreach (var property in type.Properties) add("Property", property.Name);
            foreach (var handler in type.Events) add("Event", handler.Name);
            foreach (string name in type.Methods.Where(m => m.IsPublic && !m.IsConstructor && !m.IsGetter && !m.IsSetter && !m.IsAddOn && !m.IsRemoveOn)
                .Select(m => m.Name).Distinct()) add("Method", name);
        }
        if (!root.HasChildNodes) throw new System.IO.InvalidDataException("No public WinRT component types: " + module.Name);
        return document;
    }

    private const string Interop = "global::AppxPortable.WinRT.Interop.";
    private const string TypeDescriptor = "AppxPortable.WinRT.Interop.ProjectionTypeAttribute";
    private static bool interop;
    private static readonly System.Collections.Generic.Dictionary<string, Mono.Cecil.TypeDefinition> external =
        new System.Collections.Generic.Dictionary<string, Mono.Cecil.TypeDefinition>(System.StringComparer.Ordinal);
    private static readonly System.Collections.Generic.List<Mono.Cecil.TypeDefinition> providers = new System.Collections.Generic.List<Mono.Cecil.TypeDefinition>();

    private static bool Available(string name) { return selected.Contains(name) || external.ContainsKey(name); }
    private static void ValidateExternalIdentity(Mono.Cecil.TypeReference type)
    {
        if (!external.ContainsKey(type.FullName)) return;
        var actual = external[type.FullName].Module.Assembly.Name;
        var requested = type.Scope as Mono.Cecil.AssemblyNameReference;
        if (type is Mono.Cecil.TypeDefinition && ((Mono.Cecil.TypeDefinition)type).Module.Assembly.Name.FullName == actual.FullName) return;
        if (requested == null || !System.String.Equals(requested.Name, actual.Name, System.StringComparison.OrdinalIgnoreCase) ||
            requested.Version != actual.Version ||
            !System.String.Equals(requested.Culture ?? "", actual.Culture ?? "", System.StringComparison.OrdinalIgnoreCase) ||
            !requested.PublicKeyToken.SequenceEqual(actual.PublicKeyToken))
            throw new System.NotSupportedException("External projection identity mismatch for " + type.FullName + ": " + type.Scope + " versus " + actual.FullName);
    }
    private static void ReferenceProjection(string path)
    {
        var module = Mono.Cecil.ModuleDefinition.ReadModule(path, new Mono.Cecil.ReaderParameters { ApplyWindowsRuntimeProjections = false, InMemory = true });
        modules.Add(module);
        if (!System.String.Equals(System.IO.Path.GetFileNameWithoutExtension(path), module.Assembly.Name.Name, System.StringComparison.OrdinalIgnoreCase))
            throw new System.IO.InvalidDataException("Projection reference filename does not match assembly identity: " + module.Assembly.Name.Name);
        var bridge = module.AssemblyReferences.SingleOrDefault(a => a.Name == "AppxPortable.WinRT.Interop");
        if (bridge == null || bridge.Version != new System.Version(1,0,0,0) || bridge.PublicKeyToken.Length != 0 || !System.String.IsNullOrEmpty(bridge.Culture) ||
            Attribute(module.Assembly, "AppxPortable.WinRT.Interop.ProjectionProviderAttribute") == null)
            throw new System.NotSupportedException("Reference is not an interop projection: " + path);
        foreach (var type in module.Types.Where(t => t.IsPublic))
        {
            var descriptor = Attribute(type, TypeDescriptor);
            if (descriptor == null) continue;
            string iid = (string)descriptor.ConstructorArguments.Single().Value;
            System.Guid parsed;
            if (type.HasGenericParameters || (type.IsEnum ? iid != "" : type.IsInterface || type.IsValueType || !System.Guid.TryParse(iid, out parsed)))
                throw new System.NotSupportedException("Invalid external projection type: " + type.FullName);
            if (types.ContainsKey(type.FullName)) throw new System.IO.InvalidDataException("Conflicting external projection type: " + type.FullName);
            external.Add(type.FullName, type); types.Add(type.FullName, type);
        }
    }
    private static string Wrapped(Mono.Cecil.TypeReference type, string owned)
    {
        return external.ContainsKey(type.FullName) ? Interop + "ProjectionInterop.Wrap<" + Name(type) + ">(" + owned + ")"
            : Wrapper(type) + ".FromNative(" + owned + ")";
    }
    private static void Provider(System.Text.StringBuilder output)
    {
        output.AppendLine("namespace AppxPortable.WinRT.Projected {");
        output.AppendLine("    internal sealed class CrossAssemblyProvider : " + Interop + "IProjectionProvider {");
        output.AppendLine("        public CrossAssemblyProvider() { }");
        output.AppendLine("        public object Wrap(global::System.Type type, global::System.Runtime.InteropServices.SafeHandle borrowed) {");
        output.AppendLine("            using (var value = " + Runtime + "NativeObject.FromLease(borrowed)) {");
        foreach (var type in providers)
            output.AppendLine("                if (type == typeof(global::" + type.FullName + ")) return global::" + type.FullName + ".FromNative(value);");
        output.AppendLine("                throw new global::System.NotSupportedException(\"Unsupported external wrapper: \" + type.FullName);");
        output.AppendLine("            }");
        output.AppendLine("        }");
        output.AppendLine("    }");
        output.AppendLine("}");
    }
}
