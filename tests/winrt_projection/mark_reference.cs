using System;

using Mono.Cecil;
class MarkReference
{
    static int Main(string[] args)
    {
        using (AssemblyDefinition assembly = AssemblyDefinition.ReadAssembly(args[0]))
        {
            foreach (AssemblyNameReference reference in assembly.MainModule.AssemblyReferences)
            {
                if (reference.Name != "Windows") continue;
                reference.Attributes |= AssemblyAttributes.WindowsRuntime;
                if (args.Length > 2 && args[2] == "--abi-setters")
                    foreach (MemberReference member in assembly.MainModule.GetMemberReferences())
                    {
                        var method = member as MethodReference;
                        if (method != null && method.DeclaringType.Scope.Name == "Windows" && method.Name.StartsWith("set_", StringComparison.Ordinal))
                            method.Name = "put_" + method.Name.Substring(4);
                    }
                assembly.Write(args[1]);
                Console.WriteLine(reference.FullName + " flags=" + reference.Attributes);
                return 0;
            }
        }
        return 1;
    }
}
