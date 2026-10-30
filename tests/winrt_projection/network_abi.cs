using System;
using System.Runtime.InteropServices;

using AppxPortable.WinRT;

using Windows.Networking.Connectivity;
class NetworkAbiTest
{
    [DllImport("combase.dll")] static extern int RoInitialize(uint apartment);
    [DllImport("network_projection_fixture.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int InvokeNetworkChanged(IntPtr handler, IntPtr sender);
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    static int Main()
    {
        try
        {
            Marshal.ThrowExceptionForHR(RoInitialize(1));
            int calls = 0; object marker = new object();
            NetworkStatusChangedEventHandler handler = sender => { Check(sender == null || ReferenceEquals(sender, marker), "native sender identity"); ++calls; };
            using (var held = ManagedObject.Hold(handler, AppxPortable.WinRT.Projected.Windows_Networking_Connectivity_NetworkStatusChangedEventHandler.Interface))
            using (var sender = ManagedObject.Hold(marker, new NativeInterface(new Guid("d28bcc59-71ce-43d0-bb42-2af9b95dc060"), true)))
            {
                Errors.Check(InvokeNetworkChanged(held.Pointer, IntPtr.Zero));
                Errors.Check(InvokeNetworkChanged(held.Pointer, sender.Pointer));
            }
            Check(calls == 2, "native delegate invocation count");
            var failure = new InvalidOperationException("network callback failure");
            NetworkStatusChangedEventHandler failing = sender => { throw failure; };
            using (var scope = Errors.Begin())
            using (var held = ManagedObject.Hold(failing, AppxPortable.WinRT.Projected.Windows_Networking_Connectivity_NetworkStatusChangedEventHandler.Interface))
            {
                try { Errors.Check(InvokeNetworkChanged(held.Pointer, IntPtr.Zero)); throw new Exception("callback error accepted"); }
                catch (InvalidOperationException error) { Check(ReferenceEquals(error, failure), "callback exception identity"); }
            }
            Console.WriteLine("PASS: native network delegate ABI preserves sender and callback failures"); return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
