// Build-time completion of a compiled projection. No SDK metadata or Cecil assembly ships with games.
// Every Windows type a consumer references is defined with its WinMD shape, and every
// referenced member gets the consumer's exact signature. Members the projection does
// not implement throw NotImplementedException naming themselves, after naming
// themselves in the shim log, where a swallowed exception still leaves a trace.
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
    // The same contracts read in their CLR-projected form, as consumers see them.
    private static readonly Dictionary<string, TypeDefinition> projected =
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
    private static MethodReference unimplemented;
    private static int completed;

    private static IMetadataScope Scope(TypeReference type)
    {
        while (type.IsNested) type = type.DeclaringType;
        return type.Scope;
    }

    private static bool IsProjection(TypeReference type) { return Scope(type).Name == projection.Assembly.Name.Name; }

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
        // Structs and delegates holding a CLR-redirected type take that member in its projected form.
        TypeDefinition clr;
        if (!projected.TryGetValue(name, out clr)) clr = null;
        if (source.IsEnum || source.IsValueType)
        {
            foreach (FieldDefinition field in source.Fields)
            {
                FieldDefinition shaped = clr == null ? null : clr.Fields.SingleOrDefault(f => f.Name == field.Name);
                var copy = new FieldDefinition(field.Name, field.Attributes, shaped != null && Redirected(field.FieldType)
                    ? MapContract(shaped.FieldType, type, null) : Shape(field.FieldType, type, name + "." + field.Name));
                if (field.HasConstant) copy.Constant = field.Constant;
                type.Fields.Add(copy);
            }
        }
        else if (source.BaseType != null && source.BaseType.FullName == "System.MulticastDelegate")
        {
            foreach (MethodDefinition method in source.Methods)
            {
                MethodDefinition shaped = clr == null ? null : clr.Methods.SingleOrDefault(m => m.Name == method.Name && m.Parameters.Count == method.Parameters.Count);
                var copy = new MethodDefinition(method.Name, method.Attributes, shaped != null && Redirected(method.ReturnType)
                    ? MapContract(shaped.ReturnType, type, null) : Shape(method.ReturnType, type, name));
                foreach (ParameterDefinition parameter in method.Parameters)
                    copy.Parameters.Add(new ParameterDefinition(parameter.Name, parameter.Attributes,
                        shaped != null && Redirected(parameter.ParameterType) ? MapContract(shaped.Parameters[parameter.Index].ParameterType, type, null)
                        : Shape(parameter.ParameterType, type, name + "." + method.Name)));
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
        if (type.IsNested || !IsProjection(type)) return projection.ImportReference(type);
        return Ensure(type.FullName);
    }

    private static string Signature(MethodReference method, string name = null)
    {
        return method.HasThis + " " + method.ReturnType.FullName + " " + (name ?? method.Name) + "`" + method.GenericParameters.Count +
            "(" + string.Join(",", method.Parameters.Select(p => p.ParameterType.FullName)) + ")";
    }

    // Maps a CLR-projected contract signature type into the projection: contract
    // types become projection types, CLR projections stay references.
    private static TypeReference MapContract(TypeReference type, TypeDefinition owner, MethodDefinition method)
    {
        var parameter = type as GenericParameter;
        if (parameter != null)
            return parameter.Type == GenericParameterType.Type ? owner.GenericParameters[parameter.Position]
                : method.GenericParameters[parameter.Position];
        if (type.IsArray) return new ArrayType(MapContract(((ArrayType)type).ElementType, owner, method), ((ArrayType)type).Rank);
        if (type.IsByReference) return new ByReferenceType(MapContract(((ByReferenceType)type).ElementType, owner, method));
        var instance = type as GenericInstanceType;
        if (instance != null)
        {
            var result = new GenericInstanceType(MapContract(instance.ElementType, owner, method));
            foreach (TypeReference argument in instance.GenericArguments) result.GenericArguments.Add(MapContract(argument, owner, method));
            return result;
        }
        if (!type.IsNested && metadata.ContainsKey(type.FullName) && !redirected.Contains(type.FullName)) return Ensure(type.FullName);
        // CLR projections name contract facades (System.Runtime, System.ObjectModel);
        // bind the ones the runtime defines to its own assemblies.
        // Look in mscorlib and System.dll directly: partial assembly names bind on Mono, not .NET Framework.
        Type runtime = type.IsNested ? null : typeof(object).Assembly.GetType(type.FullName) ?? typeof(Uri).Assembly.GetType(type.FullName);
        return runtime != null ? projection.ImportReference(runtime) : projection.ImportReference(type);
    }

    private static bool Redirected(TypeReference type)
    {
        if (type.IsArray || type.IsByReference) return Redirected(((TypeSpecification)type).ElementType);
        var instance = type as GenericInstanceType;
        if (instance != null) return Redirected(instance.ElementType) || instance.GenericArguments.Any(Redirected);
        return redirected.Contains(type.FullName);
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
        Add(owner, method, reference);
    }

    // A contract member in its CLR-projected form, as a consumer compiled against the SDK references it.
    private static void CompleteContract(TypeDefinition owner, MethodDefinition source)
    {
        var method = new MethodDefinition(source.Name, MethodAttributes.Public | MethodAttributes.HideBySig, projection.TypeSystem.Void);
        foreach (GenericParameter parameter in source.GenericParameters)
            method.GenericParameters.Add(new GenericParameter(parameter.Name, method));
        method.HasThis = source.HasThis;
        method.ExplicitThis = source.ExplicitThis;
        method.CallingConvention = source.CallingConvention;
        method.ReturnType = MapContract(source.ReturnType, owner, method);
        foreach (ParameterDefinition parameter in source.Parameters)
            method.Parameters.Add(new ParameterDefinition(parameter.Name, ParameterAttributes.None, MapContract(parameter.ParameterType, owner, method)));
        Add(owner, method, method);
    }

    private static bool verbose;

    private static void Add(TypeDefinition owner, MethodDefinition method, MethodReference reference)
    {
        string signature = Signature(method);
        if (owner.Methods.Any(m => Signature(m) == signature)) return;
        if (verbose) Console.WriteLine("completing " + owner.FullName + " " + signature);
        if (owner.BaseType != null && owner.BaseType.FullName == "System.MulticastDelegate")
            throw new NotSupportedException("Delegate member not in metadata: " + owner.FullName + "::" + reference.Name);
        // WinRT consumers call put_X; a C# property emits set_X. Forward only
        // when WinMD identifies this accessor and its exact projected signature
        // matches an implemented property. Ordinary methods are never renamed.
        TypeDefinition contract;
        if (metadata.TryGetValue(owner.FullName, out contract))
        {
            PropertyDefinition declared = contract.Properties.SingleOrDefault(p => p.SetMethod != null && p.SetMethod.Name == reference.Name);
            PropertyDefinition implemented = declared == null ? null : owner.Properties.SingleOrDefault(p => p.Name == declared.Name && p.SetMethod != null &&
                Signature(p.SetMethod, reference.Name) == signature);
            if (implemented != null)
            {
                ILProcessor forward = method.Body.GetILProcessor();
                if (method.HasThis) forward.Emit(OpCodes.Ldarg_0);
                foreach (ParameterDefinition parameter in method.Parameters) forward.Emit(OpCodes.Ldarg, parameter);
                forward.Emit(OpCodes.Call, implemented.SetMethod);
                forward.Emit(OpCodes.Ret);
                if (!reference.HasThis) method.IsStatic = true;
                owner.Methods.Add(method);
                ++completed;
                return;
            }
        }
        if (!reference.HasThis) method.IsStatic = true;
        if (method.Name == ".ctor") method.Attributes |= MethodAttributes.SpecialName | MethodAttributes.RTSpecialName;
        if (owner.IsInterface)
        {
            method.Attributes |= MethodAttributes.Abstract | MethodAttributes.Virtual | MethodAttributes.NewSlot;
            owner.Methods.Add(method);
            ++completed;
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
        ++completed;
    }

    // Every public type and member of the contracts, read in their CLR-projected
    // form. A shape the projection cannot express is reported, not fatal.
    private static void CompleteSurface(TextWriter report)
    {
        // Interfaces the projection already defines keep their shape: managed types
        // implement them, and an added abstract member would leave those unloadable.
        var implemented = new HashSet<string>(projection.Types.Where(t => t.IsInterface).Select(t => t.FullName), StringComparer.Ordinal);
        foreach (TypeDefinition type in projected.Values.OrderBy(t => t.FullName, StringComparer.Ordinal).Where(t => t.IsPublic &&
            !redirected.Contains(t.FullName) && !t.CustomAttributes.Any(a => a.AttributeType.FullName == "Windows.Foundation.Metadata.ExclusiveToAttribute")))
        {
            TypeDefinition owner;
            try { owner = Ensure(type.FullName); }
            catch (Exception error)
            {
                if (!Unexpressible(error)) throw;
                report.WriteLine(type.FullName + ": " + error.Message);
                continue;
            }
            if (type.IsEnum || type.IsValueType || IsDelegate(type)) continue;
            if (implemented.Contains(type.FullName))
            {
                RenameSetters(owner, type);
                report.WriteLine(type.FullName + ": kept as the projection defines it");
                continue;
            }
            // Protected members too: apps derive from composable classes and call them.
            foreach (MethodDefinition method in type.Methods.Where(m => (m.IsPublic || m.IsFamily || m.IsFamilyOrAssembly) && !(m.IsConstructor && m.IsStatic)))
            {
                try { CompleteContract(owner, method); }
                catch (Exception error)
                {
                    if (!Unexpressible(error)) throw;
                    report.WriteLine(type.FullName + "::" + method.Name + ": " + error.Message);
                }
            }
        }
    }

    // WinRT names a property setter put_X; a C#-defined projection interface names it
    // set_X. Rename the interface setter, point every implementer at it explicitly,
    // and retarget references within the projection, so put_X callers bind.
    private static void RenameSetters(TypeDefinition iface, TypeDefinition contract)
    {
        foreach (PropertyDefinition property in iface.Properties.Where(p => p.SetMethod != null))
        {
            PropertyDefinition declared = contract.Properties.SingleOrDefault(p => p.Name == property.Name && p.SetMethod != null);
            if (declared == null || declared.SetMethod.Name == property.SetMethod.Name) continue;
            MethodDefinition setter = property.SetMethod;
            string old = setter.Name;
            setter.Name = declared.SetMethod.Name;
            foreach (TypeDefinition implementer in projection.Types.Where(t => !t.IsInterface))
            {
                foreach (InterfaceImplementation implementation in implementer.Interfaces.Where(i => i.InterfaceType.GetElementType().FullName == iface.FullName))
                {
                    MethodDefinition body = implementer.Methods.SingleOrDefault(m => m.Name == old && m.Parameters.Count == 1 && m.IsVirtual);
                    if (body == null || body.Overrides.Any(o => o.Name == setter.Name)) continue;
                    var reference = new MethodReference(setter.Name, setter.ReturnType, implementation.InterfaceType) { HasThis = true };
                    reference.Parameters.Add(new ParameterDefinition(setter.Parameters[0].ParameterType));
                    body.Overrides.Add(reference);
                }
            }
            foreach (MethodDefinition method in projection.Types.SelectMany(t => t.Methods).Where(m => m.HasBody))
                foreach (Instruction instruction in method.Body.Instructions)
                {
                    var target = instruction.Operand as MethodReference;
                    if (target != null && target.Name == old && !(target is MethodDefinition) &&
                        target.DeclaringType.GetElementType().FullName == iface.FullName) target.Name = setter.Name;
                }
            foreach (MethodDefinition method in projection.Types.SelectMany(t => t.Methods))
                foreach (MethodReference target in method.Overrides.Where(o => o.Name == old && !(o is MethodDefinition) &&
                    o.DeclaringType.GetElementType().FullName == iface.FullName))
                    target.Name = setter.Name;
        }
    }

    // An assembly compiled with C# against the uncompleted projection names WinRT
    // setters set_X; against the surface they are put_X. Its implementers override
    // put_X explicitly and its calls bind to it.
    private static void Rebind(string input, string output)
    {
        var setters = projection.Types.Where(t => t.IsInterface).SelectMany(t => t.Properties
            .Where(p => p.SetMethod != null && p.SetMethod.Name == "put_" + p.Name).Select(p => p.SetMethod)).ToList();
        ModuleDefinition module = ModuleDefinition.ReadModule(input, new ReaderParameters { InMemory = true });
        modules.Add(module);
        foreach (MethodDefinition setter in setters)
        {
            string old = "set_" + setter.Name.Substring(4), owner = setter.DeclaringType.FullName;
            foreach (TypeDefinition type in module.GetTypes().Where(t => !t.IsInterface))
                foreach (InterfaceImplementation implementation in type.Interfaces.Where(i => i.InterfaceType.GetElementType().FullName == owner && IsProjection(i.InterfaceType)))
                {
                    MethodDefinition body = type.Methods.SingleOrDefault(m => m.Name == old && m.Parameters.Count == 1 && m.IsVirtual);
                    if (body == null) continue;
                    MethodReference target = module.ImportReference(setter);
                    target.DeclaringType = implementation.InterfaceType;
                    body.Overrides.Add(target);
                }
            foreach (MethodDefinition method in module.GetTypes().SelectMany(t => t.Methods))
            {
                foreach (MethodReference target in method.Overrides.Where(o => o.Name == old && o.DeclaringType.GetElementType().FullName == owner))
                    target.Name = setter.Name;
                if (!method.HasBody) continue;
                foreach (Instruction instruction in method.Body.Instructions)
                {
                    var target = instruction.Operand as MethodReference;
                    if (target != null && target.Name == old && target.DeclaringType.GetElementType().FullName == owner && IsProjection(target.DeclaringType))
                        target.Name = setter.Name;
                }
            }
        }
        module.Write(output);
    }

    private static bool Unexpressible(Exception error) { return error is NotSupportedException || error is InvalidDataException; }

    private static bool IsDelegate(TypeDefinition type)
    {
        return type.BaseType != null && type.BaseType.FullName == "System.MulticastDelegate";
    }

    private static void Throw(MethodDefinition method, string name)
    {
        ILProcessor il = method.Body.GetILProcessor();
        il.Emit(OpCodes.Ldstr, name);
        il.Emit(OpCodes.Call, unimplemented);
        il.Emit(OpCodes.Throw);
    }

    private static int Main(string[] args)
    {
        try
        {
            if (args.Length == 4 && args[0] == "--rebind")
            {
                projection = ModuleDefinition.ReadModule(args[1], new ReaderParameters { InMemory = true });
                modules.Add(projection);
                Rebind(args[2], args[3]);
                Console.WriteLine("Rebound " + args[2] + " to " + args[1]);
                return 0;
            }
            bool surface = args.Length > 0 && args[0] == "--all";
            if (surface) args = args.Skip(1).ToArray();
            if (args.Length < (surface ? 3 : 4) || surface && args.Length > 4)
                throw new ArgumentException("complete METADATA_DIRECTORY PROJECTION_IN PROJECTION_OUT CONSUMER... | complete --all METADATA_DIRECTORY PROJECTION_IN PROJECTION_OUT [REPORT]");
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
                ModuleDefinition clr = ModuleDefinition.ReadModule(path, new ReaderParameters { ApplyWindowsRuntimeProjections = true, InMemory = true });
                modules.Add(clr);
                foreach (TypeDefinition type in clr.Types.Where(t => t.Name != "<Module>")) projected[type.FullName] = type;
            }
            projection = ModuleDefinition.ReadModule(args[1], new ReaderParameters { InMemory = true });
            modules.Add(projection);
            // Stubs report themselves through the projection runtime before throwing.
            unimplemented = projection.Types.Single(t => t.FullName == "AppxPortable.WinRT.Errors").Methods.Single(m => m.Name == "Unimplemented");
            int before = projection.Types.Count;
            if (surface)
            {
                if (args.Length > 3) using (var report = new StreamWriter(args[3])) CompleteSurface(report);
                else CompleteSurface(Console.Error);
            }
            // Consumer completion names each member it adds; the whole surface adds too many to list.
            verbose = !surface;
            if (!surface) foreach (string path in args.Skip(3))
            {
                ModuleDefinition consumer = ModuleDefinition.ReadModule(path, new ReaderParameters
                {
                    ApplyWindowsRuntimeProjections = false,
                    InMemory = true
                });
                modules.Add(consumer);
                foreach (TypeReference type in consumer.GetTypeReferences().Where(t => !t.IsNested && IsProjection(t)))
                    Ensure(type.FullName);
                foreach (MemberReference member in consumer.GetMemberReferences().Where(m => IsProjection(m.DeclaringType.GetElementType())))
                {
                    var method = member as MethodReference;
                    if (method != null) { Complete(method); continue; }
                    TypeDefinition owner = Ensure(member.DeclaringType.GetElementType().FullName);
                    if (!owner.Fields.Any(f => f.Name == member.Name))
                        throw new NotSupportedException("Field not in metadata: " + owner.FullName + "::" + member.Name);
                }
            }
            projection.Write(args[2]);
            Console.WriteLine("Completed " + (projection.Types.Count - before) + " types and " + completed + " members into " + args[2]);
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error.Message); return 1; }
        finally { foreach (ModuleDefinition module in modules) module.Dispose(); }
    }
}
