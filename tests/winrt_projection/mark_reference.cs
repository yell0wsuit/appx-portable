using System;

using Mono.Cecil;
class MarkReference
{
    static int Main(string[] args)
    {
        using (AssemblyDefinition assembly = AssemblyDefinition.ReadAssembly(args[0]))
        {
            bool runtime = args.Length > 2 && args[2] == "--runtime-reference";
            string selected = runtime ? "System.Runtime.WindowsRuntime" : args.Length > 3 && args[2] == "--assembly" ? args[3] : "Windows";
            AssemblyNameReference marked = null;
            foreach (AssemblyNameReference reference in assembly.MainModule.AssemblyReferences)
            {
                // The runtime mark binds every System.Runtime.WindowsRuntime* facade as .NET signs them.
                if (reference.Name != selected && !(runtime && reference.Name.StartsWith(selected + ".", StringComparison.Ordinal))) continue;
                if (runtime)
                {
                    reference.Version = new Version(4, 0, 0, 0);
                    reference.PublicKeyToken = new byte[] { 0xb7, 0x7a, 0x5c, 0x56, 0x19, 0x34, 0xe0, 0x89 };
                    Console.WriteLine(reference.FullName);
                    marked = marked ?? reference;
                    continue;
                }
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
            if (marked == null) return 1;
            assembly.Write(args[1]);
            Console.WriteLine(marked.FullName + " flags=" + marked.Attributes);
            return 0;
        }
    }
}
