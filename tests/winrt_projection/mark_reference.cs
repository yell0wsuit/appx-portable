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
                assembly.Write(args[1]);
                Console.WriteLine(reference.FullName + " flags=" + reference.Attributes);
                return 0;
            }
        }
        return 1;
    }
}
