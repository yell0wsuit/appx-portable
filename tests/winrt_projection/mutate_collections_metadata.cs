using System;
using System.IO;
using System.Linq;

using Mono.Cecil;
class MutateCollectionsMetadata
{
    static int Main(string[] args)
    {
        Directory.CreateDirectory(args[1]);
        bool changed = false;
        foreach (string path in Directory.GetFiles(args[0], "*.winmd"))
        {
            string target = Path.Combine(args[1], Path.GetFileName(path));
            using (var module = ModuleDefinition.ReadModule(path, new ReaderParameters { ApplyWindowsRuntimeProjections = false, InMemory = true }))
            {
                string name = args[2] != "iterator" ? "Windows.Foundation.Collections.IMap`2" : "Windows.Foundation.Collections.IIterator`1";
                var type = module.Types.SingleOrDefault(t => t.FullName == name);
                if (type == null) { File.Copy(path, target, true); continue; }
                if (args[2] == "insert") type.Methods.Single(m => m.Name == "Insert").Parameters[1].ParameterType = module.TypeSystem.UInt64;
                else if (args[2] == "order") { var first = type.Methods[0]; type.Methods.RemoveAt(0); type.Methods.Add(first); }
                else type.Methods.Single(m => m.Name == "get_HasCurrent").ReturnType = module.TypeSystem.UInt32;
                module.Write(target);
                changed = true;
            }
        }
        return changed ? 0 : 1;
    }
}
