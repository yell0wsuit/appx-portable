using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

using Mono.Cecil;
// Every Windows-scoped type and member a consumer references must resolve
// against the projection directory, with the signature the consumer uses.
class ResolveReferences
{
    static IMetadataScope Scope(TypeReference type)
    {
        while (type.IsNested) type = type.DeclaringType;
        return type.Scope;
    }
    static int Main(string[] args)
    {
        string selected = "Windows";
        if (args.Length > 2 && args[0] == "--assembly") { selected = args[1]; args = args.Skip(2).ToArray(); }
        var resolver = new DefaultAssemblyResolver();
        // Only the projection directory and consumer directories, never the working directory.
        foreach (string directory in resolver.GetSearchDirectories()) resolver.RemoveSearchDirectory(directory);
        resolver.AddSearchDirectory(args[0]);
        var unresolved = new SortedSet<string>(StringComparer.Ordinal);
        int types = 0, members = 0;
        foreach (string path in args.Skip(1))
        {
            resolver.AddSearchDirectory(Path.GetDirectoryName(Path.GetFullPath(path)));
            using (ModuleDefinition module = ModuleDefinition.ReadModule(path, new ReaderParameters
            {
                AssemblyResolver = resolver,
                ApplyWindowsRuntimeProjections = false
            }))
            {
                foreach (AssemblyNameReference reference in module.AssemblyReferences)
                    Console.WriteLine(module.Name + " references " + reference.FullName + " flags=" + reference.Attributes);
                foreach (TypeReference type in module.GetTypeReferences().Where(t => Scope(t).Name == selected))
                {
                    ++types;
                    if (type.Resolve() == null) unresolved.Add("type " + type.FullName);
                }
                foreach (MemberReference member in module.GetMemberReferences().Where(m => Scope(m.DeclaringType.GetElementType()).Name == selected))
                {
                    ++members;
                    bool found;
                    try
                    {
                        var method = member as MethodReference;
                        // Cecil's resolution ignores static/instance and generic arity; the runtime does not.
                        MethodDefinition resolved = method == null ? null : method.Resolve();
                        found = method == null ? ((FieldReference)member).Resolve() != null : resolved != null &&
                            resolved.HasThis == method.HasThis && resolved.GenericParameters.Count == method.GenericParameters.Count;
                    }
                    catch (AssemblyResolutionException) { found = false; }
                    if (!found) unresolved.Add("member " + member.FullName);
                }
            }
        }
        foreach (string name in unresolved) Console.WriteLine("UNRESOLVED " + name);
        Console.WriteLine(types + " " + selected + " type references, " + members + " member references, " + unresolved.Count + " unresolved");
        return unresolved.Count == 0 ? 0 : 1;
    }
}
