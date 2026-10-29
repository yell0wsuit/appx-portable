// Build-time completion of a compiled projection. No SDK metadata or Cecil assembly ships with games.
// Every Windows type a consumer references is defined with its WinMD shape, and every
// referenced member gets the consumer's exact signature. Members the projection does
// not implement throw NotImplementedException naming themselves.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

using Mono.Cecil;
using Mono.Cecil.Cil;

internal static class WinrtProjectionCompleter
{
    private static readonly Dictionary<string, TypeDefinition> metadata =
        new Dictionary<string, TypeDefinition>(StringComparer.Ordinal);
    private static readonly List<ModuleDefinition> modules = new List<ModuleDefinition>();
    // The CLR projects these to .NET types; a WinMD shape that uses one cannot be copied as-is.
    private static readonly HashSet<string> redirected = new HashSet<string>(StringComparer.Ordinal) {
        "Windows.Foundation.DateTime", "Windows.Foundation.EventHandler`1", "Windows.Foundation.EventRegistrationToken",
        "Windows.Foundation.HResult", "Windows.Foundation.IClosable", "Windows.Foundation.IReference`1",
        "Windows.Foundation.IReferenceArray`1", "Windows.Foundation.Point", "Windows.Foundation.Rect",
        "Windows.Foundation.Size", "Windows.Foundation.TimeSpan", "Windows.Foundation.Uri", "Windows.UI.Color",
        "Windows.UI.Xaml.Interop.TypeName", "Windows.UI.Xaml.Thickness", "Windows.UI.Xaml.CornerRadius",
        "Windows.UI.Xaml.Duration", "Windows.UI.Xaml.GridLength" };
    private static ModuleDefinition projection;
    private static MethodReference notImplemented;

    private static IMetadataScope Scope(TypeReference type)
    {
        while (type.IsNested) type = type.DeclaringType;
        return type.Scope;
    }

    private static bool IsWindows(TypeReference type) { return Scope(type).Name == "Windows"; }

    private static TypeReference Corlib(TypeReference type)
    {
        Type runtime = Type.GetType(type.FullName.Replace('/', '+'));
        if (runtime == null) throw new InvalidDataException("No corlib type: " + type.FullName);
        return projection.ImportReference(runtime);
    }

    // Copies a type from a WinMD shape (base types, fields, delegate signatures).
    private static TypeReference Shape(TypeReference type, IGenericParameterProvider owner, string use)
    {
        var parameter = type as GenericParameter;
        if (parameter != null)
        {
            if (parameter.Type != GenericParameterType.Type) throw new NotSupportedException("Generic method in WinMD shape: " + use);
            return owner.GenericParameters[parameter.Position];
        }
        if (type.IsArray) return new ArrayType(Shape(((ArrayType)type).ElementType, owner, use), ((ArrayType)type).Rank);
        if (type.IsByReference) return new ByReferenceType(Shape(((ByReferenceType)type).ElementType, owner, use));
        var instance = type as GenericInstanceType;
        if (instance != null)
        {
            var result = new GenericInstanceType(Shape(instance.ElementType, owner, use));
            foreach (TypeReference argument in instance.GenericArguments) result.GenericArguments.Add(Shape(argument, owner, use));
            return result;
        }
        if (type.Namespace == "System" || type.Namespace.StartsWith("System.", StringComparison.Ordinal)) return Corlib(type);
        if (redirected.Contains(type.FullName))
            throw new NotSupportedException("Projected type in WinMD shape: " + type.FullName + " used by " + use);
        return Ensure(type.FullName);
    }

