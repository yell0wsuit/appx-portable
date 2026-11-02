using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;

namespace AppxPortable.WinRT
{
    // Implemented by every generated wrapper of a native object.
    internal interface INativeWrapper
    {
        NativeObject Native { get; }
    }

    // Explicit ABI calls avoid the CLR's WinRT metadata/projection machinery.
    // Callers initialize their COM apartment, as the game host already does.
    // Passed through a derived projected class's constructors to its root, so
    // that only the most derived constructor composes the native object.
    internal sealed class Composing
    {
        internal static readonly Composing Instance = new Composing();
        private Composing() { }
    }

    // An object argument of no selected type.
    internal sealed class InspectableObject : INativeWrapper
    {
        private readonly NativeObject native;
        internal InspectableObject(NativeObject value) { native = value; }
        NativeObject INativeWrapper.Native { get { return native; } }
    }

    internal sealed partial class NativeObject : SafeHandle
    {
        [DllImport("appxportable.dll", CallingConvention = CallingConvention.StdCall)]
        private static extern int ShimGetFactory(IntPtr name, out IntPtr factory);
        [DllImport("appxportable.dll", CallingConvention = CallingConvention.StdCall)]
        private static extern int ShimGetPackageFactory(IntPtr name, out IntPtr factory);
        [DllImport("combase.dll")]
        private static extern IntPtr WindowsGetStringRawBuffer(IntPtr value, out uint length);
        [DllImport("combase.dll")]
        private static extern int WindowsDeleteString(IntPtr value);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int GetPointer(IntPtr self, out IntPtr value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int GetUInt32(IntPtr self, out uint value);
        // TimeSpan setters; a by-value struct of one INT64 is passed as one. GetInt64 is in property_abi.cs.
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int PutInt64(IntPtr self, long value);
        // WinRT boolean is one byte, unlike Win32 BOOL.
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int GetBooleanProperty(IntPtr self, out byte value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int PutBooleanProperty(IntPtr self, byte value);
        // WinRT enums are signed or unsigned 32-bit values; preserve their raw bits.
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int PutUInt32Property(IntPtr self, uint value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int Invoke0(IntPtr self);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int Invoke1(IntPtr self, IntPtr argument);
        // EventRegistrationToken is the ABI's 64-bit token, out and by value.
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int AddCall(IntPtr self, IntPtr handler, out EventRegistrationToken token);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int RemoveCall(IntPtr self, EventRegistrationToken token);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int InvokeValue(IntPtr self, int value, IntPtr argument, out IntPtr result);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int ComposeCall(IntPtr self, IntPtr outer, out IntPtr inner, out IntPtr value);

        private static readonly Guid unknown = new Guid("00000000-0000-0000-c000-000000000046");
        private static readonly Guid inspectable = new Guid("af86e2e0-b12d-4c6a-9c5a-d7aa65101e90");
        private static readonly object gate = new object();
        // IUnknown identity -> wrapper, so one native object has one managed wrapper.
        private static readonly Dictionary<IntPtr, WeakReference> wrappers = new Dictionary<IntPtr, WeakReference>();

        private NativeObject(IntPtr owned) : base(IntPtr.Zero, true) { SetHandle(owned); }
        public override bool IsInvalid { get { return handle == IntPtr.Zero; } }
        protected override bool ReleaseHandle() { Marshal.Release(handle); return true; }

        internal static NativeObject TakeResult(int hr, IntPtr owned)
        {
            if (hr < 0)
            {
                if (owned != IntPtr.Zero) Marshal.Release(owned);
                Errors.Check(hr);
            }
            if (owned == IntPtr.Zero) throw new InvalidOperationException("WinRT returned a null interface");
            return new NativeObject(owned);
        }

        internal static NativeObject ObjectResult(int hr, IntPtr value)
        {
            return hr >= 0 && value == IntPtr.Zero ? null : TakeResult(hr, value);
        }

        // A pointer the caller still owns, such as a callback argument.
        internal static NativeObject Borrow(IntPtr value)
        {
            if (value == IntPtr.Zero) return null;
            Marshal.AddRef(value);
            return new NativeObject(value);
        }

        internal static NativeObject Factory(string className, Guid iid)
        {
            using (var name = new NativeString(className))
            {
                IntPtr pointer;
                int hr = ShimGetFactory(name.Value, out pointer);
                // Package-native components use the same manifest resolver as
                // native activation, without depending on a Wine export hotpatch.
                if (hr == unchecked((int)0x80040111))
                {
                    int packageResult = ShimGetPackageFactory(name.Value, out pointer);
                    // Preserve the existing missing-factory contract when neither
                    // the shim registry nor the package declares this class.
                    if (packageResult != unchecked((int)0x80040154)) hr = packageResult;
                }
                if (hr == unchecked((int)0x80040111))
                {
                    // Native system classes need no package registration. Preserve the
                    // established unknown-class error when the OS also lacks the class.
                    int systemResult = RoGetActivationFactory(name.Value, ref iid, out pointer);
                    if (systemResult == unchecked((int)0x80040154) || systemResult == unchecked((int)0x80040111))
                        systemResult = hr;
                    return TakeResult(systemResult, pointer);
                }
                using (NativeObject factory = TakeResult(hr, pointer))
                    return factory.QueryInterface(iid);
            }
        }

        // Returns the managed object behind a native identity, the cached wrapper,
        // or a new wrapper over iid. Disposes value.
        internal static T Wrap<T>(NativeObject value, Guid iid, Func<NativeObject, T> create) where T : class
        {
            if (value == null) return null;
            using (value)
            {
                IntPtr identity = value.Identity();
                object managed = ManagedObject.Find(identity);
                var projection = managed as IManagedProjection;
                if (projection != null) managed = projection.Resolve(value);
                T existing = managed as T;
                if (existing != null) return existing;
                WeakReference cached;
                lock (gate)
                    if (wrappers.TryGetValue(identity, out cached) && (existing = cached.Target as T) != null) return existing;
                NativeObject owned = value.QueryInterface(iid);
                try
                {
                    T created = create(owned);
                    // Construct outside the lock: a factory may call native code
                    // that reenters managed code on another thread.
                    lock (gate)
                    {
                        if (wrappers.TryGetValue(identity, out cached) && (existing = cached.Target as T) != null)
                        {
                            owned.Dispose();
                            return existing;
                        }
                        wrappers[identity] = new WeakReference(created);
                    }
                    return created;
                }
                catch { owned.Dispose(); throw; }
            }
        }

        // A default-activated wrapper must participate in the same cache as
        // wrappers first encountered through native results or callbacks.
        internal static void Remember(object value, NativeObject native)
        {
            IntPtr identity = native.Identity();
            lock (gate)
            {
                WeakReference previous;
                if (!wrappers.TryGetValue(identity, out previous) || !previous.IsAlive)
                    wrappers[identity] = new WeakReference(value);
            }
        }

        // The native object behind a projected argument. Managed implementations of
        // WinRT interfaces need their own native identity, which is not built yet.
        internal static NativeObject Of(object value)
        {
            if (value == null) return null;
            var wrapper = value as INativeWrapper;
            if (wrapper == null) throw new NotSupportedException("Managed " + value.GetType().FullName + " cannot be passed to native code yet");
            return wrapper.Native;
        }

        // An object argument: the managed object behind it, or a generic wrapper.
        internal static object Inspectable(NativeObject value)
        {
            if (value == null) return null;
            object unboxed;
            try { if (Unbox(value, out unboxed)) { value.Dispose(); return unboxed; } }
            catch { value.Dispose(); throw; }
            return Wrap<object>(value, inspectable, v => new InspectableObject(v));
        }

        // A projected argument as the interface the callee expects; dispose after the call.
        internal static NativeObject Argument(object value, Guid iid)
        {
#if APPX_PORTABLE_INTEROP
            if (value != null && !(value is INativeWrapper) && value is Interop.INativeObject)
            {
                using (var lease = ((Interop.INativeObject)value).AcquireInterface(iid)) return FromLease(lease);
            }
#endif
            NativeObject native = Of(value);
            return native == null ? null : ManagedObject.ComposedArgument(value, iid) ?? native.QueryInterface(iid);
        }

#if APPX_PORTABLE_INTEROP
        internal static NativeObject FromLease(SafeHandle borrowed)
        {
            if (borrowed == null) return null;
            bool held = false;
            try { borrowed.DangerousAddRef(ref held); return Borrow(borrowed.DangerousGetHandle()); }
            finally { if (held) borrowed.DangerousRelease(); }
        }
#endif

        // IInspectable::GetRuntimeClassName, slot 4.
        internal string RuntimeClassName() { return String(4); }

        internal NativeObject QueryInterface(Guid iid)
        {
            using (var call = new Access(this))
            {
                IntPtr pointer;
                int hr = Marshal.QueryInterface(call.Self, ref iid, out pointer);
                return TakeResult(hr, pointer);
            }
        }

        // Aggregation: answers an outer's QueryInterface without throwing.
        internal int TryQueryInterface(Guid iid, out IntPtr value)
        {
            using (var call = new Access(this))
                return Marshal.QueryInterface(call.Self, ref iid, out value);
        }

        // The IUnknown pointer, valid while this reference is alive.
        internal IntPtr Identity()
        {
            using (NativeObject identity = QueryInterface(unknown)) return identity.handle;
        }

        // A getter's object result; null is a valid WinRT value here.
        internal NativeObject Object(int slot)
        {
            using (var call = new Access(this))
            {
                IntPtr pointer;
                int hr = call.Method<GetPointer>(slot)(call.Self, out pointer);
                return hr >= 0 && pointer == IntPtr.Zero ? null : TakeResult(hr, pointer);
            }
        }

        internal uint UInt32(int slot)
        {
            using (var call = new Access(this))
            {
                uint value;
                Errors.Check(call.Method<GetUInt32>(slot)(call.Self, out value));
                return value;
            }
        }

        internal long Int64(int slot)
        {
            using (var call = new Access(this))
            {
                long value;
                Errors.Check(call.Method<GetInt64>(slot)(call.Self, out value));
                return value;
            }
        }

        internal void SetInt64(int slot, long value)
        {
            using (var call = new Access(this))
                Errors.Check(call.Method<PutInt64>(slot)(call.Self, value));
        }

        internal bool Boolean(int slot)
        {
            using (var call = new Access(this))
            {
                byte value;
                Errors.Check(call.Method<GetBooleanProperty>(slot)(call.Self, out value));
                return value != 0;
            }
        }

        internal void SetUInt32(int slot, uint value)
        {
            using (var call = new Access(this))
                Errors.Check(call.Method<PutUInt32Property>(slot)(call.Self, value));
        }

        internal void SetBoolean(int slot, bool value)
        {
            using (var call = new Access(this))
                Errors.Check(call.Method<PutBooleanProperty>(slot)(call.Self, value ? (byte)1 : (byte)0));
        }

        internal string String(int slot)
        {
            using (var call = new Access(this))
            {
                IntPtr value = IntPtr.Zero;
                try
                {
                    Errors.Check(call.Method<GetPointer>(slot)(call.Self, out value));
                    uint length;
                    IntPtr text = WindowsGetStringRawBuffer(value, out length);
                    return length == 0 ? string.Empty : Marshal.PtrToStringUni(text, checked((int)length));
                }
                finally { WindowsDeleteString(value); }
            }
        }

        internal void Call(int slot)
        {
            using (var call = new Access(this))
                Errors.Check(call.Method<Invoke0>(slot)(call.Self));
        }

        internal void Call(int slot, IntPtr argument)
        {
            using (var call = new Access(this))
                Errors.Check(call.Method<Invoke1>(slot)(call.Self, argument));
        }

        internal void Call(int slot, NativeObject argument)
        {
            if (argument == null) { Call(slot, IntPtr.Zero); return; }
            using (var held = new Access(argument)) Call(slot, held.Self);
        }

        // A call taking a 32-bit value and a pointer that returns an object.
        internal NativeObject Call(int slot, int value, IntPtr argument)
        {
            using (var call = new Access(this))
            {
                IntPtr result;
                int hr = call.Method<InvokeValue>(slot)(call.Self, value, argument, out result);
                return hr >= 0 && result == IntPtr.Zero ? null : TakeResult(hr, result);
            }
        }

        internal EventRegistrationToken AddHandler(int slot, IntPtr handler)
        {
            using (var call = new Access(this))
            {
                EventRegistrationToken token;
                Errors.Check(call.Method<AddCall>(slot)(call.Self, handler, out token));
                return token;
            }
        }

        internal void RemoveHandler(int slot, EventRegistrationToken token)
        {
            using (var call = new Access(this))
                Errors.Check(call.Method<RemoveCall>(slot)(call.Self, token));
        }

        // A composable factory's CreateInstance(outer, out inner): returns the
        // non-delegating inner and releases the delegating result.
        internal NativeObject Compose(int slot, IntPtr outer)
        {
            using (var call = new Access(this))
            {
                IntPtr inner, value;
                int hr = call.Method<ComposeCall>(slot)(call.Self, outer, out inner, out value);
                if (value != IntPtr.Zero) Marshal.Release(value);
                return TakeResult(hr, inner);
            }
        }

        // Keep the COM reference alive across vtable lookup and invocation,
        // including when another managed thread disposes the wrapper.
        internal sealed class Access : IDisposable
        {
            private NativeObject owner;
            internal readonly IntPtr Self;
            internal Access(NativeObject owner)
            {
                bool held = false;
                owner.DangerousAddRef(ref held);
                this.owner = owner;
                Self = owner.DangerousGetHandle();
            }
            internal T Method<T>(int slot) where T : class
            {
                IntPtr method = Marshal.ReadIntPtr(Marshal.ReadIntPtr(Self), checked(slot * IntPtr.Size));
                Delegate callback = Marshal.GetDelegateForFunctionPointer(method, typeof(T));
                // A pointer originating from our CCW may resolve to the original
                // managed delegate, even when the requested delegate type has an
                // identical signature. Rebind it instead of an invalid CLR cast.
                if (callback is T) return (T)(object)callback;
                return (T)(object)Delegate.CreateDelegate(typeof(T), callback.Target, callback.Method);
            }
            public void Dispose()
            {
                if (owner == null) return;
                owner.DangerousRelease();
                owner = null;
            }
        }
    }

    internal sealed class PinnedBytes : IDisposable
    {
        private GCHandle pin;
        internal uint Length { get; private set; }
        internal IntPtr Pointer { get; private set; }
        internal PinnedBytes(byte[] value)
        {
            Length = value == null ? 0 : checked((uint)value.Length);
            if (Length == 0) return;
            pin = GCHandle.Alloc(value, GCHandleType.Pinned);
            Pointer = pin.AddrOfPinnedObject();
        }
        public void Dispose()
        {
            if (pin.IsAllocated) pin.Free();
            Pointer = IntPtr.Zero;
        }
    }

    internal sealed class NativeString : IDisposable
    {
        [DllImport("combase.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
        private static extern int WindowsCreateString(string text, uint length, out IntPtr value);
        [DllImport("combase.dll")]
        private static extern int WindowsDeleteString(IntPtr value);
        [DllImport("combase.dll")]
        private static extern IntPtr WindowsGetStringRawBuffer(IntPtr value, out uint length);
        internal static string Read(IntPtr value)
        {
            uint length;
            IntPtr text = WindowsGetStringRawBuffer(value, out length);
            return length == 0 ? string.Empty : Marshal.PtrToStringUni(text, checked((int)length));
        }
        internal static string TakeResult(int hr, IntPtr value)
        {
            try { Errors.Check(hr); return Read(value); }
            finally { WindowsDeleteString(value); }
        }
        internal IntPtr Value { get; private set; }
        internal NativeString(string text)
        {
            text = text ?? string.Empty;
            IntPtr value;
            Marshal.ThrowExceptionForHR(WindowsCreateString(text, (uint)text.Length, out value));
            Value = value;
        }
        // Transfers ownership of the HSTRING to a native caller.
        internal IntPtr Detach()
        {
            IntPtr value = Value;
            Value = IntPtr.Zero;
            return value;
        }
        public void Dispose()
        {
            WindowsDeleteString(Value);
            Value = IntPtr.Zero;
        }
    }
}
