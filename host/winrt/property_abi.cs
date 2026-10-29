using System;
using System.Runtime.InteropServices;

namespace AppxPortable.WinRT
{
    // Windows.UI.Xaml.Interop.TypeName projects to System.Type. Keep its HSTRING
    // alive for the entire native call; TypeKind is a 32-bit enum on both ABIs.
    internal sealed class NativeTypeName : IDisposable
    {
        [StructLayout(LayoutKind.Sequential)]
        internal struct Abi { internal IntPtr Name; internal int Kind; }
        private readonly NativeString name;
        internal readonly Abi Value;
        internal NativeTypeName(Type type)
        {
            if (type == null) throw new ArgumentNullException("type");
            string text = type == typeof(string) ? "String" : type == typeof(int) ? "Int32" :
                type == typeof(bool) ? "Boolean" : type == typeof(double) ? "Double" :
                type == typeof(float) ? "Single" : type == typeof(uint) ? "UInt32" :
                type == typeof(long) ? "Int64" : type == typeof(ulong) ? "UInt64" : null;
            int kind = text == null ? 2 : 0;
            if (type == typeof(object)) { text = "Object"; kind = 0; }
            if (text == null)
            {
                if (type.FullName == null || type.IsPointer || type.IsByRef || type.ContainsGenericParameters)
                    throw new NotSupportedException("TypeName: " + type);
                text = type.AssemblyQualifiedName;
                if (type.Assembly == typeof(NativeTypeName).Assembly && type.FullName.StartsWith("Windows.", StringComparison.Ordinal)) { text = type.FullName; kind = 1; }
            }
            name = new NativeString(text);
            Value = new Abi { Name = name.Value, Kind = kind };
        }
        public void Dispose() { name.Dispose(); }
    }

    // Internal projection/shim contract for managed reference-type validation.
    // The CLR owns assignability (including interfaces and generic/base types).
    internal static class ManagedPropertyValue
    {
        internal static readonly Guid Iid = new Guid("0b8297a1-48a9-4ff6-b911-bf34bc07059c");
        [DllImport("combase.dll")]
        private static extern IntPtr WindowsGetStringRawBuffer(IntPtr value, out uint length);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int IsInstanceCall(IntPtr self, IntPtr name, out byte matches);
        private static readonly IsInstanceCall isInstance = IsInstance;
        internal static readonly NativeInterface Interface = new NativeInterface(Iid, false, isInstance);
        private static int IsInstance(IntPtr self, IntPtr name, out byte matches)
        {
            byte value = 0;
            int hr = ManagedObject.Call(self, target =>
            {
                uint length;
                IntPtr text = WindowsGetStringRawBuffer(name, out length);
                Type type = Type.GetType(Marshal.PtrToStringUni(text, checked((int)length)), false);
                if (type == null) throw new NotSupportedException("Managed property type cannot be resolved");
                value = type.IsInstanceOfType(target) ? (byte)1 : (byte)0;
            });
            matches = value;
            return hr;
        }
    }

    internal sealed partial class NativeObject
    {
        [DllImport("combase.dll", CallingConvention = CallingConvention.StdCall)]
        private static extern int RoGetActivationFactory(IntPtr name, ref Guid iid, out IntPtr value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int BoxString(IntPtr self, IntPtr value, out IntPtr result);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int BoxInt32(IntPtr self, int value, out IntPtr result);
        private static readonly Guid propertyValue = new Guid("4bd682dd-7554-40e9-9a9b-82654ede7e62");

        internal static NativeObject Box(object value)
        {
            if (value == null) return null;
            if (value is INativeWrapper) return Argument(value, inspectable);
            if (!(value is string) && !(value is int))
            {
                if (value.GetType().IsValueType) throw new NotSupportedException("Boxing " + value.GetType().FullName);
                ManagedObject managed = ManagedObject.For(value, ManagedObject.Identity);
                NativeObject reference = Borrow(managed.Pointer);
                // Pointer extraction does not itself root the weak-table entry.
                // Keep it alive until AddRef establishes the native reference.
                GC.KeepAlive(managed);
                return reference;
            }
            var iid = new Guid("629bdbc8-d932-4ff4-96b9-8d96c5c1e858");
            using (var name = new NativeString("Windows.Foundation.PropertyValue"))
            {
                IntPtr pointer;
                int hr = RoGetActivationFactory(name.Value, ref iid, out pointer);
                using (NativeObject factory = TakeResult(hr, pointer))
                using (var call = new Access(factory))
                {
                    IntPtr result;
                    if (value is string)
                        using (var text = new NativeString((string)value))
                            // IPropertyValueStatics::CreateString, windows.foundation.h, slot 18.
                            return TakeResult(call.Method<BoxString>(18)(call.Self, text.Value, out result), result);
                    // IPropertyValueStatics::CreateInt32, slot 10.
                    return TakeResult(call.Method<BoxInt32>(10)(call.Self, (int)value, out result), result);
                }
            }
        }
        private static bool Unbox(NativeObject value, out object result)
        {
            result = null;
            IntPtr pointer;
            int hr = value.TryQueryInterface(propertyValue, out pointer);
            if (hr == unchecked((int)0x80004002)) return false;
            using (NativeObject box = TakeResult(hr, pointer))
            {
                // IPropertyValue::get_Type, GetInt32 and GetString, slots 6, 11, 19.
                uint kind = box.UInt32(6);
                if (kind == 4) result = unchecked((int)box.UInt32(11));
                else if (kind == 12) result = box.String(19);
                else throw new NotSupportedException("Unboxing PropertyType " + kind);
                return true;
            }
        }
    }
}
