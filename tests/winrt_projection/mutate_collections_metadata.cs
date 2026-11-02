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
                    case "resource-map": name = "Windows.UI.Xaml.ResourceDictionary"; break;
                    case "visibility-width": name = "Windows.UI.Xaml.Visibility"; break;
                    case "connector-id": name = "Windows.UI.Xaml.Markup.IComponentConnector"; break;
                    case "uri-return": name = "Windows.Foundation.IUriRuntimeClass"; break;
                    case "uri-factory": name = "Windows.Foundation.IUriRuntimeClassFactory"; break;
                    case "typed-event": name = "Windows.Foundation.TypedEventHandler`2"; break;
                    case "network-enum": name = "Windows.Networking.Connectivity.NetworkConnectivityLevel"; break;
                    case "iterator": name = "Windows.Foundation.Collections.IIterator`1"; break;
                    case "writer-string": name = "Windows.Storage.Streams.IDataWriter"; break;
                    case "writer-factory": name = "Windows.Storage.Streams.IDataWriterFactory"; break;
                    case "file-open": name = "Windows.Storage.IStorageFile"; break;
                    case "stream-size": name = "Windows.Storage.Streams.IRandomAccessStream"; break;
                    case "reader-load": name = "Windows.Storage.Streams.IDataReader"; break;
                    case "reader-factory": name = "Windows.Storage.Streams.IDataReaderFactory"; break;
                    case "read-count": name = "Windows.Storage.Streams.IInputStream"; break;
                    case "buffer-order": name = "Windows.Storage.Streams.IBuffer"; break;
                    case "future-order": name = "Windows.Foundation.IAsyncOperation`1"; break;
                    case "view-order": name = "Windows.Foundation.Collections.IMapView`2"; break;
                    default: name = "Windows.Foundation.Collections.IMap`2"; break;
                }
                var type = module.Types.SingleOrDefault(t => t.FullName == name);
                if (type == null) { File.Copy(path, target, true); continue; }
                if (args[2] == "resource-map")
                {
                    var map = (GenericInstanceType)type.Interfaces.Single(i => i.InterfaceType.FullName == "Windows.Foundation.Collections.IMap`2<System.Object,System.Object>").InterfaceType;
                    map.GenericArguments[0] = module.TypeSystem.String;
                }
                else if (args[2] == "visibility-width") type.Fields.Single(f => f.Name == "value__").FieldType = module.TypeSystem.Int64;
                else if (args[2] == "connector-id") type.Methods.Single(m => m.Name == "Connect").Parameters[0].ParameterType = module.TypeSystem.Int64;
                else if (args[2] == "uri-return") type.Methods.Single(m => m.Name == "get_AbsoluteUri").ReturnType = module.TypeSystem.Object;
                else if (args[2] == "uri-factory") type.Methods.Single(m => m.Name == "CreateUri").Parameters[0].ParameterType = module.TypeSystem.IntPtr;
                else if (args[2] == "typed-event") type.Methods.Single(m => m.Name == "Invoke").Parameters[1].ParameterType = module.TypeSystem.UInt32;
                else if (args[2] == "network-enum") type.Fields.Single(f => f.Name == "value__").FieldType = module.TypeSystem.Int64;
                else if (args[2] == "insert") type.Methods.Single(m => m.Name == "Insert").Parameters[1].ParameterType = module.TypeSystem.UInt64;
                else if (args[2] == "order" || args[2] == "buffer-order" || args[2] == "future-order" || args[2] == "view-order") { var first = type.Methods[0]; type.Methods.RemoveAt(0); type.Methods.Add(first); }
                else if (args[2] == "iterator") type.Methods.Single(m => m.Name == "get_HasCurrent").ReturnType = module.TypeSystem.UInt32;
                else if (args[2] == "stream-size") type.Methods.Single(m => m.Name == "get_Size").ReturnType = module.TypeSystem.UInt32;
                else if (args[2] == "writer-string") type.Methods.Single(m => m.Name == "WriteString").ReturnType = module.TypeSystem.UInt64;
                else if (args[2] == "writer-factory") type.Methods.Single(m => m.Name == "CreateDataWriter").Parameters[0].ParameterType = module.TypeSystem.Object;
                else if (args[2] == "file-open") type.Methods.Single(m => m.Name == "OpenAsync").Parameters[0].ParameterType = module.TypeSystem.UInt64;
                else if (args[2] == "reader-load") type.Methods.Single(m => m.Name == "LoadAsync").Parameters[0].ParameterType = module.TypeSystem.UInt64;
                else if (args[2] == "reader-factory") type.Methods.Single(m => m.Name == "CreateDataReader").Parameters[0].ParameterType = module.TypeSystem.Object;
                else if (args[2] == "read-count") type.Methods.Single(m => m.Name == "ReadAsync").Parameters[1].ParameterType = module.TypeSystem.UInt64;
                else throw new ArgumentException("Unknown mutation");
                module.Write(target);
                changed = true;
            }
        }
        return changed ? 0 : 1;
    }
}
