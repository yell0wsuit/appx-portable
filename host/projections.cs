// Generated component projections live apart from their same-named native DLLs.
using System;
using System.IO;
using System.Reflection;
internal static class ComponentProjections
{
    internal static void Install(string directory)
    {
        string root = Path.Combine(directory, "appx-projections");
        AppDomain.CurrentDomain.AssemblyResolve += (sender, args) =>
        {
            var requested = new AssemblyName(args.Name);
            string name = requested.Name;
            if (string.IsNullOrEmpty(name) || name.IndexOfAny(new[] { '/', '\\', ':' }) >= 0 || name == "." || name == "..") return null;
            string path = Path.Combine(root, name + ".dll");
            if (!File.Exists(path)) return null;
            var actual = AssemblyName.GetAssemblyName(path);
            if (!string.Equals(actual.Name, name, StringComparison.OrdinalIgnoreCase) || actual.Version != requested.Version ||
                !string.Equals(actual.CultureName ?? "", requested.CultureName ?? "", StringComparison.OrdinalIgnoreCase) ||
                Convert.ToBase64String(actual.GetPublicKeyToken() ?? new byte[0]) != Convert.ToBase64String(requested.GetPublicKeyToken() ?? new byte[0]))
            {
                string message = "Generated component projection identity does not match " + args.Name;
                // Mono replaces resolver exceptions with FileNotFoundException;
                // retain the reason in the host log before returning to the loader.
                Host.AppendLog(Path.Combine(directory, "managed.log"), "[projection] " + message + "\n");
                throw new FileLoadException(message, path);
            }
            return Assembly.LoadFrom(path);
        };
    }
}
