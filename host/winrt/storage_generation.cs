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
            "GetFileAsync|(System.String)->Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>" });
        StorageInterface(output, "FileStatics", "Windows.Storage.IStorageFileStatics", new[] {
            "GetFileFromPathAsync|(System.String)->Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>" });
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
        StorageInterface(output, "Random", "Windows.Storage.Streams.IRandomAccessStream", new[] {
            "get_Size|()->System.UInt64", "put_Size|(System.UInt64)->System.Void", "get_Position|()->System.UInt64", "Seek|(System.UInt64)->System.Void",
            "get_CanRead|()->System.Boolean", "get_CanWrite|()->System.Boolean",
            "GetInputStreamAt|(System.UInt64)->Windows.Storage.Streams.IInputStream", "GetOutputStreamAt|(System.UInt64)->Windows.Storage.Streams.IOutputStream",
            "CloneStream|()->Windows.Storage.Streams.IRandomAccessStream" });
        StorageInterface(output, "Content", "Windows.Storage.Streams.IContentTypeProvider", new[] { "get_ContentType|()->System.String" });
        StorageInterface(output, "Closable", "Windows.Foundation.IClosable", new[] { "Close|()->System.Void" });
        StorageFuture(output, "File", "Windows.Storage.StorageFile", false);
        StorageFuture(output, "Stream", "Windows.Storage.Streams.IRandomAccessStreamWithContentType", false);
        StorageFuture(output, "Read", "Windows.Storage.Streams.IBuffer", true);
        output.AppendLine("    }");
    }
    private static void StorageInterface(StringBuilder output, string label, string name, string[] methods)
    {
        TypeDefinition type = Find(name);
        output.AppendLine("        internal static readonly global::System.Guid " + label + " = " + Guid(type) + ";");
        foreach (string entry in methods)
        {
            string[] parts = entry.Split('|');
            CollectionMethod(type, 0, parts[0], parts[1]);
            output.AppendLine("        // " + name + "::" + parts[0] + ", slot " + NamedSlot(type, parts[0]));
            output.AppendLine("        internal const int " + label + "_" + parts[0] + " = " + NamedSlot(type, parts[0]) + ";");
        }
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
        TypeDefinition resultType = Find(result);
        string signature = resultType.IsInterface ? "{" + Iid(resultType) + "}" : "rc(" + result + ";{" + Iid(DefaultInterface(resultType)) + "})";
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
