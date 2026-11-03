using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

using Windows.Foundation;
using Windows.Storage.Streams;
[assembly: InternalsVisibleTo("System.Runtime.WindowsRuntime")]
namespace AppxPortable.WinRT
{
    internal sealed class FutureAbi
    {
        internal readonly Guid Iid;
        internal readonly int PutCompleted, GetCompleted, Results;
        internal readonly NativeInterface Completed, Progress;
        internal FutureAbi(Guid iid, Guid completed, Guid progress, int put, int get, int results)
        {
            Iid = iid; PutCompleted = put; GetCompleted = get; Results = results;
            Completed = new NativeInterface(completed, false, NativeAsync.completed);
            if (progress != Guid.Empty) Progress = new NativeInterface(progress, false, NativeAsync.progress);
        }
    }
    internal abstract class NativeAsync : INativeWrapper
    {
        protected readonly NativeObject native;
        protected readonly FutureAbi abi;
        protected NativeAsync(NativeObject native, FutureAbi abi) { this.native = native; this.abi = abi; }
        NativeObject INativeWrapper.Native { get { return native; } }
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] internal delegate int CompletedCall(IntPtr self, IntPtr operation, int status);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] internal delegate int ProgressCall(IntPtr self, IntPtr operation, uint value);
        internal static readonly CompletedCall completed = (self, operation, status) => ManagedObject.Call(self, target => ((Callback)target).Completed(operation, status));
        internal static readonly ProgressCall progress = (self, operation, value) => ManagedObject.Call(self, target => ((Callback)target).Progress(operation, value));
        protected sealed class Callback
        {
            internal Delegate Original;
            internal Action<IntPtr, int> Completed;
            internal Action<IntPtr, uint> Progress;
        }
        protected void Set(int slot, Callback callback, NativeInterface contract)
        {
            using (var invocation = Errors.Begin())
            using (var held = callback == null ? null : ManagedObject.Hold(callback, contract))
                native.Call(slot, held == null ? IntPtr.Zero : held.Pointer);
        }
        protected T Get<T>(int slot) where T : class
        {
            using (NativeObject callback = native.Object(slot))
            {
                if (callback == null) return null;
                var managed = ManagedObject.Find(callback.Identity()) as Callback;
                if (managed == null) throw new NotSupportedException("Foreign native async callback cannot be projected");
                return managed.Original as T;
            }
        }
        public uint Id { get { using (var info = native.QueryInterface(StorageAbi.Info)) return info.UInt32(StorageAbi.Info_get_Id); } }
        public AsyncStatus Status { get { using (var info = native.QueryInterface(StorageAbi.Info)) return (AsyncStatus)info.UInt32(StorageAbi.Info_get_Status); } }
        public Exception ErrorCode
        {
            get
            {
                using (var info = native.QueryInterface(StorageAbi.Info))
                {
                    int hr = unchecked((int)info.UInt32(StorageAbi.Info_get_ErrorCode));
                    return hr < 0 ? Marshal.GetExceptionForHR(hr, new IntPtr(-1)) : null;
                }
            }
        }
        public void Cancel() { using (var info = native.QueryInterface(StorageAbi.Info)) info.Call(StorageAbi.Info_Cancel); }
        public void Close() { using (var info = native.QueryInterface(StorageAbi.Info)) info.Call(StorageAbi.Info_Close); }
    }
    internal sealed class NativeFuture<T> : NativeAsync, IAsyncOperation<T>
    {
        // Reads GetResults from the operation itself, so results need not be objects.
        private readonly Func<NativeObject, T> read;
        private readonly Func<NativeObject, IAsyncOperation<T>> reconstruct;
        internal NativeFuture(NativeObject value, FutureAbi abi, Func<NativeObject, T> read,
            Func<NativeObject, IAsyncOperation<T>> reconstruct = null) : base(value, abi)
        { this.read = read; this.reconstruct = reconstruct; }
        internal static IAsyncOperation<T> FromNative(NativeObject value, FutureAbi abi, Func<NativeObject, T> convert)
        {
            return Wrap(value, abi, operation => convert(operation.Object(abi.Results)));
        }
        internal static IAsyncOperation<T> Wrap(NativeObject value, FutureAbi abi, Func<NativeObject, T> read)
        {
            return NativeObject.Wrap<IAsyncOperation<T>>(value, abi.Iid, v => new NativeFuture<T>(v, abi, read));
        }
        public T GetResults() { return read(native); }
        public AsyncOperationCompletedHandler<T> Completed
        {
            get { return Get<AsyncOperationCompletedHandler<T>>(abi.GetCompleted); }
            set
            {
                var layout = abi; var reader = read; var factory = reconstruct;
                Set(abi.PutCompleted, value == null ? null : new Callback
                {
                    Original = value,
                    // Reconstruct from the native callback argument. Capturing
                    // this wrapper would create a native/CCW reference cycle.
                    Completed = (pointer, status) => value(factory == null ? Wrap(NativeObject.Borrow(pointer), layout, reader) : factory(NativeObject.Borrow(pointer)), (AsyncStatus)status)
                }, abi.Completed);
            }
        }
    }
    internal sealed class NativeAction : NativeAsync, IAsyncAction
    {
        private NativeAction(NativeObject value) : base(value, StorageAbi.ActionFuture) { }
        internal static IAsyncAction FromNative(NativeObject value)
        {
            return NativeObject.Wrap<IAsyncAction>(value, StorageAbi.ActionFuture.Iid, v => new NativeAction(v));
        }
        // Rethrows the action's failure, as Windows' GetResults does.
        public void GetResults() { native.Call(abi.Results); }
        public AsyncActionCompletedHandler Completed
        {
            get { return Get<AsyncActionCompletedHandler>(abi.GetCompleted); }
            set
            {
                Set(abi.PutCompleted, value == null ? null : new Callback
                {
                    Original = value,
                    Completed = (pointer, status) => value(FromNative(NativeObject.Borrow(pointer)), (AsyncStatus)status)
                }, abi.Completed);
            }
        }
    }
    internal sealed class NativeRead : NativeAsync, IAsyncOperationWithProgress<IBuffer, uint>
    {
        private NativeRead(NativeObject value) : base(value, StorageAbi.ReadFuture) { }
        internal static IAsyncOperationWithProgress<IBuffer, uint> FromNative(NativeObject value)
        {
            return NativeObject.Wrap<IAsyncOperationWithProgress<IBuffer, uint>>(value, StorageAbi.ReadFuture.Iid, v => new NativeRead(v));
        }
        public IBuffer GetResults() { return Windows.Storage.Streams.Buffer.FromNative(native.Object(abi.Results)); }
        public AsyncOperationWithProgressCompletedHandler<IBuffer, uint> Completed
        {
            get { return Get<AsyncOperationWithProgressCompletedHandler<IBuffer, uint>>(abi.GetCompleted); }
            set
            {
                Set(abi.PutCompleted, value == null ? null : new Callback
                {
                    Original = value,
                    Completed = (pointer, status) => value(FromNative(NativeObject.Borrow(pointer)), (AsyncStatus)status)
                }, abi.Completed);
            }
        }
        public AsyncOperationProgressHandler<IBuffer, uint> Progress
        {
            get { return Get<AsyncOperationProgressHandler<IBuffer, uint>>(abi.PutCompleted - 1); }
            set
            {
                Set(abi.PutCompleted - 2, value == null ? null : new Callback
                {
                    Original = value,
                    Progress = (pointer, progress) => value(FromNative(NativeObject.Borrow(pointer)), progress)
                }, abi.Progress);
            }
        }
    }
    internal static class StorageCalls
    {
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] internal delegate int WriteStringCall(IntPtr self, IntPtr text, out uint count);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] internal delegate int TextCall(IntPtr self, IntPtr text, out IntPtr value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] internal delegate int ObjectCall(IntPtr self, IntPtr argument, out IntPtr value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] internal delegate int ObjectPairCall(IntPtr self, IntPtr first, IntPtr second, out IntPtr value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] internal delegate int OptionCall(IntPtr self, int option, out IntPtr value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] internal delegate int TextOptionCall(IntPtr self, IntPtr text, int option, out IntPtr value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] internal delegate int UIntCall(IntPtr self, uint number, out IntPtr value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] internal delegate int SetUIntCall(IntPtr self, uint number);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] internal delegate int GetWideCall(IntPtr self, out ulong value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] internal delegate int SetWideCall(IntPtr self, ulong value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] internal delegate int WideCall(IntPtr self, ulong number, out IntPtr value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] internal delegate int BoolCall(IntPtr self, out byte value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] internal delegate int ReadCall(IntPtr self, IntPtr buffer, uint count, int options, out IntPtr value);
        internal static NativeObject Text(NativeObject receiver, int slot, string value)
        {
            if (value == null) throw new ArgumentNullException("value");
            if (value.IndexOf('\0') >= 0) throw new ArgumentException("A file path cannot contain NUL", "value");
            using (var access = new NativeObject.Access(receiver))
            using (var text = new NativeString(value))
            {
                IntPtr result; int hr = access.Method<TextCall>(slot)(access.Self, text.Value, out result);
                return NativeObject.TakeResult(hr, result);
            }
        }
        internal static ulong Wide(NativeObject native, int slot)
        {
            using (var access = new NativeObject.Access(native)) { ulong result; Errors.Check(access.Method<GetWideCall>(slot)(access.Self, out result)); return result; }
        }
        internal static void SetWide(NativeObject native, int slot, ulong value)
        { using (var access = new NativeObject.Access(native)) Errors.Check(access.Method<SetWideCall>(slot)(access.Self, value)); }
        internal static bool Boolean(NativeObject native, int slot)
        { using (var access = new NativeObject.Access(native)) { byte result; Errors.Check(access.Method<BoolCall>(slot)(access.Self, out result)); return result != 0; } }
        internal static NativeObject WideObject(NativeObject native, int slot, ulong value)
        { using (var access = new NativeObject.Access(native)) { IntPtr result; int hr = access.Method<WideCall>(slot)(access.Self, value, out result); return NativeObject.TakeResult(hr, result); } }
    }
    internal static class StorageBuffer
    {
        // IBufferByteAccess from robuffer.h; a classic IUnknown interface.
        private static readonly Guid bytes = new Guid("905a0fef-bc53-11df-8c49-001e4fc686da");
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int BytesCall(IntPtr self, out IntPtr value);
        internal static void Copy(IBuffer buffer, byte[] array, int offset, int count, bool intoBuffer)
        {
            if (buffer == null) throw new ArgumentNullException("buffer");
            if (array == null) throw new ArgumentNullException("array");
            if (offset < 0 || count < 0 || offset > array.Length - count) throw new ArgumentOutOfRangeException("offset");
            if ((uint)count > buffer.Capacity || (!intoBuffer && (uint)count > buffer.Length)) throw new ArgumentException("Buffer is too small");
            using (var native = NativeObject.Argument(buffer, bytes))
            using (var access = new NativeObject.Access(native))
            {
                IntPtr pointer; Errors.Check(access.Method<BytesCall>(3)(access.Self, out pointer));
                if (count != 0) { if (intoBuffer) Marshal.Copy(array, offset, pointer, count); else Marshal.Copy(pointer, array, offset, count); }
            }
            if (intoBuffer) buffer.Length = (uint)count;
        }
    }
}
namespace Windows.Storage
{
    using AppxPortable.WinRT;
    public interface IStorageItem { string Name { get; } string Path { get; } }
    public interface IStorageFile : IStorageItem { }
    public sealed class StorageFolder : IStorageItem, INativeWrapper
    {
        private readonly NativeObject native;
        private StorageFolder(NativeObject value) { native = value; }
        NativeObject INativeWrapper.Native { get { return native; } }
        internal static StorageFolder FromNative(NativeObject value) { return NativeObject.Wrap<StorageFolder>(value, StorageAbi.Folder, v => new StorageFolder(v)); }
        public string Path { get { using (var item = native.QueryInterface(StorageAbi.Item)) return item.String(StorageAbi.Item_get_Path); } }
        public string Name { get { using (var item = native.QueryInterface(StorageAbi.Item)) return item.String(StorageAbi.Item_get_Name); } }
        // The folder's files and folders, listed by the shim when the operation starts.
        public IAsyncOperation<global::System.Collections.Generic.IReadOnlyList<IStorageItem>> GetItemsAsync()
        {
            return NativeFuture<global::System.Collections.Generic.IReadOnlyList<IStorageItem>>.FromNative(
                native.Object(StorageAbi.Folder_GetItemsAsyncOverloadDefaultStartAndCount), StorageAbi.ItemsFuture, Items);
        }
        private static global::System.Collections.Generic.IReadOnlyList<IStorageItem> Items(NativeObject view)
        {
            var items = new global::System.Collections.Generic.List<IStorageItem>();
            using (view)
            {
                uint count = view.UInt32(StorageAbi.View_get_Size);
                for (uint i = 0; i < count; ++i)
                {
                    IntPtr item;
                    using (var access = new NativeObject.Access(view))
                        Errors.Check(access.Method<StorageCalls.UIntCall>(StorageAbi.View_GetAt)(access.Self, i, out item));
                    var native = NativeObject.TakeResult(0, item);
                    items.Add(native.RuntimeClassName() == "Windows.Storage.StorageFolder" ? (IStorageItem)FromNative(native) : StorageFile.FromNative(native));
                }
            }
            return items.AsReadOnly();
        }
        public IAsyncOperation<StorageFile> GetFileAsync(string name) { return NativeFuture<StorageFile>.FromNative(StorageCalls.Text(native, StorageAbi.Folder_GetFileAsync, name), StorageAbi.FileFuture, StorageFile.FromNative); }
        public IAsyncOperation<StorageFile> CreateFileAsync(string desiredName) { return NativeFuture<StorageFile>.FromNative(StorageCalls.Text(native, StorageAbi.Folder_CreateFileAsyncOverloadDefaultOptions, desiredName), StorageAbi.FileFuture, StorageFile.FromNative); }
        public IAsyncOperation<StorageFile> CreateFileAsync(string desiredName, CreationCollisionOption options)
        {
            if (desiredName == null) throw new ArgumentNullException("desiredName");
            using (var access = new NativeObject.Access(native))
            using (var text = new NativeString(desiredName))
            {
                IntPtr result; int hr = access.Method<StorageCalls.TextOptionCall>(StorageAbi.Folder_CreateFileAsync)(access.Self, text.Value, (int)options, out result);
                return NativeFuture<StorageFile>.FromNative(NativeObject.TakeResult(hr, result), StorageAbi.FileFuture, StorageFile.FromNative);
            }
        }
    }
    public sealed class StorageFile : IStorageFile, INativeWrapper
    {
        private readonly NativeObject native;
        private StorageFile(NativeObject value) { native = value; }
        NativeObject INativeWrapper.Native { get { return native; } }
        internal static StorageFile FromNative(NativeObject value) { return NativeObject.Wrap<StorageFile>(value, StorageAbi.Item, v => new StorageFile(v)); }
        public string Path { get { using (var item = native.QueryInterface(StorageAbi.Item)) return item.String(StorageAbi.Item_get_Path); } }
        public string Name { get { using (var item = native.QueryInterface(StorageAbi.Item)) return item.String(StorageAbi.Item_get_Name); } }
        public static IAsyncOperation<StorageFile> GetFileFromPathAsync(string path)
        {
            using (var factory = NativeObject.Factory("Windows.Storage.StorageFile", StorageAbi.FileStatics))
                return NativeFuture<StorageFile>.FromNative(StorageCalls.Text(factory, StorageAbi.FileStatics_GetFileFromPathAsync, path), StorageAbi.FileFuture, FromNative);
        }
        // ms-appx:/// and ms-appdata:/// resolve in the shim, against the package and app data.
        public static IAsyncOperation<StorageFile> GetFileFromApplicationUriAsync(Uri uri)
        {
            if (uri == null) throw new ArgumentNullException("uri");
            if (!uri.IsAbsoluteUri) throw new ArgumentException("An application URI must be absolute", "uri");
            using (var uris = NativeObject.Factory("Windows.Foundation.Uri", StorageAbi.UriFactory))
            using (var native = StorageCalls.Text(uris, StorageAbi.UriFactory_CreateUri, uri.AbsoluteUri))
            using (var factory = NativeObject.Factory("Windows.Storage.StorageFile", StorageAbi.FileStatics))
            using (var access = new NativeObject.Access(factory))
            using (var argument = new NativeObject.Access(native))
            {
                IntPtr result;
                int hr = access.Method<StorageCalls.ObjectCall>(StorageAbi.FileStatics_GetFileFromApplicationUriAsync)(access.Self, argument.Self, out result);
                return NativeFuture<StorageFile>.FromNative(NativeObject.TakeResult(hr, result), StorageAbi.FileFuture, FromNative);
            }
        }
        public IAsyncOperation<IInputStream> OpenSequentialReadAsync()
        {
            using (var reference = native.QueryInterface(StorageAbi.InputReference))
                return NativeFuture<IInputStream>.FromNative(reference.Object(StorageAbi.InputReference_OpenSequentialReadAsync), StorageAbi.InputFuture, InputStream.FromNative);
        }
        public IAsyncOperation<IRandomAccessStream> OpenAsync(FileAccessMode accessMode)
        {
            using (var file = native.QueryInterface(StorageAbi.File))
            using (var access = new NativeObject.Access(file))
            {
                IntPtr result;
                int hr = access.Method<StorageCalls.OptionCall>(StorageAbi.File_OpenAsync)(access.Self, (int)accessMode, out result);
                return NativeFuture<IRandomAccessStream>.FromNative(NativeObject.TakeResult(hr, result), StorageAbi.RandomFuture, RandomStream.FromRandom);
            }
        }
        public IAsyncOperation<IRandomAccessStreamWithContentType> OpenReadAsync()
        {
            using (var reference = native.QueryInterface(StorageAbi.FileReference))
                return NativeFuture<IRandomAccessStreamWithContentType>.FromNative(reference.Object(StorageAbi.FileReference_OpenReadAsync), StorageAbi.StreamFuture, RandomStream.FromContent);
        }
    }
    public static class FileIO
    {
        public static IAsyncOperation<string> ReadTextAsync(IStorageFile file)
        {
            // Null reaches the native call, which fails with E_POINTER as Windows does.
            using (var factory = NativeObject.Factory("Windows.Storage.FileIO", StorageAbi.FileIO))
            using (var argument = file == null ? null : NativeObject.Argument(file, StorageAbi.File))
            using (var access = new NativeObject.Access(factory))
            using (var target = argument == null ? null : new NativeObject.Access(argument))
            {
                IntPtr result; int hr = access.Method<StorageCalls.ObjectCall>(StorageAbi.FileIO_ReadTextAsync)(access.Self, target == null ? IntPtr.Zero : target.Self, out result);
                return NativeFuture<string>.Wrap(NativeObject.TakeResult(hr, result), StorageAbi.TextFuture, operation => operation.String(StorageAbi.TextFuture.Results));
            }
        }
        // Lines of the file's text: CR, LF and CRLF end a line; a final line ending adds no empty line.
        public static IAsyncOperation<global::System.Collections.Generic.IList<string>> ReadLinesAsync(IStorageFile file)
        {
            var lines = new ManagedOperation<global::System.Collections.Generic.IList<string>>();
            var text = ReadTextAsync(file);
            text.Completed = (operation, status) =>
            {
                if (status != AsyncStatus.Completed) { lines.Fail(operation.ErrorCode ?? new OperationCanceledException()); return; }
                try
                {
                    string value = operation.GetResults();
                    var split = new global::System.Collections.Generic.List<string>(
                        global::System.Text.RegularExpressions.Regex.Split(value, "\r\n|\n|\r"));
                    if (value.Length == 0 || split[split.Count - 1].Length == 0) split.RemoveAt(split.Count - 1);
                    lines.Complete(split);
                }
                catch (Exception error) { lines.Fail(error); }
            };
            return lines;
        }
        // Whole-file writes, as FileIO replaces the contents: UTF-8 text without a BOM, or bytes.
        public static IAsyncAction WriteTextAsync(IStorageFile file, string contents)
        {
            if (file == null) throw new ArgumentNullException("file");
            return ManagedAction.Run(() => global::System.IO.File.WriteAllText(file.Path, contents ?? "", new global::System.Text.UTF8Encoding(false)));
        }
        public static IAsyncAction WriteBytesAsync(IStorageFile file, byte[] buffer)
        {
            if (file == null) throw new ArgumentNullException("file");
            return ManagedAction.Run(() => global::System.IO.File.WriteAllBytes(file.Path, buffer ?? new byte[0]));
        }
        public static IAsyncAction WriteBufferAsync(IStorageFile file, IBuffer buffer)
        {
            // Nulls reach the native call, which fails with E_POINTER.
            using (var factory = NativeObject.Factory("Windows.Storage.FileIO", StorageAbi.FileIO))
            using (var target = file == null ? null : NativeObject.Argument(file, StorageAbi.File))
            using (var data = buffer == null ? null : NativeObject.Argument(buffer, StorageAbi.Buffer))
            using (var access = new NativeObject.Access(factory))
            using (var targetAccess = target == null ? null : new NativeObject.Access(target))
            using (var dataAccess = data == null ? null : new NativeObject.Access(data))
            {
                IntPtr result; int hr = access.Method<StorageCalls.ObjectPairCall>(StorageAbi.FileIO_WriteBufferAsync)(access.Self,
                    targetAccess == null ? IntPtr.Zero : targetAccess.Self, dataAccess == null ? IntPtr.Zero : dataAccess.Self, out result);
                return NativeAction.FromNative(NativeObject.TakeResult(hr, result));
            }
        }
    }
}
namespace Windows.Storage.Streams
{
    using AppxPortable.WinRT;
    public interface IBuffer { uint Capacity { get; } uint Length { get; set; } }
    public interface IInputStream : IDisposable { IAsyncOperationWithProgress<IBuffer, uint> ReadAsync(IBuffer buffer, uint count, InputStreamOptions options); }
    public interface IOutputStream : IDisposable { IAsyncOperationWithProgress<uint, uint> WriteAsync(IBuffer buffer); IAsyncOperation<bool> FlushAsync(); }
    public interface IContentTypeProvider { string ContentType { get; } }
    public interface IRandomAccessStream : IInputStream, IOutputStream
    {
        ulong Size { get; set; }
        ulong Position { get; }
        bool CanRead { get; }
        bool CanWrite { get; }
        void Seek(ulong position); IInputStream GetInputStreamAt(ulong position); IOutputStream GetOutputStreamAt(ulong position); IRandomAccessStream CloneStream();
    }
    public interface IRandomAccessStreamWithContentType : IRandomAccessStream, IContentTypeProvider { }
    public sealed class Buffer : IBuffer, INativeWrapper
    {
        private readonly NativeObject native;
        private Buffer(NativeObject value) { native = value; }
        NativeObject INativeWrapper.Native { get { return native; } }
        public Buffer(uint capacity)
        {
            using (var factory = NativeObject.Factory("Windows.Storage.Streams.Buffer", StorageAbi.BufferFactory))
            using (var access = new NativeObject.Access(factory))
            {
                IntPtr result; int hr = access.Method<StorageCalls.UIntCall>(StorageAbi.BufferFactory_Create)(access.Self, capacity, out result);
                native = NativeObject.TakeResult(hr, result);
            }
            native.Remember(this);
        }
        internal static IBuffer FromNative(NativeObject value) { return NativeObject.Wrap<IBuffer>(value, StorageAbi.Buffer, v => new Buffer(v)); }
        public uint Capacity { get { return native.UInt32(StorageAbi.Buffer_get_Capacity); } }
        public uint Length
        {
            get { return native.UInt32(StorageAbi.Buffer_get_Length); }
            set { using (var access = new NativeObject.Access(native)) Errors.Check(access.Method<StorageCalls.SetUIntCall>(StorageAbi.Buffer_put_Length)(access.Self, value)); }
        }
    }
    public sealed class InMemoryRandomAccessStream : IRandomAccessStream, INativeWrapper
    {
        private readonly RandomStream stream;
        NativeObject INativeWrapper.Native { get { return ((INativeWrapper)stream).Native; } }
        private InMemoryRandomAccessStream(NativeObject value) { stream = new RandomStream(value); }
        public InMemoryRandomAccessStream()
        {
            using (var factory = NativeObject.Factory("Windows.Storage.Streams.InMemoryRandomAccessStream", new Guid("00000035-0000-0000-c000-000000000046")))
            using (var value = factory.Object(6)) stream = new RandomStream(value.QueryInterface(StorageAbi.Random));
            NativeObject.Remember(this, ((INativeWrapper)stream).Native);
        }
        internal static InMemoryRandomAccessStream FromNative(NativeObject value)
        { return NativeObject.Wrap<InMemoryRandomAccessStream>(value, StorageAbi.Random, v => new InMemoryRandomAccessStream(v)); }
        public ulong Size { get { return stream.Size; } set { stream.Size = value; } }
        public ulong Position { get { return stream.Position; } }
        public bool CanRead { get { return stream.CanRead; } }
        public bool CanWrite { get { return stream.CanWrite; } }
        public void Seek(ulong position) { stream.Seek(position); }
        public IInputStream GetInputStreamAt(ulong position) { return stream.GetInputStreamAt(position); }
        public IOutputStream GetOutputStreamAt(ulong position) { return stream.GetOutputStreamAt(position); }
        public IRandomAccessStream CloneStream()
        { return FromNative(((INativeWrapper)stream).Native.Object(StorageAbi.Random_CloneStream)); }
        public IAsyncOperationWithProgress<IBuffer, uint> ReadAsync(IBuffer buffer, uint count, InputStreamOptions options) { return stream.ReadAsync(buffer, count, options); }
        public IAsyncOperationWithProgress<uint, uint> WriteAsync(IBuffer buffer) { return stream.WriteAsync(buffer); }
        public IAsyncOperation<bool> FlushAsync() { return stream.FlushAsync(); }
        public void Dispose() { stream.Dispose(); }
    }
    public sealed class DataWriter : IDisposable, INativeWrapper
    {
        private readonly NativeObject native;
        NativeObject INativeWrapper.Native { get { return native; } }
        private DataWriter(NativeObject value) { native = value; }
        internal static DataWriter FromNative(NativeObject value)
        { return NativeObject.Wrap<DataWriter>(value, StorageAbi.Writer, v => new DataWriter(v)); }
        public DataWriter(IOutputStream outputStream)
        {
            using (var factory = NativeObject.Factory("Windows.Storage.Streams.DataWriter", StorageAbi.WriterFactory))
            using (var output = NativeObject.Argument(outputStream, StorageAbi.Output))
            using (var access = new NativeObject.Access(factory))
            {
                IntPtr result;
                int hr = access.Method<StorageCalls.ObjectCall>(StorageAbi.WriterFactory_CreateDataWriter)(access.Self,
                    output == null ? IntPtr.Zero : output.DangerousGetHandle(), out result);
                native = NativeObject.TakeResult(hr, result);
            }
            NativeObject.Remember(this, native);
        }
        public uint WriteString(string value)
        {
            using (var text = new NativeString(value))
            using (var access = new NativeObject.Access(native))
            {
                uint count;
                Errors.Check(access.Method<StorageCalls.WriteStringCall>(StorageAbi.Writer_WriteString)(access.Self, text.Value, out count));
                return count;
            }
        }
        public IBuffer DetachBuffer() { return Buffer.FromNative(native.Object(StorageAbi.Writer_DetachBuffer)); }
        public void Dispose() { using (var close = native.QueryInterface(StorageAbi.Closable)) close.Call(StorageAbi.Closable_Close); }
    }
    public sealed class DataReader : IDisposable, INativeWrapper
    {
        private readonly NativeObject native;
        NativeObject INativeWrapper.Native { get { return native; } }
        private DataReader(NativeObject value) { native = value; }
        internal static DataReader FromNative(NativeObject value)
        { return NativeObject.Wrap<DataReader>(value, StorageAbi.Reader, v => new DataReader(v)); }
        public DataReader(IInputStream inputStream)
        {
            using (var factory = NativeObject.Factory("Windows.Storage.Streams.DataReader", StorageAbi.ReaderFactory))
            using (var argument = NativeObject.Argument(inputStream, StorageAbi.Input))
            using (var access = new NativeObject.Access(factory))
            {
                IntPtr result;
                int hr = access.Method<StorageCalls.ObjectCall>(StorageAbi.ReaderFactory_CreateDataReader)(access.Self,
                    argument == null ? IntPtr.Zero : argument.DangerousGetHandle(), out result);
                native = NativeObject.TakeResult(hr, result);
            }
            NativeObject.Remember(this, native);
        }
        public static DataReader FromBuffer(IBuffer buffer)
        {
            using (var factory = NativeObject.Factory("Windows.Storage.Streams.DataReader", StorageAbi.ReaderStatics))
            using (var argument = NativeObject.Argument(buffer, StorageAbi.Buffer))
            using (var access = new NativeObject.Access(factory))
            {
                IntPtr result;
                int hr = access.Method<StorageCalls.ObjectCall>(StorageAbi.ReaderStatics_FromBuffer)(access.Self,
                    argument == null ? IntPtr.Zero : argument.DangerousGetHandle(), out result);
                return FromNative(NativeObject.TakeResult(hr, result));
            }
        }
        public uint UnconsumedBufferLength { get { return native.UInt32(StorageAbi.Reader_get_UnconsumedBufferLength); } }
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int ReadBytesCall(IntPtr self, uint count, IntPtr bytes);
        public void ReadBytes(byte[] value)
        {
            using (var bytes = new PinnedBytes(value))
            using (var access = new NativeObject.Access(native))
                Errors.Check(access.Method<ReadBytesCall>(StorageAbi.Reader_ReadBytes)(access.Self, bytes.Length, bytes.Pointer));
        }
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int Int16Call(IntPtr self, out short value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int UInt16Call(IntPtr self, out ushort value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int Int32Call(IntPtr self, out int value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int UInt32Call(IntPtr self, out uint value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int Int64Call(IntPtr self, out long value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int UInt64Call(IntPtr self, out ulong value);
        // Integers in the reader's ByteOrder, read by the shim.
        public short ReadInt16() { short v; using (var a = new NativeObject.Access(native)) Errors.Check(a.Method<Int16Call>(StorageAbi.Reader_ReadInt16)(a.Self, out v)); return v; }
        public ushort ReadUInt16() { ushort v; using (var a = new NativeObject.Access(native)) Errors.Check(a.Method<UInt16Call>(StorageAbi.Reader_ReadUInt16)(a.Self, out v)); return v; }
        public int ReadInt32() { int v; using (var a = new NativeObject.Access(native)) Errors.Check(a.Method<Int32Call>(StorageAbi.Reader_ReadInt32)(a.Self, out v)); return v; }
        public uint ReadUInt32() { uint v; using (var a = new NativeObject.Access(native)) Errors.Check(a.Method<UInt32Call>(StorageAbi.Reader_ReadUInt32)(a.Self, out v)); return v; }
        public long ReadInt64() { long v; using (var a = new NativeObject.Access(native)) Errors.Check(a.Method<Int64Call>(StorageAbi.Reader_ReadInt64)(a.Self, out v)); return v; }
        public ulong ReadUInt64() { ulong v; using (var a = new NativeObject.Access(native)) Errors.Check(a.Method<UInt64Call>(StorageAbi.Reader_ReadUInt64)(a.Self, out v)); return v; }
        public string ReadString(uint codeUnitCount)
        {
            using (var access = new NativeObject.Access(native))
            {
                IntPtr result;
                int hr = access.Method<StorageCalls.UIntCall>(StorageAbi.Reader_ReadString)(access.Self, codeUnitCount, out result);
                return NativeString.TakeResult(hr, result);
            }
        }
        public DataReaderLoadOperation LoadAsync(uint count)
        {
            using (var access = new NativeObject.Access(native))
            {
                IntPtr result;
                int hr = access.Method<StorageCalls.UIntCall>(StorageAbi.Reader_LoadAsync)(access.Self, count, out result);
                return DataReaderLoadOperation.FromNative(NativeObject.TakeResult(hr, result));
            }
        }
        public IInputStream DetachStream() { return InputStream.FromNative(native.Object(StorageAbi.Reader_DetachStream)); }
        public void Dispose() { using (var close = native.QueryInterface(StorageAbi.Closable)) close.Call(StorageAbi.Closable_Close); }
    }
    public sealed class DataReaderLoadOperation : IAsyncOperation<uint>, INativeWrapper
    {
        private readonly NativeFuture<uint> operation;
        NativeObject INativeWrapper.Native { get { return ((INativeWrapper)operation).Native; } }
        private DataReaderLoadOperation(NativeObject value)
        { operation = new NativeFuture<uint>(value, StorageAbi.LoadFuture, v => v.UInt32(StorageAbi.LoadFuture.Results), FromNative); }
        internal static DataReaderLoadOperation FromNative(NativeObject value)
        { return NativeObject.Wrap<DataReaderLoadOperation>(value, StorageAbi.LoadFuture.Iid, v => new DataReaderLoadOperation(v)); }
        public uint Id { get { return operation.Id; } }
        public AsyncStatus Status { get { return operation.Status; } }
        public Exception ErrorCode { get { return operation.ErrorCode; } }
        public AsyncOperationCompletedHandler<uint> Completed { get { return operation.Completed; } set { operation.Completed = value; } }
        public uint GetResults() { return operation.GetResults(); }
        public void Cancel() { operation.Cancel(); }
        public void Close() { operation.Close(); }
    }
    internal class InputStream : IInputStream, INativeWrapper
    {
        protected readonly NativeObject native;
        internal InputStream(NativeObject value) { native = value; }
        NativeObject INativeWrapper.Native { get { return native; } }
        internal static IInputStream FromNative(NativeObject value) { return NativeObject.Wrap<IInputStream>(value, StorageAbi.Input, v => new InputStream(v)); }
        public IAsyncOperationWithProgress<IBuffer, uint> ReadAsync(IBuffer buffer, uint count, InputStreamOptions options)
        {
            if (buffer == null) throw new ArgumentNullException("buffer");
            using (var input = native.QueryInterface(StorageAbi.Input))
            using (var access = new NativeObject.Access(input))
            using (var argument = NativeObject.Argument(buffer, StorageAbi.Buffer))
            {
                IntPtr result; int hr = access.Method<StorageCalls.ReadCall>(StorageAbi.Input_ReadAsync)(access.Self, argument.DangerousGetHandle(), count, (int)options, out result);
                return NativeRead.FromNative(NativeObject.TakeResult(hr, result));
            }
        }
        public void Dispose() { using (var close = native.QueryInterface(StorageAbi.Closable)) close.Call(StorageAbi.Closable_Close); }
    }
    internal sealed class RandomStream : InputStream, IRandomAccessStreamWithContentType
    {
        internal RandomStream(NativeObject value) : base(value) { }
        internal static IRandomAccessStream FromRandom(NativeObject value)
        { return NativeObject.Wrap<IRandomAccessStream>(value, StorageAbi.Random, v => new RandomStream(v)); }
        internal static IRandomAccessStreamWithContentType FromContent(NativeObject value)
        { return NativeObject.Wrap<IRandomAccessStreamWithContentType>(value, StorageAbi.Random, v => new RandomStream(v)); }
        private NativeObject Random() { return native.QueryInterface(StorageAbi.Random); }
        public ulong Size { get { using (var value = Random()) return StorageCalls.Wide(value, StorageAbi.Random_get_Size); } set { using (var target = Random()) StorageCalls.SetWide(target, StorageAbi.Random_put_Size, value); } }
        public ulong Position { get { using (var value = Random()) return StorageCalls.Wide(value, StorageAbi.Random_get_Position); } }
        public bool CanRead { get { using (var value = Random()) return StorageCalls.Boolean(value, StorageAbi.Random_get_CanRead); } }
        public bool CanWrite { get { using (var value = Random()) return StorageCalls.Boolean(value, StorageAbi.Random_get_CanWrite); } }
        public string ContentType { get { using (var value = native.QueryInterface(StorageAbi.Content)) return value.String(StorageAbi.Content_get_ContentType); } }
        public void Seek(ulong position) { using (var value = Random()) StorageCalls.SetWide(value, StorageAbi.Random_Seek, position); }
        public IInputStream GetInputStreamAt(ulong position) { using (var value = Random()) return FromNative(StorageCalls.WideObject(value, StorageAbi.Random_GetInputStreamAt, position)); }
        public IOutputStream GetOutputStreamAt(ulong position) { throw new NotImplementedException("Windows.Storage.Streams.IRandomAccessStream.GetOutputStreamAt"); }
        public IRandomAccessStream CloneStream() { using (var value = Random()) return FromContent(value.Object(StorageAbi.Random_CloneStream)); }
        public IAsyncOperationWithProgress<uint, uint> WriteAsync(IBuffer buffer) { throw new NotImplementedException("Windows.Storage.Streams.IOutputStream.WriteAsync"); }
        public IAsyncOperation<bool> FlushAsync() { throw new NotImplementedException("Windows.Storage.Streams.IOutputStream.FlushAsync"); }
    }
}
namespace AppxPortable.WinRT
{
    internal sealed partial class NativeObject
    {
        internal void Remember(object wrapper) { IntPtr identity = Identity(); lock (gate) wrappers[identity] = new WeakReference(wrapper); }
    }
}

namespace AppxPortable.WinRT
{
    // WinRT async results computed in managed code. Completed runs once, when the
    // operation finishes, or at once when assigned after it has finished.
    internal abstract class ManagedAsync
    {
        private static int nextId;
        protected readonly object gate = new object();
        private readonly uint id = (uint)global::System.Threading.Interlocked.Increment(ref nextId);
        protected AsyncStatus status = AsyncStatus.Started;
        protected Exception error;
        public uint Id { get { return id; } }
        public AsyncStatus Status { get { lock (gate) return status; } }
        public Exception ErrorCode { get { lock (gate) return error; } }
        public void Cancel() { Finish(AsyncStatus.Canceled, null); }
        public void Close() { }
        internal void Fail(Exception failure) { Finish(AsyncStatus.Error, failure); }
        protected void Finish(AsyncStatus final, Exception failure)
        {
            lock (gate)
            {
                if (status != AsyncStatus.Started) return;
                status = final; error = failure;
            }
            Notify();
        }
        protected abstract void Notify();
        protected void Results()
        {
            if (status == AsyncStatus.Error) throw error;
            if (status == AsyncStatus.Canceled) throw new OperationCanceledException();
            if (status != AsyncStatus.Completed) throw new InvalidOperationException("The operation has not completed");
        }
        // WinRT's E_ILLEGAL_DELEGATE_ASSIGNMENT for a second Completed handler.
        protected static Exception Reassigned() { return new InvalidOperationException("Completed is already set"); }
    }
    internal sealed class ManagedOperation<T> : ManagedAsync, IAsyncOperation<T>
    {
        private T result;
        private AsyncOperationCompletedHandler<T> completed;
        private bool notified;
        internal void Complete(T value)
        {
            lock (gate) { if (status != AsyncStatus.Started) return; result = value; status = AsyncStatus.Completed; }
            Notify();
        }
        public AsyncOperationCompletedHandler<T> Completed
        {
            get { lock (gate) return completed; }
            set { lock (gate) { if (completed != null) throw Reassigned(); completed = value; } Notify(); }
        }
        public T GetResults() { lock (gate) { Results(); return result; } }
        protected override void Notify()
        {
            AsyncOperationCompletedHandler<T> handler; AsyncStatus final;
            lock (gate)
            {
                if (notified || completed == null || status == AsyncStatus.Started) return;
                notified = true; handler = completed; final = status;
            }
            handler(this, final);
        }
    }
    internal sealed class ManagedAction : ManagedAsync, IAsyncAction
    {
        private AsyncActionCompletedHandler completed;
        private bool notified;
        // Runs the work now; a failure becomes the action's error, as awaiting it rethrows.
        internal static ManagedAction Run(Action work)
        {
            var action = new ManagedAction();
            try { work(); action.Finish(AsyncStatus.Completed, null); }
            catch (Exception failure) { action.Fail(failure); }
            return action;
        }
        public AsyncActionCompletedHandler Completed
        {
            get { lock (gate) return completed; }
            set { lock (gate) { if (completed != null) throw Reassigned(); completed = value; } Notify(); }
        }
        public void GetResults() { lock (gate) Results(); }
        protected override void Notify()
        {
            AsyncActionCompletedHandler handler; AsyncStatus final;
            lock (gate)
            {
                if (notified || completed == null || status == AsyncStatus.Started) return;
                notified = true; handler = completed; final = status;
            }
            handler(this, final);
        }
    }
}
