using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Threading;

namespace AppxPortable.WinRT
{
    // Callback failures belong to one native invocation. Callback objects carry
    // its scope to the view thread; completed calls cannot contaminate later ones.
    internal static class Errors
    {
        private sealed class Failure
        {
            internal int HResult;
            internal ExceptionDispatchInfo Error;
        }
        [ThreadStatic] private static Scope current;
        internal static Scope Current { get { return current; } }

        internal sealed class Scope : IDisposable
        {
            private readonly object gate = new object();
            private readonly Scope previous;
            private Failure pending;
            private bool active = true;
            internal Scope() { previous = current; current = this; }
            internal void Report(int hr, Exception error)
            {
                lock (gate)
                    if (active && pending == null)
                        pending = new Failure { HResult = hr, Error = ExceptionDispatchInfo.Capture(error) };
            }
            internal void Check(int hr)
            {
                Failure failure;
                lock (gate) { failure = pending; pending = null; }
                if (hr < 0 && failure != null && failure.HResult == hr) failure.Error.Throw();
            }
            public void Dispose()
            {
                lock (gate) { active = false; pending = null; }
                current = previous;
            }
        }
        private sealed class CallbackScope : IDisposable
        {
            private readonly Scope previous;
            internal CallbackScope(Scope scope, bool bound) { previous = current; if (bound || current == null) current = scope; }
            public void Dispose() { current = previous; }
        }
        internal static Scope Begin() { return new Scope(); }
        internal static IDisposable Callback(Scope scope, bool bound) { return new CallbackScope(scope, bound); }
        internal static int Report(Exception error)
        {
            int hr = Marshal.GetHRForException(error);
            if (hr >= 0) hr = unchecked((int)0x80004005);
            if (current != null) current.Report(hr, error);
            return hr;
        }
        internal static void Check(int hr)
        {
            if (current != null) current.Check(hr);
            // Ignore unrelated thread-local COM IErrorInfo; managed callback
            // details are carried only by this invocation.
            if (hr < 0) Marshal.ThrowExceptionForHR(hr, new IntPtr(-1));
        }
    }

    // One native interface a managed object exposes: its IID and method thunks
    // after IUnknown (and IInspectable for WinRT interfaces; delegates are IUnknown).
    internal sealed class NativeInterface
    {
        internal readonly Guid Iid;
        internal readonly bool Inspectable;
        internal readonly IntPtr Vtable;
        private readonly Delegate[] methods;

        internal NativeInterface(Guid iid, bool inspectable, params Delegate[] methods)
        {
            Iid = iid;
            Inspectable = inspectable;
            var entries = new List<Delegate> { ManagedObject.queryInterface, ManagedObject.addRef, ManagedObject.release };
            if (inspectable) entries.AddRange(new Delegate[] { ManagedObject.getIids, ManagedObject.getRuntimeClassName, ManagedObject.getTrustLevel });
            entries.AddRange(methods);
            this.methods = entries.ToArray();
            // Lives for the process, like the static thunks it points to.
            Vtable = Marshal.AllocHGlobal(IntPtr.Size * this.methods.Length);
            for (int i = 0; i < this.methods.Length; ++i)
                Marshal.WriteIntPtr(Vtable, i * IntPtr.Size, Marshal.GetFunctionPointerForDelegate(this.methods[i]));
        }
    }