    private static TypeDefinition Ensure(string name)
    {
        TypeDefinition existing = projection.Types.SingleOrDefault(t => t.FullName == name);
        if (existing != null) return existing;
        TypeDefinition source;
        if (!metadata.TryGetValue(name, out source)) throw new InvalidDataException("Metadata type not found: " + name);
        var type = new TypeDefinition(source.Namespace, source.Name,
            source.Attributes & ~(TypeAttributes.WindowsRuntime | TypeAttributes.Import));
        foreach (GenericParameter parameter in source.GenericParameters)
            type.GenericParameters.Add(new GenericParameter(parameter.Name, type));
        projection.Types.Add(type);
        if (source.BaseType != null) type.BaseType = Shape(source.BaseType, type, name);
        if (source.IsEnum || source.IsValueType)
        {
            foreach (FieldDefinition field in source.Fields)
            {
                var copy = new FieldDefinition(field.Name, field.Attributes, Shape(field.FieldType, type, name + "." + field.Name));
                if (field.HasConstant) copy.Constant = field.Constant;
                type.Fields.Add(copy);
            }
        }
        else if (source.BaseType != null && source.BaseType.FullName == "System.MulticastDelegate")
        {
            foreach (MethodDefinition method in source.Methods)
            {
                var copy = new MethodDefinition(method.Name, method.Attributes, Shape(method.ReturnType, type, name));
                foreach (ParameterDefinition parameter in method.Parameters)
                    copy.Parameters.Add(new ParameterDefinition(parameter.Name, parameter.Attributes,
                        Shape(parameter.ParameterType, type, name + "." + method.Name)));
                copy.ImplAttributes = MethodImplAttributes.Runtime | MethodImplAttributes.Managed;
                type.Methods.Add(copy);
            }
        }
        return type;
    }

    // Maps a consumer signature type into the projection, keeping non-Windows scopes as references.
    private static TypeReference Map(TypeReference type, TypeDefinition owner, MethodDefinition method)
    {
        var parameter = type as GenericParameter;
        if (parameter != null)
            return parameter.Type == GenericParameterType.Type ? owner.GenericParameters[parameter.Position]
                : method.GenericParameters[parameter.Position];
        if (type.IsArray) return new ArrayType(Map(((ArrayType)type).ElementType, owner, method), ((ArrayType)type).Rank);
        if (type.IsByReference) return new ByReferenceType(Map(((ByReferenceType)type).ElementType, owner, method));
        if (type.IsPointer) return new PointerType(Map(((PointerType)type).ElementType, owner, method));
        var required = type as RequiredModifierType;
        if (required != null) return new RequiredModifierType(Map(required.ModifierType, owner, method), Map(required.ElementType, owner, method));
        var optional = type as OptionalModifierType;
        if (optional != null) return new OptionalModifierType(Map(optional.ModifierType, owner, method), Map(optional.ElementType, owner, method));
        var instance = type as GenericInstanceType;
        if (instance != null)
        {
            var result = new GenericInstanceType(Map(instance.ElementType, owner, method));
            foreach (TypeReference argument in instance.GenericArguments) result.GenericArguments.Add(Map(argument, owner, method));
            return result;
        }
        if (type.IsNested || !IsWindows(type)) return projection.ImportReference(type);
        return Ensure(type.FullName);
    }

    private static string Signature(MethodReference method)
    {
        return method.HasThis + " " + method.ReturnType.FullName + " " + method.Name + "`" + method.GenericParameters.Count +
            "(" + string.Join(",", method.Parameters.Select(p => p.ParameterType.FullName)) + ")";
    }

