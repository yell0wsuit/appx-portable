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
    internal sealed class NativeFuture<T> : NativeAsync, IAsyncOperation<T> where T : class
    {
        private readonly Func<NativeObject, T> convert;
        private NativeFuture(NativeObject value, FutureAbi abi, Func<NativeObject, T> convert) : base(value, abi) { this.convert = convert; }
        internal static IAsyncOperation<T> FromNative(NativeObject value, FutureAbi abi, Func<NativeObject, T> convert)
        {
            return NativeObject.Wrap<IAsyncOperation<T>>(value, abi.Iid, v => new NativeFuture<T>(v, abi, convert));
        }
        public T GetResults() { return convert(native.Object(abi.Results)); }
        public AsyncOperationCompletedHandler<T> Completed
        {
            get { return Get<AsyncOperationCompletedHandler<T>>(abi.GetCompleted); }
            set
            {
                var layout = abi; var factory = convert;
                Set(abi.PutCompleted, value == null ? null : new Callback
                {
                    Original = value,
                    // Reconstruct from the native callback argument. Capturing
                    // this wrapper would create a native/CCW reference cycle.
                    Completed = (pointer, status) => value(FromNative(NativeObject.Borrow(pointer), layout, factory), (AsyncStatus)status)
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
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] internal delegate int TextCall(IntPtr self, IntPtr text, out IntPtr value);
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
    public sealed class StorageFolder : INativeWrapper
    {
        private readonly NativeObject native;
        private StorageFolder(NativeObject value) { native = value; }
        NativeObject INativeWrapper.Native { get { return native; } }
        internal static StorageFolder FromNative(NativeObject value) { return NativeObject.Wrap<StorageFolder>(value, StorageAbi.Folder, v => new StorageFolder(v)); }
        public string Path { get { using (var item = native.QueryInterface(StorageAbi.Item)) return item.String(StorageAbi.Item_get_Path); } }
        public string Name { get { using (var item = native.QueryInterface(StorageAbi.Item)) return item.String(StorageAbi.Item_get_Name); } }
        public IAsyncOperation<StorageFile> GetFileAsync(string name) { return NativeFuture<StorageFile>.FromNative(StorageCalls.Text(native, StorageAbi.Folder_GetFileAsync, name), StorageAbi.FileFuture, StorageFile.FromNative); }
    }
    public sealed class StorageFile : INativeWrapper
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
        public IAsyncOperation<IRandomAccessStreamWithContentType> OpenReadAsync()
        {
            using (var reference = native.QueryInterface(StorageAbi.FileReference))
                return NativeFuture<IRandomAccessStreamWithContentType>.FromNative(reference.Object(StorageAbi.FileReference_OpenReadAsync), StorageAbi.StreamFuture, RandomStream.FromContent);
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
        private RandomStream(NativeObject value) : base(value) { }
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
