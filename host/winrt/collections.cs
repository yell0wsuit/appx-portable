using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace AppxPortable.WinRT
{
    // The generator supplies closed interface IIDs and method slots from WinMD.
    internal sealed class StringObjectMapAbi
    {
        internal Guid Map { get; set; }
        internal Guid Iterable { get; set; }
        internal int Lookup { get; set; }
        internal int Size { get; set; }
        internal int HasKey { get; set; }
        internal int Insert { get; set; }
        internal int Remove { get; set; }
        internal int Clear { get; set; }
        internal int First { get; set; }
        internal int Current { get; set; }
        internal int HasCurrent { get; set; }
        internal int MoveNext { get; set; }
        internal int Key { get; set; }
        internal int Value { get; set; }

    }

    // CLR collection semantics over a native IMap<String,IInspectable>. No
    // managed shadow copy: mutations go through the native map/persistence layer.
    internal abstract class NativeStringObjectMap : IDictionary<string, object>, INativeWrapper
    {
        private const int Bounds = unchecked((int)0x8000000b), Changed = unchecked((int)0x8000000c);
        private readonly NativeObject native;
        private readonly StringObjectMapAbi abi;
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int LookupCall(IntPtr self, IntPtr key, out IntPtr value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int HasKeyCall(IntPtr self, IntPtr key, out byte value);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int InsertCall(IntPtr self, IntPtr key, IntPtr value, out byte replaced);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int RemoveCall(IntPtr self, IntPtr key);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int BooleanCall(IntPtr self, out byte value);
        protected NativeStringObjectMap(NativeObject value, StringObjectMapAbi abi) { native = value; this.abi = abi; }
        NativeObject INativeWrapper.Native { get { return native; } }
        private static void KeyNotNull(string key) { if (key == null) throw new ArgumentNullException("key"); }
        private static void Check(int hr)
        {
            if (hr == Changed) throw new InvalidOperationException("The native collection changed during enumeration");
            Errors.Check(hr);
        }
        public bool TryGetValue(string key, out object value)
        {
            KeyNotNull(key);
            value = null;
            using (NativeObject map = native.QueryInterface(abi.Map))
            using (var access = new NativeObject.Access(map))
            using (var text = new NativeString(key))
            {
                IntPtr result;
                int hr = access.Method<LookupCall>(abi.Lookup)(access.Self, text.Value, out result);
                if (hr == Bounds) { if (result != IntPtr.Zero) Marshal.Release(result); return false; }
                value = NativeObject.Inspectable(NativeObject.ObjectResult(hr, result));
                return true;
            }
        }
        public object this[string key]
        {
            get { object value; if (TryGetValue(key, out value)) return value; throw new KeyNotFoundException("Key not found: " + key); }
            set
            {
                KeyNotNull(key);
                using (NativeObject map = native.QueryInterface(abi.Map))
                using (var access = new NativeObject.Access(map))
                using (var text = new NativeString(key))
                using (NativeObject boxed = NativeObject.Box(value))
                {
                    byte replaced;
                    Check(access.Method<InsertCall>(abi.Insert)(access.Self, text.Value,
                        boxed == null ? IntPtr.Zero : boxed.DangerousGetHandle(), out replaced));
                }
            }
        }
        public int Count { get { using (NativeObject map = native.QueryInterface(abi.Map)) return checked((int)map.UInt32(abi.Size)); } }
        public bool IsReadOnly { get { return false; } }
        public bool ContainsKey(string key)
        {
            KeyNotNull(key);
            using (NativeObject map = native.QueryInterface(abi.Map))
            using (var access = new NativeObject.Access(map))
            using (var text = new NativeString(key))
            {
                byte found;
                Check(access.Method<HasKeyCall>(abi.HasKey)(access.Self, text.Value, out found));
                return found != 0;
            }
        }
        // As with IDictionary generally, a compound Add or pair removal needs
        // caller synchronization if other actors mutate this native map.
        public void Add(string key, object value)
        {
            if (ContainsKey(key)) throw new ArgumentException("Key already exists", "key");
            this[key] = value;
        }
        public bool Remove(string key)
        {
            KeyNotNull(key);
            using (NativeObject map = native.QueryInterface(abi.Map))
            using (var access = new NativeObject.Access(map))
            using (var text = new NativeString(key))
            {
                int hr = access.Method<RemoveCall>(abi.Remove)(access.Self, text.Value);
                if (hr == Bounds) return false;
                Check(hr);
                return true;
            }
        }
        public void Clear() { using (NativeObject map = native.QueryInterface(abi.Map)) map.Call(abi.Clear); }
        public void Add(KeyValuePair<string, object> item) { Add(item.Key, item.Value); }
        public bool Contains(KeyValuePair<string, object> item)
        {
            object value;
            return TryGetValue(item.Key, out value) && EqualityComparer<object>.Default.Equals(value, item.Value);
        }
        public bool Remove(KeyValuePair<string, object> item) { return Contains(item) && Remove(item.Key); }
        private static void ArraySpace<T>(T[] array, int index, int count)
        {
            if (array == null) throw new ArgumentNullException("array");
            if (index < 0 || index > array.Length) throw new ArgumentOutOfRangeException("index");
            if (array.Length - index < count) throw new ArgumentException("Array is too small", "array");
        }
        public void CopyTo(KeyValuePair<string, object>[] array, int index)
        {
            ArraySpace(array, index, Count);
            foreach (var pair in this) array[index++] = pair;
        }
        private bool Boolean(NativeObject value, int slot)
        {
            using (var access = new NativeObject.Access(value))
            {
                byte result;
                Check(access.Method<BooleanCall>(slot)(access.Self, out result));
                return result != 0;
            }
        }
        public IEnumerator<KeyValuePair<string, object>> GetEnumerator()
        {
            using (NativeObject iterable = native.QueryInterface(abi.Iterable))
            using (NativeObject iterator = iterable.Object(abi.First))
            {
                bool current = Boolean(iterator, abi.HasCurrent);
                while (current)
                {
                    KeyValuePair<string, object> result;
                    using (NativeObject pair = iterator.Object(abi.Current))
                        result = new KeyValuePair<string, object>(pair.String(abi.Key), NativeObject.Inspectable(pair.Object(abi.Value)));
                    yield return result;
                    current = Boolean(iterator, abi.MoveNext);
                }
            }
        }
        IEnumerator IEnumerable.GetEnumerator() { return GetEnumerator(); }
        public ICollection<string> Keys { get { return new View<string>(this, true); } }
        public ICollection<object> Values { get { return new View<object>(this, false); } }
        private sealed class View<T> : ICollection<T>
        {
            private readonly NativeStringObjectMap owner;
            private readonly bool keys;
            internal View(NativeStringObjectMap owner, bool keys) { this.owner = owner; this.keys = keys; }
            public int Count { get { return owner.Count; } }
            public bool IsReadOnly { get { return true; } }
            public void Add(T item) { throw new NotSupportedException("Read-only dictionary view"); }
            public bool Remove(T item) { throw new NotSupportedException("Read-only dictionary view"); }
            public void Clear() { throw new NotSupportedException("Read-only dictionary view"); }
            public bool Contains(T item)
            {
                foreach (T value in this) if (EqualityComparer<T>.Default.Equals(value, item)) return true;
                return false;
            }
            public void CopyTo(T[] array, int index) { ArraySpace(array, index, Count); foreach (T value in this) array[index++] = value; }
            public IEnumerator<T> GetEnumerator()
            {
                foreach (var pair in owner) yield return (T)(keys ? (object)pair.Key : pair.Value);
            }
            IEnumerator IEnumerable.GetEnumerator() { return GetEnumerator(); }
        }
    }
}
