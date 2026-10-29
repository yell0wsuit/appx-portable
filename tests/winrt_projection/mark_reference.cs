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
            foreach (AssemblyNameReference reference in assembly.MainModule.AssemblyReferences)
            {
                if (reference.Name != selected) continue;
                if (runtime)
                {
                    reference.Version = new Version(4, 0, 0, 0);
                    reference.PublicKeyToken = new byte[] { 0xb7, 0x7a, 0x5c, 0x56, 0x19, 0x34, 0xe0, 0x89 };
                }
                else reference.Attributes |= AssemblyAttributes.WindowsRuntime;
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