    private static void Complete(MethodReference reference)
    {
        TypeDefinition owner = Ensure(reference.DeclaringType.GetElementType().FullName);
        var method = new MethodDefinition(reference.Name, MethodAttributes.Public | MethodAttributes.HideBySig,
            projection.TypeSystem.Void);
        foreach (GenericParameter parameter in reference.GenericParameters)
            method.GenericParameters.Add(new GenericParameter(parameter.Name, method));
        method.HasThis = reference.HasThis;
        method.ExplicitThis = reference.ExplicitThis;
        method.CallingConvention = reference.CallingConvention;
        method.ReturnType = Map(reference.ReturnType, owner, method);
        foreach (ParameterDefinition parameter in reference.Parameters)
            method.Parameters.Add(new ParameterDefinition(parameter.Name, parameter.Attributes, Map(parameter.ParameterType, owner, method)));
        string signature = Signature(method);
        if (owner.Methods.Any(m => Signature(m) == signature)) return;
        if (owner.BaseType != null && owner.BaseType.FullName == "System.MulticastDelegate")
            throw new NotSupportedException("Delegate member not in metadata: " + owner.FullName + "::" + reference.Name);
        if (!reference.HasThis) method.IsStatic = true;
        if (method.Name == ".ctor") method.Attributes |= MethodAttributes.SpecialName | MethodAttributes.RTSpecialName;
        if (owner.IsInterface)
        {
            method.Attributes |= MethodAttributes.Abstract | MethodAttributes.Virtual | MethodAttributes.NewSlot;
            owner.Methods.Add(method);
            // Projection types implementing the interface need a body for it, or they fail to load.
            foreach (TypeDefinition implementer in projection.Types.Where(t => t.Interfaces.Any(i => i.InterfaceType.FullName == owner.FullName)))
            {
                if (owner.HasGenericParameters) throw new NotSupportedException("Generic interface member on a projection type: " + owner.FullName);
                var implementation = new MethodDefinition(owner.FullName + "." + method.Name, MethodAttributes.Private | MethodAttributes.Final |
                    MethodAttributes.Virtual | MethodAttributes.HideBySig | MethodAttributes.NewSlot, method.ReturnType);
                foreach (ParameterDefinition parameter in method.Parameters)
                    implementation.Parameters.Add(new ParameterDefinition(parameter.Name, parameter.Attributes, parameter.ParameterType));
                implementation.Overrides.Add(method);
                Throw(implementation, owner.FullName + "." + reference.Name);
                implementer.Methods.Add(implementation);
            }
            return;
        }
        Throw(method, owner.FullName + "." + reference.Name);
        owner.Methods.Add(method);
    }

    private static void Throw(MethodDefinition method, string name)
    {
        ILProcessor il = method.Body.GetILProcessor();
        il.Emit(OpCodes.Ldstr, name);
        il.Emit(OpCodes.Newobj, notImplemented);
        il.Emit(OpCodes.Throw);
    }

    private static int Main(string[] args)
    {
        try
        {
            if (args.Length < 4) throw new ArgumentException("complete METADATA_DIRECTORY PROJECTION_IN PROJECTION_OUT CONSUMER...");
            foreach (string path in Directory.GetFiles(args[0], "*.winmd").OrderBy(p => p, StringComparer.Ordinal))
            {
                ModuleDefinition module = ModuleDefinition.ReadModule(path, new ReaderParameters
                {
                    ApplyWindowsRuntimeProjections = false,
                    InMemory = true
                });
                modules.Add(module);
                foreach (TypeDefinition type in module.Types.Where(t => t.Name != "<Module>"))
                {
                    if (metadata.ContainsKey(type.FullName)) throw new InvalidDataException("Duplicate metadata type: " + type.FullName);
                    metadata.Add(type.FullName, type);
                }
            }
            projection = ModuleDefinition.ReadModule(args[1], new ReaderParameters { InMemory = true });
            modules.Add(projection);
            if (projection.Assembly.Name.Name != "Windows") throw new InvalidDataException("Projection is not the Windows assembly");
            notImplemented = projection.ImportReference(typeof(NotImplementedException).GetConstructor(new[] { typeof(string) }));
            int before = projection.Types.Count;
            foreach (string path in args.Skip(3))
            {
                ModuleDefinition consumer = ModuleDefinition.ReadModule(path, new ReaderParameters
                {
                    ApplyWindowsRuntimeProjections = false,
                    InMemory = true
                });
                modules.Add(consumer);
                foreach (TypeReference type in consumer.GetTypeReferences().Where(t => !t.IsNested && IsWindows(t)))
                    Ensure(type.FullName);
                foreach (MemberReference member in consumer.GetMemberReferences().Where(m => IsWindows(m.DeclaringType.GetElementType())))
                {
                    var method = member as MethodReference;
                    if (method != null) { Complete(method); continue; }
                    TypeDefinition owner = Ensure(member.DeclaringType.GetElementType().FullName);
                    if (!owner.Fields.Any(f => f.Name == member.Name))
                        throw new NotSupportedException("Field not in metadata: " + owner.FullName + "::" + member.Name);
                }
            }
            projection.Write(args[2]);
            Console.WriteLine("Completed " + (projection.Types.Count - before) + " types into " + args[2]);
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error.Message); return 1; }
        finally { foreach (ModuleDefinition module in modules) module.Dispose(); }
    }
}
