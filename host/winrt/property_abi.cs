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
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int BoxInt64(IntPtr self, long value, out IntPtr result);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int BoxBoolean(IntPtr self, byte value, out IntPtr result);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int BoxDouble(IntPtr self, double value, out IntPtr result);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int BoxGuid(IntPtr self, Guid value, out IntPtr result);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int GetInt64(IntPtr self, out long value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int GetBoolean(IntPtr self, out byte value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int GetDouble(IntPtr self, out double value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int GetGuid(IntPtr self, out Guid value);
        private static readonly Guid propertyValue = new Guid("4bd682dd-7554-40e9-9a9b-82654ede7e62");

        internal static NativeObject Box(object value)
        {
            if (value == null) return null;
            if (value is INativeWrapper) return Argument(value, inspectable);
            if (!(value is string) && !(value is int) && !(value is long) && !(value is bool) && !(value is double) && !(value is Guid))
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
                    // IPropertyValueStatics slots from windows.foundation.h.
                    if (value is long) return TakeResult(call.Method<BoxInt64>(12)(call.Self, (long)value, out result), result);
                    if (value is bool) return TakeResult(call.Method<BoxBoolean>(17)(call.Self, (bool)value ? (byte)1 : (byte)0, out result), result);
                    if (value is double) return TakeResult(call.Method<BoxDouble>(15)(call.Self, (double)value, out result), result);
                    if (value is Guid) return TakeResult(call.Method<BoxGuid>(20)(call.Self, (Guid)value, out result), result);
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
            using (var call = new Access(box))
            {
                // IPropertyValue::get_Type, GetInt32 and GetString, slots 6, 11, 19.
                uint kind = box.UInt32(6);
                if (kind == 4) result = unchecked((int)box.UInt32(11));
                else if (kind == 12) result = box.String(19);
                else if (kind == 6) { long value64; Errors.Check(call.Method<GetInt64>(13)(call.Self, out value64)); result = value64; }
                else if (kind == 11) { byte boolean; Errors.Check(call.Method<GetBoolean>(18)(call.Self, out boolean)); result = boolean != 0; }
                else if (kind == 9) { double number; Errors.Check(call.Method<GetDouble>(16)(call.Self, out number)); result = number; }
                else if (kind == 16) { Guid guid; Errors.Check(call.Method<GetGuid>(20)(call.Self, out guid)); result = guid; }
                else throw new NotSupportedException("Unboxing PropertyType " + kind);
                return true;
            }
        }
    }
}