    // The native identity of a managed object. Native references keep it and its
    // target alive; with none, it lives as long as the target. An aggregating
    // outer answers other interfaces from its native inner.
    internal sealed class ManagedObject
    {
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        internal delegate int QueryInterfaceCall(IntPtr self, ref Guid iid, out IntPtr value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        internal delegate uint ReferenceCall(IntPtr self);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        internal delegate int GetIidsCall(IntPtr self, out uint count, out IntPtr iids);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        internal delegate int GetPointerCall(IntPtr self, out IntPtr value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        internal delegate int GetInt32Call(IntPtr self, out int value);

        internal static readonly QueryInterfaceCall queryInterface = QueryInterface;
        internal static readonly ReferenceCall addRef = AddRef;
        internal static readonly ReferenceCall release = Release;
        internal static readonly GetIidsCall getIids = GetIids;
        internal static readonly GetPointerCall getRuntimeClassName = GetRuntimeClassName;
        internal static readonly GetInt32Call getTrustLevel = GetTrustLevel;

        private const int E_NOINTERFACE = unchecked((int)0x80004002);
        private static readonly Guid unknown = new Guid("00000000-0000-0000-c000-000000000046");
        private static readonly Guid inspectable = new Guid("af86e2e0-b12d-4c6a-9c5a-d7aa65101e90");
        private static readonly Guid agile = new Guid("94ea2b94-e9cc-49e0-c0ff-ee64ca8f5b90");
        // The IInspectable identity of a composed outer, before its overrides interfaces.
        internal static readonly NativeInterface Identity = new NativeInterface(inspectable, true);
        private static readonly object gate = new object();
        private static readonly Dictionary<IntPtr, WeakReference> pointers = new Dictionary<IntPtr, WeakReference>();
        private static readonly HashSet<ManagedObject> referenced = new HashSet<ManagedObject>();
        private static readonly ConditionalWeakTable<object, ManagedObject> objects = new ConditionalWeakTable<object, ManagedObject>();

        private readonly object target;
        private readonly Errors.Scope invocation;
        private readonly bool invocationBound;
        private readonly NativeInterface[] interfaces;
        private readonly IntPtr[] blocks;
        private NativeObject inner;
        private int references;

        private ManagedObject(object target, NativeInterface[] interfaces, bool invocationBound = false)
        {
            this.target = target;
            invocation = Errors.Current;
            this.invocationBound = invocationBound;
            this.interfaces = interfaces;
            blocks = new IntPtr[interfaces.Length];
            lock (gate)
            {
                for (int i = 0; i < interfaces.Length; ++i)
                {
                    blocks[i] = Marshal.AllocHGlobal(IntPtr.Size);
                    Marshal.WriteIntPtr(blocks[i], interfaces[i].Vtable);
                    pointers.Add(blocks[i], new WeakReference(this));
                }
            }
        }

        ~ManagedObject()
        {
            lock (gate)
                foreach (IntPtr block in blocks)
                {
                    pointers.Remove(block);
                    Marshal.FreeHGlobal(block);
                }
        }

        // The first interface is also the object's IUnknown and IInspectable identity.
        internal static ManagedObject For(object target, params NativeInterface[] interfaces)
        {
            return objects.GetValue(target, key => new ManagedObject(key, interfaces));
        }

        internal IntPtr Pointer { get { return blocks[0]; } }

        internal void Aggregate(NativeObject value) { inner = value; }

        // The managed object behind a native IUnknown, if it is one of ours.
        internal static object Find(IntPtr identity)
        {
            WeakReference reference;
            lock (gate)
            {
                if (!pointers.TryGetValue(identity, out reference)) return null;
                var managed = (ManagedObject)reference.Target;
                return managed == null ? null : managed.target;
            }
        }

        // A counted reference for passing a managed object as a native argument.
        internal static Held Hold(object target, NativeInterface iface)
        {
            if (target == null) return new Held(null);
            // Each passed callback gets its own invocation context, even when
            // callers reuse the same managed delegate concurrently.
            ManagedObject managed = new ManagedObject(target, new[] { iface }, true);
            managed.Reference(1);
            return new Held(managed);
        }

        internal sealed class Held : IDisposable
        {
            private ManagedObject managed;
            internal Held(ManagedObject managed) { this.managed = managed; }
            internal IntPtr Pointer { get { return managed == null ? IntPtr.Zero : managed.Pointer; } }
            public void Dispose()
            {
                if (managed != null) managed.Reference(-1);
                managed = null;
            }
        }

        // Runs a native call on the target, reporting a managed failure as an HRESULT.
        internal static int Call(IntPtr self, Action<object> action)
        {
            ManagedObject managed = From(self);
            using (Errors.Callback(managed.invocation, managed.invocationBound))
            {
                try
                {
                    DispatcherContext.Install();
                    action(managed.target);
                    return 0;
                }
                catch (Exception error) { return Errors.Report(error); }
            }
        }

        // Fails construction when a managed subclass overrides a virtual this
        // projection cannot dispatch yet, instead of silently never calling it.
        internal static void RequireOverrides(Type type, Type projected, params string[] unsupported)
        {
            for (Type current = type; current != null && current != projected; current = current.BaseType)
                foreach (MethodInfo method in current.GetMethods(BindingFlags.DeclaredOnly | BindingFlags.Instance |
                    BindingFlags.Public | BindingFlags.NonPublic))
                    if (unsupported.Contains(method.Name))
                        throw new NotImplementedException(projected.FullName + "." + method.Name + " override");
        }

        private static ManagedObject From(IntPtr self)
        {
            WeakReference reference;
            lock (gate)
                if (pointers.TryGetValue(self, out reference))
                {
                    var managed = (ManagedObject)reference.Target;
                    if (managed != null) return managed;
                }
            throw new InvalidOperationException("Unknown managed interface pointer");
        }

        private uint Reference(int change)
        {
            lock (gate)
            {
                references += change;
                if (references == 0) referenced.Remove(this);
                else if (references == change && change > 0) referenced.Add(this);
                return (uint)references;
            }
        }

        private int Query(Guid iid, out IntPtr value)
        {
            value = IntPtr.Zero;
            if (iid == unknown || iid == agile) value = blocks[0];
            if (iid == inspectable)
                for (int i = 0; i < interfaces.Length; ++i)
                    if (interfaces[i].Inspectable) { value = blocks[i]; break; }
            for (int i = 0; value == IntPtr.Zero && i < interfaces.Length; ++i)
                if (interfaces[i].Iid == iid) value = blocks[i];
            if (value == IntPtr.Zero) return inner == null ? E_NOINTERFACE : inner.TryQueryInterface(iid, out value);
            Reference(1);
            return 0;
        }

        private static int QueryInterface(IntPtr self, ref Guid iid, out IntPtr value)
        {
            value = IntPtr.Zero;
            try { return From(self).Query(iid, out value); }
            catch (Exception error) { return Errors.Report(error); }
        }

        private static uint AddRef(IntPtr self)
        {
            try { return From(self).Reference(1); }
            catch (Exception) { return 0; }
        }

        private static uint Release(IntPtr self)
        {
            try { return From(self).Reference(-1); }
            catch (Exception) { return 0; }
        }

        private static int GetIids(IntPtr self, out uint count, out IntPtr iids)
        {
            count = 0;
            iids = IntPtr.Zero;
            return 0;
        }

        private static int GetRuntimeClassName(IntPtr self, out IntPtr value)
        {
            value = IntPtr.Zero;
            try
            {
                using (var name = new NativeString(From(self).target.GetType().FullName)) value = name.Detach();
                return 0;
            }
            catch (Exception error) { return Errors.Report(error); }
        }

        private static int GetTrustLevel(IntPtr self, out int value)
        {
            value = 0;
            return 0;
        }
    }
}
