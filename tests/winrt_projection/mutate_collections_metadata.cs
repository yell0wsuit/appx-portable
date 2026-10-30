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
                string name;
                switch (args[2])
                {
                    case "typed-event": name = "Windows.Foundation.TypedEventHandler`2"; break;
                    case "network-enum": name = "Windows.Networking.Connectivity.NetworkConnectivityLevel"; break;
                    case "iterator": name = "Windows.Foundation.Collections.IIterator`1"; break;
                    case "stream-size": name = "Windows.Storage.Streams.IRandomAccessStream"; break;
                    case "read-count": name = "Windows.Storage.Streams.IInputStream"; break;
                    case "buffer-order": name = "Windows.Storage.Streams.IBuffer"; break;
                    case "future-order": name = "Windows.Foundation.IAsyncOperation`1"; break;
                    default: name = "Windows.Foundation.Collections.IMap`2"; break;
                }
                var type = module.Types.SingleOrDefault(t => t.FullName == name);
                if (type == null) { File.Copy(path, target, true); continue; }
                if (args[2] == "typed-event") type.Methods.Single(m => m.Name == "Invoke").Parameters[1].ParameterType = module.TypeSystem.UInt32;
                else if (args[2] == "network-enum") type.Fields.Single(f => f.Name == "value__").FieldType = module.TypeSystem.Int64;
                else if (args[2] == "insert") type.Methods.Single(m => m.Name == "Insert").Parameters[1].ParameterType = module.TypeSystem.UInt64;
                else if (args[2] == "order" || args[2] == "buffer-order" || args[2] == "future-order") { var first = type.Methods[0]; type.Methods.RemoveAt(0); type.Methods.Add(first); }
                else if (args[2] == "iterator") type.Methods.Single(m => m.Name == "get_HasCurrent").ReturnType = module.TypeSystem.UInt32;
                else if (args[2] == "stream-size") type.Methods.Single(m => m.Name == "get_Size").ReturnType = module.TypeSystem.UInt32;
                else if (args[2] == "read-count") type.Methods.Single(m => m.Name == "ReadAsync").Parameters[1].ParameterType = module.TypeSystem.UInt64;
                else throw new ArgumentException("Unknown mutation");
                module.Write(target);
                changed = true;
            }
        }
        return changed ? 0 : 1;
    }
}
