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
    // Redirected WinRT structs whose CLR assemblies appx-portable does not ship yet.
    static readonly string[] notYetShipped = { "System.Runtime.WindowsRuntime", "System.Runtime.WindowsRuntime.UI.Xaml" };
    static string MissingAssembly(Exception error)
    {
        foreach (string name in notYetShipped.OrderByDescending(n => n.Length))
            if (error.Message.IndexOf("'" + name + ",", StringComparison.Ordinal) >= 0 || error.Message.IndexOf("'" + name + "'", StringComparison.Ordinal) >= 0 ||
                (error is System.IO.FileNotFoundException && ((System.IO.FileNotFoundException)error).FileName != null &&
                 ((System.IO.FileNotFoundException)error).FileName.StartsWith(name + ",", StringComparison.Ordinal)))
                return name;
        return null;
    }
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
            // Mono resolves signature types lazily: touch every one.
            var deferred = new Dictionary<string, int>();
            foreach (Type type in types)
            {
                try
                {
                    var signatureTypes = new List<Func<Type>>();
                    foreach (MethodInfo method in type.GetMethods(all))
                    {
                        MethodInfo current = method;
                        signatureTypes.Add(() => current.ReturnType);
                        foreach (ParameterInfo parameter in current.GetParameters()) { ParameterInfo p = parameter; signatureTypes.Add(() => p.ParameterType); }
                    }
                    foreach (FieldInfo field in type.GetFields(all)) { FieldInfo f = field; signatureTypes.Add(() => f.FieldType); }
                    foreach (ConstructorInfo constructor in type.GetConstructors(all))
                        foreach (ParameterInfo parameter in constructor.GetParameters()) { ParameterInfo p = parameter; signatureTypes.Add(() => p.ParameterType); }
                    type.GetProperties(all); type.GetEvents(all);
                    foreach (Func<Type> resolve in signatureTypes)
                    {
                        try { resolve(); }
                        catch (Exception error)
                        {
                            string missing = MissingAssembly(error);
                            if (missing == null) { failures.Add(type.FullName + ": " + error.Message); continue; }
                            int count; deferred.TryGetValue(missing, out count); deferred[missing] = count + 1;
                        }
                    }
                }
                catch (Exception error)
                {
                    // Mono resolves a type's signatures when listing its members.
                    string missing = MissingAssembly(error);
                    if (missing == null) { failures.Add(type.FullName + ": " + error.Message); continue; }
                    int count; deferred.TryGetValue(missing, out count); deferred[missing] = count + 1;
                }
            }
            // A CLR-redirected type is defined by its own assembly, never copied into Windows.dll.
            foreach (string redirected in new[] { "Windows.UI.Xaml.Media.Matrix", "Windows.UI.Xaml.Media.Media3D.Matrix3D", "Windows.UI.Xaml.Media.Animation.KeyTime",
                "Windows.Foundation.Numerics.Vector3", "Windows.Foundation.Point", "Windows.UI.Xaml.Thickness" })
                if (surface.GetType(redirected) != null) failures.Add("CLR-redirected type copied into the surface: " + redirected);
            foreach (string failure in failures) Console.Error.WriteLine(failure);
            foreach (KeyValuePair<string, int> pair in deferred) Console.WriteLine("deferred: " + pair.Value + " signature types in " + pair.Key);
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
