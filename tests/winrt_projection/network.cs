using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;

using Windows.Networking.Connectivity;
class NetworkProjectionTest
{
    [DllImport("combase.dll")] static extern int RoInitialize(uint apartment);
    static int callbacks;
    sealed class Target { internal void Changed(object sender) { ++callbacks; } }
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    static void Collect() { for (int i = 0; i < 3; ++i) { GC.Collect(); GC.WaitForPendingFinalizers(); } }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static WeakReference Subscribe(out EventRegistrationToken token)
    {
        var target = new Target();
        token = NetworkInformation.add_NetworkStatusChanged(target.Changed);
        return new WeakReference(target);
    }
    static void Run(bool stub)
    {
        var profile = NetworkInformation.GetInternetConnectionProfile();
        Check((profile != null) == stub, "native offline/stub-IAP contract");
        if (profile != null)
        {
            Check(profile.ProfileName == "Offline stub IAP", "native profile name");
            Check(profile.GetNetworkConnectivityLevel() == NetworkConnectivityLevel.InternetAccess, "native connectivity enum result");
        }
        EventRegistrationToken token;
        WeakReference target = Subscribe(out token);
        Collect(); Check(target.IsAlive, "native event did not retain the subscriber");
        NetworkInformation.remove_NetworkStatusChanged(token);
        Collect(); Check(!target.IsAlive, "native event retained the removed subscriber");
        NetworkStatusChangedEventHandler managed = sender => ++callbacks;
        WindowsRuntimeMarshal.AddEventHandler<NetworkStatusChangedEventHandler>(NetworkInformation.add_NetworkStatusChanged, NetworkInformation.remove_NetworkStatusChanged, managed);
        WindowsRuntimeMarshal.RemoveEventHandler<NetworkStatusChangedEventHandler>(NetworkInformation.remove_NetworkStatusChanged, managed);
        Check(callbacks == 0, "constant native connectivity state emitted a spurious event");
        Console.WriteLine("PASS: managed network projection preserves native connectivity and event lifetime (" + (stub ? "stub" : "offline") + ")");
    }
    [MTAThread]
    static int Main(string[] args)
    {
        try { Marshal.ThrowExceptionForHR(RoInitialize(1)); Run(args.Length != 0 && args[0] == "stub"); return 0; }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
