using System;
using System.Runtime.InteropServices;
using System.Threading;
using AppxPortable.WinRT;

// Compiled with the runtime sources so these tests exercise native ownership and
// callback ABIs without adding public test hooks to the shipped projection.
class RuntimeTest
{
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate int Invoke(IntPtr self);
    private static readonly Invoke invoke = self => 0;
    private static readonly Guid iid = new Guid("7ddccbec-0916-4942-b505-069e435e5ff2");
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    sealed class Wrapper
    {
        internal NativeObject Native;
        internal Wrapper(NativeObject value) { Native = value; }
    }
    static void Inspectable()
    {
        using (var held = ManagedObject.Hold(new object(), new NativeInterface(iid, false, invoke)))
        {
            var inspectable = new Guid("af86e2e0-b12d-4c6a-9c5a-d7aa65101e90");
            IntPtr pointer;
            int hr = Marshal.QueryInterface(held.Pointer, ref inspectable, out pointer);
            if (pointer != IntPtr.Zero) Marshal.Release(pointer);
            Check(hr == unchecked((int)0x80004002) && pointer == IntPtr.Zero, "delegate incorrectly exposes IInspectable");
        }
    }
    static void Identity()
    {
        using (var held = ManagedObject.Hold(new object(), new NativeInterface(iid, true)))
        using (var barrier = new Barrier(2))
        {
            var values = new Wrapper[2];
            var owned = new NativeObject[2];
            var errors = new Exception[2];
            var threads = new Thread[2];
            for (int i = 0; i < 2; ++i)
            {
                int index = i;
                threads[i] = new Thread(() => {
                    try { values[index] = NativeObject.Wrap(NativeObject.Borrow(held.Pointer), iid, native => {
                        owned[index] = native;
                        Check(barrier.SignalAndWait(5000), "wrapper creation barrier");
                        return new Wrapper(native);
                    }); }
                    catch (Exception error) { errors[index] = error; }
                });
                threads[i].Start();
            }
            foreach (Thread thread in threads) Check(thread.Join(10000), "wrapper thread timed out");
            foreach (Exception error in errors) if (error != null) throw error;
            Check(ReferenceEquals(values[0], values[1]), "same native identity produced two wrappers");
            Check(owned[0].IsClosed != owned[1].IsClosed, "losing reference was not released");
            values[0].Native.Dispose();
        }
    }
    static void Stale()
    {
        int hr = Errors.Report(new InvalidOperationException("stale callback marker"));
        try { Errors.Check(hr); }
        catch (Exception error) { Check(error.Message != "stale callback marker", "unrelated call consumed a stale callback exception"); return; }
        throw new Exception("failed HRESULT did not throw");
    }
    static void Invocation()
    {
        // Two callers with identical HRESULTs, both reported from callback threads
        // before either caller checks its result. Neither may steal the other error.
        using (var barrier = new Barrier(2))
        {
            var errors = new Exception[2];
            var threads = new Thread[2];
            for (int i = 0; i < 2; ++i)
            {
                int index = i;
                threads[i] = new Thread(() => {
                    try
                    {
                        using (Errors.Begin())
                        using (var held = ManagedObject.Hold(new object(), new NativeInterface(iid, false, invoke)))
                        {
                            int hr = 0;
                            var callback = new Thread(() => {
                                hr = ManagedObject.Call(held.Pointer, target => { throw new InvalidOperationException("caller " + index); });
                            });
                            callback.Start();
                            Check(callback.Join(5000), "callback thread timed out");
                            Check(barrier.SignalAndWait(5000), "exception barrier");
                            try { Errors.Check(hr); }
                            catch (InvalidOperationException error) { Check(error.Message == "caller " + index, "wrong invocation exception"); return; }
                            throw new Exception("callback failure was lost");
                        }
                    }
                    catch (Exception error) { errors[index] = error; }
                });
                threads[i].Start();
            }
            foreach (Thread thread in threads) Check(thread.Join(10000), "invocation thread timed out");
            foreach (Exception error in errors) if (error != null) throw error;
        }
        using (Errors.Begin())
        {
            int outer = Errors.Report(new InvalidOperationException("outer marker"));
            using (Errors.Begin())
            {
                int inner = Errors.Report(new InvalidOperationException("inner marker"));
                try { Errors.Check(inner); }
                catch (InvalidOperationException error) { Check(error.Message == "inner marker", "nested inner"); }
            }
            try { Errors.Check(outer); }
            catch (InvalidOperationException error) { Check(error.Message == "outer marker", "nested outer"); }
        }
        using (Errors.Begin()) { Errors.Report(new InvalidOperationException("discarded marker")); }
        Stale();
    }
    static void Reentrant()
    {
        using (var ready = new ManualResetEvent(false))
        using (var checkedError = new ManualResetEvent(false))
        {
            ManagedObject.Held held = null;
            int hr = 0;
            Exception failure = null;
            var owner = new Thread(() => {
                try
                {
                    using (Errors.Begin())
                    using (held = ManagedObject.Hold(new object(), new NativeInterface(iid, false, invoke)))
                    {
                        ready.Set();
                        Check(checkedError.WaitOne(5000), "reentrant callback timeout");
                        try { Errors.Check(hr); }
                        catch (InvalidOperationException error) { Check(error.Message == "bound invocation", "wrong reentrant error"); return; }
                        throw new Exception("bound invocation lost its callback exception");
                    }
                }
                catch (Exception error) { failure = error; ready.Set(); }
            });
            owner.Start();
            Check(ready.WaitOne(5000), "reentrant owner timeout");
            using (Errors.Begin())
            {
                try { hr = ManagedObject.Call(held.Pointer, target => { throw new InvalidOperationException("bound invocation"); }); }
                finally { checkedError.Set(); }
                Check(owner.Join(5000), "reentrant owner join timeout");
                if (failure != null) throw failure;
                // The pumping caller must not inherit the other invocation's error.
                try { Errors.Check(hr); }
                catch (Exception error) { Check(error.Message != "bound invocation", "pumping invocation stole error"); }
            }
        }
    }
    static int Main(string[] args)
    {
        try
        {
            if (args[0] == "inspectable") Inspectable();
            else if (args[0] == "identity") Identity();
            else if (args[0] == "stale") Stale();
            else if (args[0] == "invocation") Invocation();
            else if (args[0] == "reentrant") Reentrant();
            else throw new Exception("unknown case");
            Console.WriteLine("PASS: runtime " + args[0]);
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
