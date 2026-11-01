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
}
