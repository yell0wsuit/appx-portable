using System;
using System.Runtime.InteropServices;
using AppxPortable.WinRT;

internal static class ManagedWinrtBridgeTest
{
    [DllImport("combase.dll")]
    private static extern int RoInitialize(uint apartment);
    [DllImport("combase.dll")]
    private static extern void RoUninitialize();

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private static void ExpectHResult(Action action, int expected, string message)
    {
        try { action(); }
        catch (Exception ex)
        {
            Check(ex.HResult == expected, message + ": " + ex);
            return;
        }
        throw new InvalidOperationException(message + ": no exception");
    }

    private static void CheckDisposed(PackageId id)
    {
        try { string unused = id.Name; }
        catch (ObjectDisposedException) { return; }
        throw new InvalidOperationException("use after dispose must throw");
    }

    [MTAThread]
    private static int Main()
    {
        Marshal.ThrowExceptionForHR(RoInitialize(1));
        try
        {
            const string publisher = "CN=bridge-đá-💎";
            using (Package package = Package.Current)
            using (PackageId id = package.Id)
            {
                Check(id.Name == "ManagedWinrtBridgeTest", "real shim package identity");
                Check(id.Publisher == publisher, "Unicode HSTRING result");
                Check(id.ResourceId == "", "empty HSTRING result");
                Check(id.Architecture == (IntPtr.Size == 4 ? ProcessorArchitecture.X86 : ProcessorArchitecture.X64),
                    "native 32-bit enum result");
                using (PackageId another = package.Id)
                {
                    id.Dispose();
                    id.Dispose();
                    CheckDisposed(id);
                    Check(another.Publisher == publisher, "independent native references");
                }
            }
            ExpectHResult(() => NativeObject.Factory("AppxPortable.Missing-💎", Guid.Empty),
                unchecked((int)0x80040111), "native missing factory HRESULT");
            ExpectHResult(() => NativeObject.Factory("Windows.ApplicationModel.Package", Guid.Empty),
                unchecked((int)0x80004002), "native unsupported interface HRESULT");
            for (int i = 0; i < 128; ++i)
            {
                using (Package package = Package.Current)
                using (PackageId id = package.Id)
                {
                    package.Dispose();
                    Check(id.Publisher == publisher, "child survives parent disposal");
                }
            }
            GC.Collect();
            GC.WaitForPendingFinalizers();
            using (Package package = Package.Current)
            using (PackageId id = package.Id)
                Check(id.Name == "ManagedWinrtBridgeTest", "activation after GC");
            Console.WriteLine("PASS: managed WinRT activation, Unicode, HRESULTs, and COM ownership");
            return 0;
        }
        catch (Exception ex)
        {
            Console.WriteLine("FAIL: " + ex);
            return 1;
        }
        finally { RoUninitialize(); }
    }
}
