using System;
using System.Runtime.InteropServices;

using Windows.ApplicationModel;
using Windows.System;
class GeneratedProjectionTest
{
    [DllImport("combase.dll")] static extern int RoInitialize(uint apartment);
    [DllImport("combase.dll")] static extern void RoUninitialize();
    static void Check(bool value, string name)
    {
        if (!value) throw new InvalidOperationException(name);
    }
    [MTAThread]
    static int Main()
    {
        Marshal.ThrowExceptionForHR(RoInitialize(1));
        try
        {
            Check(typeof(Package).Assembly.GetName().Version == new Version(255, 255, 255, 255), "Windows assembly identity");
            Check(Marshal.SizeOf(Enum.GetUnderlyingType(typeof(ProcessorArchitecture))) == 4, "enum width");
            Check((int)ProcessorArchitecture.Unknown == 65535, "metadata enum constant");
            for (int i = 0; i < 32; ++i)
            {
                Package package = Package.Current;
                PackageId id = package.Id;
                Check(id.Name == "GeneratedProjectionTest", "native package name");
                Check(id.Publisher == "CN=projection-đá-💎", "native Unicode publisher");
                Check(id.ResourceId == "", "native empty string");
                Check(id.Architecture == (IntPtr.Size == 4 ? ProcessorArchitecture.X86 : ProcessorArchitecture.X64), "native enum");
            }
            GC.Collect();
            GC.WaitForPendingFinalizers();
            Console.WriteLine("PASS: generated Windows projection bound and called real package ABI");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine("0x" + error.HResult.ToString("X8") + " " + error); return 1; }
        finally { RoUninitialize(); }
    }
}
