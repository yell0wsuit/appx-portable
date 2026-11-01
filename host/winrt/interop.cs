// Shared cross-assembly projection contract. No SDK or Cecil dependency.
using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
[assembly: AssemblyVersion("1.0.0.0")]
namespace AppxPortable.WinRT.Interop
{
    public interface INativeObject
    {
        // The caller owns the returned interface lease and must dispose it.
        SafeHandle AcquireInterface(Guid iid);
    }
    [AttributeUsage(AttributeTargets.Class | AttributeTargets.Enum, Inherited = false)]
    public sealed class ProjectionTypeAttribute : Attribute
    {
        public string InterfaceId { get; private set; }
        public ProjectionTypeAttribute(string interfaceId) { InterfaceId = interfaceId; }
    }
    [AttributeUsage(AttributeTargets.Assembly)]
    public sealed class ProjectionProviderAttribute : Attribute
    {
        public Type Provider { get; private set; }
        public ProjectionProviderAttribute(Type provider) { Provider = provider; }
    }
    public interface IProjectionProvider
    {
        // The lease is borrowed for this call only. The provider retains its own reference.
        object Wrap(Type type, SafeHandle borrowed);
    }
    public static class ProjectionInterop
    {
        private static readonly object gate = new object();
        private static readonly Dictionary<Assembly, IProjectionProvider> providers = new Dictionary<Assembly, IProjectionProvider>();
        // Consumes the owned lease on success, null, and every failure path.
        public static T Wrap<T>(SafeHandle owned) where T : class
        {
            if (owned == null) return null;
            using (owned)
            {
                IProjectionProvider provider;
                lock (gate)
                {
                    if (!providers.TryGetValue(typeof(T).Assembly, out provider))
                    {
                        var attributes = typeof(T).Assembly.GetCustomAttributes(typeof(ProjectionProviderAttribute), false);
                        if (attributes.Length != 1) throw new NotSupportedException("No projection provider: " + typeof(T).FullName);
                        var type = ((ProjectionProviderAttribute)attributes[0]).Provider;
                        if (type.Assembly != typeof(T).Assembly || !typeof(IProjectionProvider).IsAssignableFrom(type))
                            throw new NotSupportedException("Invalid projection provider: " + typeof(T).FullName);
                        provider = (IProjectionProvider)Activator.CreateInstance(type);
                        providers.Add(typeof(T).Assembly, provider);
                    }
                }
                return (T)provider.Wrap(typeof(T), owned);
            }
        }
    }
}
