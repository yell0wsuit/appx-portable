using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

// Run through appx-host, so this checks the selected apartment and native mode.
static class ManagedHostTest {
    [DllImport("combase.dll", CharSet = CharSet.Unicode)]
    static extern int WindowsCreateString(string text, int length, out IntPtr value);
    [DllImport("combase.dll")]
    static extern int WindowsDeleteString(IntPtr value);
    [DllImport("combase.dll")]
    static extern int RoGetActivationFactory(IntPtr name, ref Guid iid, out IntPtr factory);
    [DllImport("appxportable.dll", CallingConvention = CallingConvention.StdCall)]
    static extern int ShimGetFactory(IntPtr name, out IntPtr factory);
#if COREWINDOW
    [MTAThread]
#else
    [STAThread]
#endif
    static int Main(string[] args) {
#if COREWINDOW
        bool coreWindow = true;
#else
        bool coreWindow = false;
#endif
        if (args.Length != 1 || args[0] != "fixture-argument") return 1;
        if (Thread.CurrentThread.GetApartmentState() != (coreWindow ? ApartmentState.MTA : ApartmentState.STA)) return 2;
        if ((SynchronizationContext.Current == null) != coreWindow) return 3;
        // An exception observer must survive a log locked by another reader,
        // and simultaneous exceptions from game/network worker threads.
        using (var locked = new FileStream("managed.log", FileMode.Open, FileAccess.Read, FileShare.Read)) {
            try { throw new InvalidOperationException("locked log fixture"); }
            catch (InvalidOperationException) { }
        }
        var workers = new Thread[6];
        for (int i = 0; i < workers.Length; ++i) {
            workers[i] = new Thread(() => {
                for (int j = 0; j < 50; ++j) {
                    try { throw new InvalidOperationException("concurrent log fixture"); }
                    catch (InvalidOperationException) { }
                }
            });
            workers[i].Start();
        }
        foreach (var worker in workers) worker.Join();
        const string name = "Windows.ApplicationModel.Core.CoreApplication";
        IntPtr text, factory;
        Marshal.ThrowExceptionForHR(WindowsCreateString(name, name.Length, out text));
        try {
            int hr = ShimGetFactory(text, out factory);
            try {
                if (hr != 0) return 4;
                // CLR-origin activation must reach the same shim factory.
                var iid = new Guid("00000035-0000-0000-c000-000000000046");
                IntPtr activated;
                Marshal.ThrowExceptionForHR(RoGetActivationFactory(text, ref iid, out activated));
                try { if (activated != factory) return 5; }
                finally { Marshal.Release(activated); }
            } finally { if (factory != IntPtr.Zero) Marshal.Release(factory); }
        } finally { WindowsDeleteString(text); }
        return 0;
    }
}
