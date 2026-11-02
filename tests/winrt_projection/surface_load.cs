using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;

// Loads every type of a whole-surface Windows.dll and its members, so a type the
// runtime cannot load fails here, not in the first game that touches it.
class SurfaceLoadTest
{
    [DllImport("combase.dll")] static extern int RoInitialize(uint apartment);
    static int Main(string[] args)
    {
        try
        {
#if UNIMPLEMENTED
            Marshal.ThrowExceptionForHR(RoInitialize(1));
            try { Windows.Security.Cryptography.CryptographicBuffer.EncodeToBase64String(null); }
            catch (NotImplementedException error)
            {
                if (error.Message.IndexOf("Windows.Security.Cryptography.CryptographicBuffer.EncodeToBase64String", StringComparison.Ordinal) < 0) throw;
                Console.WriteLine("PASS: unimplemented surface members name themselves");
                return 0;
            }
            Console.Error.WriteLine("unimplemented surface member returned");
            return 1;
#else
            Assembly surface = Assembly.LoadFrom(args[0]);
            Type[] types;
            try { types = surface.GetExportedTypes(); }
            catch (ReflectionTypeLoadException error)
            {
                foreach (Exception inner in error.LoaderExceptions) Console.Error.WriteLine(inner.Message);
                return 1;
            }
            const BindingFlags all = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;
            var failures = new List<string>();
            foreach (Type type in types)
            {
                try { type.GetMethods(all); type.GetProperties(all); type.GetEvents(all); type.GetFields(all); type.GetConstructors(all); }
                catch (Exception error) { failures.Add(type.FullName + ": " + error.Message); }
            }
            foreach (string failure in failures) Console.Error.WriteLine(failure);
            var names = new HashSet<string>(types.Select(t => t.FullName));
            foreach (string required in new[] { "Windows.Security.Cryptography.CryptographicBuffer", "Windows.Devices.Geolocation.Geolocator" })
                if (!names.Contains(required)) { Console.Error.WriteLine("missing surface type " + required); return 1; }
            if (types.Length < 5000) { Console.Error.WriteLine("surface has only " + types.Length + " types"); return 1; }
            if (failures.Count != 0) return 1;
            Console.WriteLine("PASS: surface loads " + types.Length + " types");
            return 0;
#endif
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
