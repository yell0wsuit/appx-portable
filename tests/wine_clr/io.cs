using System;
using System.IO;
using System.Runtime.InteropServices;
class IOProbe {
 [DllImport("bcrypt.dll")] static extern int BCryptGenRandom(IntPtr provider, IntPtr bytes, int count, int flags);
 static int Main(string[] args) {
  Console.WriteLine("IO probe entered Main: " + IntPtr.Size);
  if (args.Length > 0 && args[0] == "native") {
   IntPtr data = Marshal.AllocHGlobal(16);
   int hr = BCryptGenRandom(IntPtr.Zero, data, 16, 2);
   Marshal.FreeHGlobal(data);
   Console.WriteLine("BCrypt result " + hr);
   return hr;
  }
  Console.WriteLine("Guid: " + Guid.NewGuid());
  File.WriteAllText("clr-io-result.txt", "io probe reached disk");
  Console.WriteLine(File.ReadAllText("clr-io-result.txt"));
  File.Delete("clr-io-result.txt");
  return 0;
 }
}
