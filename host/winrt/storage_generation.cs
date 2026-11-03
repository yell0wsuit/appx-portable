// Metadata qualification for the explicit native storage ABI in storage_abi.cs.
using System;
using System.Linq;
using System.Text;
using System.Xml;

using Mono.Cecil;
internal static partial class WinrtProjectionGenerator
{
    private static void StorageContract(StringBuilder output, TypeDefinition type, XmlElement surface)
    {
        if (surface.HasChildNodes || (surface.Name == "Interface") != type.IsInterface ||
            (surface.Name != "Class" && surface.Name != "Interface"))
            throw new NotSupportedException("Unsupported storage surface: " + type.FullName);
        // These CLR declarations are supplied by storage_abi.cs; emit one shared
        // qualified layout when the first storage runtime class is visited.
        if (type.FullName != "Windows.Storage.StorageFile") return;
        output.AppendLine("}");
        output.AppendLine("namespace AppxPortable.WinRT");
        output.AppendLine("{");
        output.AppendLine("    internal static class StorageAbi");
        output.AppendLine("    {");
        StorageInterface(output, "Item", "Windows.Storage.IStorageItem", new[] {
            "get_Name|()->System.String", "get_Path|()->System.String" });
        StorageInterface(output, "Folder", "Windows.Storage.IStorageFolder", new[] {
            "CreateFileAsyncOverloadDefaultOptions|(System.String)->Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>",
            "CreateFileAsync|(System.String,Windows.Storage.CreationCollisionOption)->Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>",
            "GetFileAsync|(System.String)->Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>",
            "GetItemsAsyncOverloadDefaultStartAndCount|()->Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<Windows.Storage.IStorageItem>>" });
        StorageInterface(output, "FileStatics", "Windows.Storage.IStorageFileStatics", new[] {
            "GetFileFromPathAsync|(System.String)->Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>",
            "GetFileFromApplicationUriAsync|(Windows.Foundation.Uri)->Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>" });
        StorageInterface(output, "InputReference", "Windows.Storage.Streams.IInputStreamReference", new[] {
            "OpenSequentialReadAsync|()->Windows.Foundation.IAsyncOperation`1<Windows.Storage.Streams.IInputStream>" });
        StorageInterface(output, "UriFactory", "Windows.Foundation.IUriRuntimeClassFactory", new[] {
            "CreateUri|(System.String)->Windows.Foundation.Uri" });
        StorageInterface(output, "File", "Windows.Storage.IStorageFile", new[] {
            "OpenAsync|(Windows.Storage.FileAccessMode)->Windows.Foundation.IAsyncOperation`1<Windows.Storage.Streams.IRandomAccessStream>" });
        StorageInterface(output, "FileIO", "Windows.Storage.IFileIOStatics", new[] {
            "ReadTextAsync|(Windows.Storage.IStorageFile)->Windows.Foundation.IAsyncOperation`1<System.String>",
            "WriteBufferAsync|(Windows.Storage.IStorageFile,Windows.Storage.Streams.IBuffer)->Windows.Foundation.IAsyncAction" });
        StorageInterface(output, "FileReference", "Windows.Storage.Streams.IRandomAccessStreamReference", new[] {
            "OpenReadAsync|()->Windows.Foundation.IAsyncOperation`1<Windows.Storage.Streams.IRandomAccessStreamWithContentType>" });
        StorageInterface(output, "Info", "Windows.Foundation.IAsyncInfo", new[] {
            "get_Id|()->System.UInt32", "get_Status|()->Windows.Foundation.AsyncStatus", "get_ErrorCode|()->Windows.Foundation.HResult",
            "Cancel|()->System.Void", "Close|()->System.Void" });
        if (!Find("Windows.Storage.Streams.IBuffer").Methods.Select(m => m.Name).SequenceEqual(new[] { "get_Capacity", "get_Length", "put_Length" }))
            throw new NotSupportedException("Unsupported buffer method order");
        StorageInterface(output, "Buffer", "Windows.Storage.Streams.IBuffer", new[] {
            "get_Capacity|()->System.UInt32", "get_Length|()->System.UInt32", "put_Length|(System.UInt32)->System.Void" });
        StorageInterface(output, "BufferFactory", "Windows.Storage.Streams.IBufferFactory", new[] {
            "Create|(System.UInt32)->Windows.Storage.Streams.Buffer" });
        StorageInterface(output, "Input", "Windows.Storage.Streams.IInputStream", new[] {
            "ReadAsync|(Windows.Storage.Streams.IBuffer,System.UInt32,Windows.Storage.Streams.InputStreamOptions)->Windows.Foundation.IAsyncOperationWithProgress`2<Windows.Storage.Streams.IBuffer,System.UInt32>" });
        StorageInterface(output, "Output", "Windows.Storage.Streams.IOutputStream", new string[0]);
        StorageInterface(output, "Random", "Windows.Storage.Streams.IRandomAccessStream", new[] {
            "get_Size|()->System.UInt64", "put_Size|(System.UInt64)->System.Void", "get_Position|()->System.UInt64", "Seek|(System.UInt64)->System.Void",
            "get_CanRead|()->System.Boolean", "get_CanWrite|()->System.Boolean",
            "GetInputStreamAt|(System.UInt64)->Windows.Storage.Streams.IInputStream", "GetOutputStreamAt|(System.UInt64)->Windows.Storage.Streams.IOutputStream",
            "CloneStream|()->Windows.Storage.Streams.IRandomAccessStream" });
        StorageInterface(output, "Content", "Windows.Storage.Streams.IContentTypeProvider", new[] { "get_ContentType|()->System.String" });
        StorageInterface(output, "Closable", "Windows.Foundation.IClosable", new[] { "Close|()->System.Void" });
        StorageInterface(output, "Reader", "Windows.Storage.Streams.IDataReader", new[] {
            "get_UnconsumedBufferLength|()->System.UInt32", "ReadBytes|(System.Byte[])->System.Void",
            "ReadString|(System.UInt32)->System.String",
            "ReadInt16|()->System.Int16", "ReadUInt16|()->System.UInt16", "ReadInt32|()->System.Int32",
            "ReadUInt32|()->System.UInt32", "ReadInt64|()->System.Int64", "ReadUInt64|()->System.UInt64",
            "LoadAsync|(System.UInt32)->Windows.Storage.Streams.DataReaderLoadOperation",
            "DetachStream|()->Windows.Storage.Streams.IInputStream" });
        StorageInterface(output, "ReaderFactory", "Windows.Storage.Streams.IDataReaderFactory", new[] {
            "CreateDataReader|(Windows.Storage.Streams.IInputStream)->Windows.Storage.Streams.DataReader" });
        StorageInterface(output, "ReaderStatics", "Windows.Storage.Streams.IDataReaderStatics", new[] {
            "FromBuffer|(Windows.Storage.Streams.IBuffer)->Windows.Storage.Streams.DataReader" });
        StorageInterface(output, "Writer", "Windows.Storage.Streams.IDataWriter", new[] {
            "WriteString|(System.String)->System.UInt32", "DetachBuffer|()->Windows.Storage.Streams.IBuffer" });
        StorageInterface(output, "WriterFactory", "Windows.Storage.Streams.IDataWriterFactory", new[] {
            "CreateDataWriter|(Windows.Storage.Streams.IOutputStream)->Windows.Storage.Streams.DataWriter" });
        TypeDefinition memory = Find("Windows.Storage.Streams.InMemoryRandomAccessStream"), writer = Find("Windows.Storage.Streams.DataWriter");
        if (DefaultInterface(writer).FullName != "Windows.Storage.Streams.IDataWriter" ||
            DefaultInterface(memory).FullName != "Windows.Storage.Streams.IRandomAccessStream" ||
            !memory.Methods.Any(m => m.IsConstructor && m.Parameters.Count == 0) ||
            !memory.CustomAttributes.Any(a => a.AttributeType.FullName == "Windows.Foundation.Metadata.ActivatableAttribute" &&
                a.ConstructorArguments[0].Type.FullName == "System.UInt32") ||
            !writer.Methods.Any(m => m.IsConstructor && m.Parameters.Count == 1 && m.Parameters[0].ParameterType.FullName == "Windows.Storage.Streams.IOutputStream") ||
            !writer.CustomAttributes.Any(a => a.AttributeType.FullName == "Windows.Foundation.Metadata.ActivatableAttribute" &&
                a.ConstructorArguments[0].Value is TypeReference && ((TypeReference)a.ConstructorArguments[0].Value).FullName == "Windows.Storage.Streams.IDataWriterFactory"))
            throw new NotSupportedException("Unsupported memory-stream or writer activation contract");
        TypeDefinition reader = Find("Windows.Storage.Streams.DataReader"), load = Find("Windows.Storage.Streams.DataReaderLoadOperation");
        if (!reader.Methods.Any(m => m.IsConstructor && m.Parameters.Count == 1 &&
                m.Parameters[0].ParameterType.FullName == "Windows.Storage.Streams.IInputStream") ||
            !reader.CustomAttributes.Any(a => a.AttributeType.FullName == "Windows.Foundation.Metadata.ActivatableAttribute" &&
                a.ConstructorArguments[0].Value is TypeReference && ((TypeReference)a.ConstructorArguments[0].Value).FullName == "Windows.Storage.Streams.IDataReaderFactory") ||
            !load.IsSealed || !load.Interfaces.Any(i => CollectionType(i.InterfaceType) == "Windows.Foundation.IAsyncOperation`1<System.UInt32>"))
            throw new NotSupportedException("Unsupported stream reader activation or load operation contract");
        StorageFuture(output, "Load", "System.UInt32", false);
        StorageFuture(output, "File", "Windows.Storage.StorageFile", false);
        StorageFuture(output, "Random", "Windows.Storage.Streams.IRandomAccessStream", false);
        StorageFuture(output, "Stream", "Windows.Storage.Streams.IRandomAccessStreamWithContentType", false);
        StorageFuture(output, "Input", "Windows.Storage.Streams.IInputStream", false);
        // A folder listing: IAsyncOperation<IVectorView<IStorageItem>> and the view's slots.
        TypeDefinition view = Find("Windows.Foundation.Collections.IVectorView`1");
        CollectionMethod(view, 1, "GetAt", "(System.UInt32)->!0");
        CollectionMethod(view, 1, "get_Size", "()->System.UInt32");
        output.AppendLine("        internal const int View_GetAt = " + NamedSlot(view, "GetAt") + ";");
        output.AppendLine("        internal const int View_get_Size = " + NamedSlot(view, "get_Size") + ";");
        StorageFuture(output, "Items", "pinterface({" + Iid(view) + "};{" + Iid(Find("Windows.Storage.IStorageItem")) + "})", false);
        StorageFuture(output, "Read", "Windows.Storage.Streams.IBuffer", true);
        StorageFuture(output, "Text", "System.String", false);
        StorageAction(output);
        output.AppendLine("    }");
    }
    // A native IAsyncAction: no results type, so no parameterized IIDs.
    private static void StorageAction(StringBuilder output)
    {
        TypeDefinition type = Find("Windows.Foundation.IAsyncAction"), completed = Find("Windows.Foundation.AsyncActionCompletedHandler");
        if (!type.Methods.Select(m => m.Name).SequenceEqual(new[] { "put_Completed", "get_Completed", "GetResults" }))
            throw new NotSupportedException("Unsupported storage action method order");
        StorageInterface(output, "Action", type.FullName, new[] {
            "put_Completed|(Windows.Foundation.AsyncActionCompletedHandler)->System.Void",
            "get_Completed|()->Windows.Foundation.AsyncActionCompletedHandler", "GetResults|()->System.Void" });
        MethodDefinition invoke = completed.Methods.Single(m => m.Name == "Invoke");
        if (CollectionType(invoke.ReturnType) != "System.Void" || !invoke.Parameters.Select(p => CollectionType(p.ParameterType))
            .SequenceEqual(new[] { type.FullName, "Windows.Foundation.AsyncStatus" }))
            throw new NotSupportedException("Unsupported storage action completion ABI");
        output.AppendLine("        internal static readonly FutureAbi ActionFuture = new FutureAbi(" + Guid(type) + ", " + Guid(completed) +
            ", global::System.Guid.Empty, " + NamedSlot(type, "put_Completed") + ", " + NamedSlot(type, "get_Completed") + ", " + NamedSlot(type, "GetResults") + ");");
    }
    private static void StorageInterface(StringBuilder output, string label, string name, string[] methods)
    {
        TypeDefinition type = Find(name);
        output.AppendLine("        internal static readonly global::System.Guid " + label + " = " + Guid(type) + ";");
        foreach (string entry in methods)
        {
            string[] parts = entry.Split('|');
            int slot = Slot(type, StorageMethod(type, parts[0], parts[1]));
            output.AppendLine("        // " + name + "::" + parts[0] + ", slot " + slot);
            output.AppendLine("        internal const int " + label + "_" + parts[0] + " = " + slot + ";");
        }
    }
    // Overloads share a metadata name; their ABI names come from OverloadAttribute.
    private static MethodDefinition StorageMethod(TypeDefinition type, string name, string signature)
    {
        var found = type.Methods.Where(m => AbiName(m) == name).ToList();
        string actual = found.Count == 1 ? "(" + string.Join(",", found[0].Parameters.Select(p => CollectionType(p.ParameterType))) + ")->" + CollectionType(found[0].ReturnType) : null;
        if (!type.IsInterface || type.HasGenericParameters || found.Count != 1 || found[0].IsStatic || !found[0].HasThis ||
            found[0].HasGenericParameters || actual != signature)
            throw new NotSupportedException("Unsupported storage ABI: " + type.FullName + "." + name);
        return found[0];
    }
    private static string AbiName(MethodDefinition method)
    {
        CustomAttribute overload = Attribute(method, "Windows.Foundation.Metadata.OverloadAttribute");
        return overload == null ? method.Name : (string)overload.ConstructorArguments[0].Value;
    }
    private static void StorageFuture(StringBuilder output, string label, string result, bool progress)
    {
        TypeDefinition type = Find("Windows.Foundation.IAsyncOperation" + (progress ? "WithProgress`2" : "`1"));
        TypeDefinition completed = Find("Windows.Foundation.AsyncOperation" + (progress ? "WithProgress" : "") + "CompletedHandler`" + (progress ? "2" : "1"));
        string completedName = completed.FullName + (progress ? "<!0,!1>" : "<!0>");
        int arity = progress ? 2 : 1;
        string[] order = progress ? new[] { "put_Progress", "get_Progress", "put_Completed", "get_Completed", "GetResults" }
            : new[] { "put_Completed", "get_Completed", "GetResults" };
        if (!type.Methods.Select(m => m.Name).SequenceEqual(order)) throw new NotSupportedException("Unsupported storage async method order");
        CollectionMethod(type, arity, "put_Completed", "(" + completedName + ")->System.Void");
        CollectionMethod(type, arity, "get_Completed", "()->" + completedName);
        CollectionMethod(type, arity, "GetResults", "()->!0");
        MethodDefinition invoke = completed.Methods.Single(m => m.Name == "Invoke");
        if (CollectionType(invoke.ReturnType) != "System.Void" || !invoke.Parameters.Select(p => CollectionType(p.ParameterType)).SequenceEqual(new[] {
            type.FullName + (progress ? "<!0,!1>" : "<!0>"), "Windows.Foundation.AsyncStatus" }))
            throw new NotSupportedException("Unsupported storage completion ABI");
        string signature = "string";
        if (result == "System.UInt32") signature = "u4";
        else if (result.StartsWith("pinterface(", StringComparison.Ordinal)) signature = result;
        else if (result != "System.String")
        {
            TypeDefinition resultType = Find(result);
            signature = resultType.IsInterface ? "{" + Iid(resultType) + "}" : "rc(" + result + ";{" + Iid(DefaultInterface(resultType)) + "})";
        }
        string[] arguments = progress ? new[] { signature, "u4" } : new[] { signature };
        string progressIid = "global::System.Guid.Empty";
        if (progress)
        {
            TypeDefinition handler = Find("Windows.Foundation.AsyncOperationProgressHandler`2");
            string handlerName = handler.FullName + "<!0,!1>";
            CollectionMethod(type, 2, "put_Progress", "(" + handlerName + ")->System.Void");
            CollectionMethod(type, 2, "get_Progress", "()->" + handlerName);
            var method = handler.Methods.Single(m => m.Name == "Invoke");
            if (CollectionType(method.ReturnType) != "System.Void" || !method.Parameters.Select(p => CollectionType(p.ParameterType)).SequenceEqual(new[] { type.FullName + "<!0,!1>", "!1" }))
                throw new NotSupportedException("Unsupported storage progress ABI");
            progressIid = "new global::System.Guid(\"" + Parameterized(handler, arguments) + "\")";
        }
        output.AppendLine("        internal static readonly FutureAbi " + label + "Future = new FutureAbi(new global::System.Guid(\"" + Parameterized(type, arguments) + "\"), new global::System.Guid(\"" + Parameterized(completed, arguments) + "\"), " + progressIid + ", " + NamedSlot(type, "put_Completed") + ", " + NamedSlot(type, "get_Completed") + ", " + NamedSlot(type, "GetResults") + ");");
    }
}
