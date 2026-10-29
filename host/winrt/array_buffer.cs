// A byte-array view with native IBuffer/IBufferByteAccess. The separately rooted
// data object owns the pin; it never references its projected wrapper.
using System;
using System.Runtime.InteropServices;

using Windows.Storage.Streams;
namespace AppxPortable.WinRT
{
    internal sealed class ArrayBuffer : IBuffer, INativeWrapper
    {
        private sealed class Data : IManagedProjection
        {
            internal readonly byte[] Bytes;
            private GCHandle pin;
            internal uint Length;
            private WeakReference projected;
            internal void Remember(ArrayBuffer value) { lock (this) projected = new WeakReference(value); }
            public object Resolve(NativeObject value)
            {
                lock (this)
                {
                    var wrapper = projected == null ? null : projected.Target as ArrayBuffer;
                    if (wrapper != null) return wrapper;
                    wrapper = new ArrayBuffer(value.QueryInterface(StorageAbi.Buffer));
                    projected = new WeakReference(wrapper);
                    return wrapper;
                }
            }
            internal Data(byte[] bytes) { Bytes = bytes; Length = (uint)bytes.Length; pin = GCHandle.Alloc(bytes, GCHandleType.Pinned); }
            internal IntPtr Pointer { get { return pin.AddrOfPinnedObject(); } }
            ~Data() { if (pin.IsAllocated) pin.Free(); }
        }
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int GetUInt(IntPtr self, out uint value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int SetUInt(IntPtr self, uint value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int GetBytes(IntPtr self, out IntPtr value);
        private static int GetCapacity(IntPtr self, out uint value)
        { uint result = 0; int hr = ManagedObject.Call(self, target => result = (uint)((Data)target).Bytes.Length); value = result; return hr; }
        private static int LengthOf(IntPtr self, out uint value)
        { uint result = 0; int hr = ManagedObject.Call(self, target => { var data = (Data)target; lock (data) result = data.Length; }); value = result; return hr; }
        private static int SetLength(IntPtr self, uint value)
        {
            return ManagedObject.Call(self, target => { var data = (Data)target; if (value > data.Bytes.Length) throw new ArgumentException("Buffer length exceeds capacity"); lock (data) data.Length = value; });
        }
        private static int Pointer(IntPtr self, out IntPtr value)
        { IntPtr result = IntPtr.Zero; int hr = ManagedObject.Call(self, target => result = ((Data)target).Pointer); value = result; return hr; }
        // Buffer slots are qualified in storage_generation.cs, byte access is
        // the classic COM interface declared by robuffer.h (method slot 3).
        private static readonly NativeInterface buffer = new NativeInterface(StorageAbi.Buffer, true, (GetUInt)GetCapacity, (GetUInt)LengthOf, (SetUInt)SetLength);
        private static readonly NativeInterface bytes = new NativeInterface(new Guid("905a0fef-bc53-11df-8c49-001e4fc686da"), false, (GetBytes)Pointer);
        private readonly NativeObject native;
        private ArrayBuffer(NativeObject value) { native = value; native.Remember(this); }
        NativeObject INativeWrapper.Native { get { return native; } }
        internal static IBuffer Create(byte[] source)
        {
            var data = new Data(source);
            using (var held = ManagedObject.Hold(data, buffer, bytes))
            {
                var wrapper = new ArrayBuffer(NativeObject.Borrow(held.Pointer));
                data.Remember(wrapper);
                return wrapper;
            }
        }
        public uint Capacity { get { return native.UInt32(StorageAbi.Buffer_get_Capacity); } }
        public uint Length
        {
            get { return native.UInt32(StorageAbi.Buffer_get_Length); }
            set { using (var access = new NativeObject.Access(native)) Errors.Check(access.Method<StorageCalls.SetUIntCall>(StorageAbi.Buffer_put_Length)(access.Self, value)); }
        }
    }
}
