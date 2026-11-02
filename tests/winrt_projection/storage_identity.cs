using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;

using AppxPortable.WinRT;

using Windows.Storage.Streams;
class StorageIdentityTest
{
    [DllImport("combase.dll")] static extern int RoInitialize(uint apartment);
    static readonly Guid inspectable = new Guid("af86e2e0-b12d-4c6a-9c5a-d7aa65101e90");
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static NativeObject NativeOnly(out WeakReference wrapper, out WeakReference array)
    {
        byte[] bytes = new byte[] { 7, 8, 9 };
        IBuffer value = ArrayBuffer.Create(bytes);
        wrapper = new WeakReference(value); array = new WeakReference(bytes);
        return NativeObject.Of(value).QueryInterface(inspectable);
    }
    sealed class PendingLoad : IDisposable
    {
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int PutCall(IntPtr self, IntPtr handler);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int GetCall(IntPtr self, out IntPtr handler);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int ResultCall(IntPtr self, out uint result);
        static readonly PutCall put = (self, callback) => ManagedObject.Call(self, target => ((PendingLoad)target).handler = NativeObject.Borrow(callback));
        static readonly GetCall get = delegate(IntPtr self, out IntPtr callback) { callback = IntPtr.Zero; return 0; };
        static readonly ResultCall result = delegate(IntPtr self, out uint value) { value = 7; return 0; };
        internal static readonly NativeInterface Contract = new NativeInterface(StorageAbi.LoadFuture.Iid, true, put, get, result);
        NativeObject handler;
        internal object Sender;
        internal void Complete(IntPtr operation)
        {
            using (var access = new NativeObject.Access(handler))
                Marshal.ThrowExceptionForHR(access.Method<NativeAsync.CompletedCall>(3)(access.Self, operation, (int)Windows.Foundation.AsyncStatus.Completed));
        }
        public void Dispose() { Sender = null; if (handler != null) handler.Dispose(); }
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static WeakReference RegisterLoad(IntPtr pointer, PendingLoad pending)
    {
        var load = DataReaderLoadOperation.FromNative(NativeObject.Borrow(pointer));
        load.Completed = (sender, status) =>
        {
            Check(status == Windows.Foundation.AsyncStatus.Completed && sender.GetResults() == 7, "delayed load result");
            pending.Sender = sender;
        };
        return new WeakReference(load);
    }
    static void LoadIdentity()
    {
        using (var pending = new PendingLoad())
        using (var held = ManagedObject.Hold(pending, PendingLoad.Contract))
        {
            WeakReference wrapper = RegisterLoad(held.Pointer, pending);
            for (int i = 0; i < 3; ++i) { GC.Collect(); GC.WaitForPendingFinalizers(); }
            Check(!wrapper.IsAlive, "load callback retained the original wrapper");
            pending.Complete(held.Pointer);
            Check(pending.Sender is DataReaderLoadOperation, "delayed callback lost the public load-operation type");
        }
    }
    static int Main()
    {
        try
        {
            Marshal.ThrowExceptionForHR(RoInitialize(1));
            IBuffer original = ArrayBuffer.Create(new byte[] { 1, 2 });
            object result = NativeObject.Inspectable(NativeObject.Of(original).QueryInterface(inspectable));
            Check(ReferenceEquals(original, result), "object round trip exposed buffer backing state");
            WeakReference wrapper, array;
            using (NativeObject held = NativeOnly(out wrapper, out array))
            {
                for (int i = 0; i < 3; ++i) { GC.Collect(); GC.WaitForPendingFinalizers(); }
                Check(!wrapper.IsAlive && array.IsAlive, "native reference lifetime/pin ownership");
                object recreated = NativeObject.Inspectable(held.QueryInterface(inspectable));
                Check(recreated is IBuffer && ((IBuffer)recreated).Length == 3, "native reference did not recreate projected buffer");
                byte[] actual = new byte[3]; StorageBuffer.Copy((IBuffer)recreated, actual, 0, 3, false);
                Check(actual[0] == 7 && actual[2] == 9, "pinned native bytes after wrapper collection");
            }
            LoadIdentity();
            Console.WriteLine("PASS: buffer projection preserves object identity and native-owned pin lifetime"); return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
