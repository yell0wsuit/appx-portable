using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;

using AppxPortable.WinRT.Xaml;

namespace Windows.UI.Xaml
{
    public partial class FrameworkElement
    {
        internal global::System.Windows.FrameworkElement Element
        {
            get { return (global::System.Windows.FrameworkElement)Values.Dependency(this); }
        }

        public object FindName(string name)
        {
            return Values.FromWpf(Element.FindName(name), typeof(object));
        }

        // The logical parent, as UWP's.
        public DependencyObject Parent { get { return Peers.Wrap(Element.Parent); } }

        public ResourceDictionary Resources
        {
            get { return (ResourceDictionary)Peers.Wrap(Element.Resources); }
            set { Element.Resources = (global::System.Windows.ResourceDictionary)Peers.Of(value); }
        }
    }

    // A UWP resource map over a WPF ResourceDictionary: values are stored as peers.
    public partial class ResourceDictionary : IDictionary<object, object>
    {
        private global::System.Windows.ResourceDictionary Map
        {
            get { CheckThread(); return (global::System.Windows.ResourceDictionary)peer; }
        }

        private static void NotNull(object key) { if (key == null) throw new ArgumentNullException("key"); }

        public object this[object key]
        {
            get
            {
                object value;
                if (!TryGetValue(key, out value)) throw new KeyNotFoundException("Resource not found: " + key);
                return value;
            }
            set
            {
                NotNull(key);
                Map[key] = global::AppxPortable.WinRT.Xaml.Values.ToWpf(value, typeof(object));
            }
        }

        public ICollection<object> Keys { get { return new LiveView<object>(() => Map.Keys.Cast<object>().ToList()); } }

        public ICollection<object> Values
        {
            get { return new LiveView<object>(() => Map.Keys.Cast<object>().Select(k => Read(Map, k)).ToList()); }
        }

        public int Count { get { return Map.Count; } }
        public bool IsReadOnly { get { return false; } }

        public Uri Source
        {
            get { return XamlLoader.SourceOf(Map); }
            set { XamlLoader.LoadDictionary(Map, value); }
        }

        public IList<ResourceDictionary> MergedDictionaries
        {
            get { return new PeerList<ResourceDictionary, global::System.Windows.ResourceDictionary>(this, Map.MergedDictionaries); }
        }

        private static object Read(global::System.Windows.ResourceDictionary map, object key)
        {
            return global::AppxPortable.WinRT.Xaml.Values.FromWpf(map[key], typeof(object));
        }

        public void Add(object key, object value)
        {
            NotNull(key);
            if (Map.Contains(key)) throw new ArgumentException("Resource already present: " + key);
            Map.Add(key, global::AppxPortable.WinRT.Xaml.Values.ToWpf(value, typeof(object)));
        }

        public bool ContainsKey(object key) { NotNull(key); return Map.Contains(key); }

        public bool Remove(object key)
        {
            NotNull(key);
            if (!Map.Contains(key)) return false;
            Map.Remove(key);
            return true;
        }

        public bool TryGetValue(object key, out object value)
        {
            NotNull(key);
            value = null;
            if (!Map.Contains(key)) return false;
            value = Read(Map, key);
            return true;
        }

        public void Add(KeyValuePair<object, object> item) { Add(item.Key, item.Value); }
        public void Clear() { Map.Clear(); }

        public bool Contains(KeyValuePair<object, object> item)
        {
            object value;
            return TryGetValue(item.Key, out value) && Equals(value, item.Value);
        }

        public bool Remove(KeyValuePair<object, object> item) { return Contains(item) && Remove(item.Key); }

        public void CopyTo(KeyValuePair<object, object>[] array, int index) { LiveView<object>.CopyTo(Snapshot(), array, index); }

        private List<KeyValuePair<object, object>> Snapshot()
        {
            var map = Map;
            return map.Keys.Cast<object>().Select(k => new KeyValuePair<object, object>(k, Read(map, k))).ToList();
        }

        // Enumeration works on a snapshot, so changes while enumerating are safe.
        public IEnumerator<KeyValuePair<object, object>> GetEnumerator() { return Snapshot().GetEnumerator(); }
        IEnumerator IEnumerable.GetEnumerator() { return GetEnumerator(); }
    }
}

namespace Windows.UI.Xaml.Controls
{
    public partial class Panel
    {
        private UIElementCollection children;

        public UIElementCollection Children
        {
            get
            {
                var panel = (global::System.Windows.Controls.Panel)Values.Dependency(this);
                return children ?? (children = new UIElementCollection(this, panel.Children));
            }
        }
    }

    // Panel.Children: a live list of wrappers over the WPF panel's children.
    public sealed partial class UIElementCollection : IList<UIElement>
    {
        private readonly Panel owner;
        private readonly global::System.Windows.Controls.UIElementCollection items;

        internal UIElementCollection(Panel owner, global::System.Windows.Controls.UIElementCollection items) { this.owner = owner; this.items = items; }

        private global::System.Windows.Controls.UIElementCollection Items { get { owner.CheckThread(); return items; } }

        private static global::System.Windows.UIElement Peer(UIElement item)
        {
            if (item == null) throw new ArgumentNullException("item");
            return (global::System.Windows.UIElement)Peers.Of(item);
        }

        public UIElement this[int index]
        {
            get { return (UIElement)Peers.Wrap(Items[index]); }
            set { Items[index] = Peer(value); }
        }

        public int Count { get { return Items.Count; } }
        public bool IsReadOnly { get { return false; } }
        public int IndexOf(UIElement item) { return item == null ? -1 : Items.IndexOf(Peer(item)); }
        public void Insert(int index, UIElement item) { Items.Insert(index, Peer(item)); }
        public void RemoveAt(int index) { Items.RemoveAt(index); }
        public void Add(UIElement item) { Items.Add(Peer(item)); }
        public void Clear() { Items.Clear(); }
        public bool Contains(UIElement item) { return item != null && Items.Contains(Peer(item)); }
        public void CopyTo(UIElement[] array, int index) { LiveView<UIElement>.CopyTo(this.ToList(), array, index); }

        public bool Remove(UIElement item)
        {
            if (!Contains(item)) return false;
            Items.Remove(Peer(item));
            return true;
        }

        public IEnumerator<UIElement> GetEnumerator()
        {
            return Items.Cast<global::System.Windows.UIElement>().Select(p => (UIElement)Peers.Wrap(p)).ToList().GetEnumerator();
        }

        IEnumerator IEnumerable.GetEnumerator() { return GetEnumerator(); }
    }
}

namespace Windows.UI.Xaml.Media
{
    public sealed partial class VisualTreeHelper
    {
        private VisualTreeHelper() { }

        // WPF's tree also holds visual-only nodes (ContainerVisual, DrawingVisual) that
        // UWP's element tree has no counterpart for; their children stand in their place.
        public static int GetChildrenCount(DependencyObject reference)
        {
            return Children(Values.Dependency(reference)).Count;
        }

        public static DependencyObject GetChild(DependencyObject reference, int childIndex)
        {
            var children = Children(Values.Dependency(reference));
            if (childIndex < 0 || childIndex >= children.Count) throw new global::System.ArgumentOutOfRangeException("childIndex");
            return Peers.Wrap(children[childIndex]);
        }

        public static DependencyObject GetParent(DependencyObject reference)
        {
            var parent = global::System.Windows.Media.VisualTreeHelper.GetParent(Values.Dependency(reference));
            while (parent != null && !Peers.IsPeer(parent)) parent = global::System.Windows.Media.VisualTreeHelper.GetParent(parent);
            return Peers.Wrap(parent);
        }

        private static List<global::System.Windows.DependencyObject> Children(global::System.Windows.DependencyObject reference)
        {
            var children = new List<global::System.Windows.DependencyObject>();
            Collect(reference, children);
            return children;
        }

        private static void Collect(global::System.Windows.DependencyObject node, List<global::System.Windows.DependencyObject> into)
        {
            int count = global::System.Windows.Media.VisualTreeHelper.GetChildrenCount(node);
            for (int i = 0; i < count; ++i)
            {
                var child = global::System.Windows.Media.VisualTreeHelper.GetChild(node, i);
                if (Peers.IsPeer(child)) into.Add(child);
                else Collect(child, into);
            }
        }
    }
}

namespace AppxPortable.WinRT.Xaml
{
    // A live list of wrappers over a WPF collection of their peers.
    internal sealed class PeerList<TWrapper, TPeer> : IList<TWrapper>
        where TWrapper : global::Windows.UI.Xaml.DependencyObject where TPeer : class
    {
        private readonly global::Windows.UI.Xaml.DependencyObject owner;
        private readonly IList<TPeer> items;

        internal PeerList(global::Windows.UI.Xaml.DependencyObject owner, IList<TPeer> items) { this.owner = owner; this.items = items; }

        private IList<TPeer> Items { get { owner.CheckThread(); return items; } }

        private static TPeer Peer(TWrapper item)
        {
            if (item == null) throw new ArgumentNullException("item");
            return (TPeer)Peers.Of(item);
        }

        public TWrapper this[int index] { get { return (TWrapper)Peers.Wrap(Items[index]); } set { Items[index] = Peer(value); } }
        public int Count { get { return Items.Count; } }
        public bool IsReadOnly { get { return false; } }
        public int IndexOf(TWrapper item) { return item == null ? -1 : Items.IndexOf(Peer(item)); }
        public void Insert(int index, TWrapper item) { Items.Insert(index, Peer(item)); }
        public void RemoveAt(int index) { Items.RemoveAt(index); }
        public void Add(TWrapper item) { Items.Add(Peer(item)); }
        public void Clear() { Items.Clear(); }
        public bool Contains(TWrapper item) { return item != null && Items.Contains(Peer(item)); }
        public void CopyTo(TWrapper[] array, int index) { LiveView<TWrapper>.CopyTo(this.ToList(), array, index); }
        public bool Remove(TWrapper item) { return item != null && Items.Remove(Peer(item)); }
        public IEnumerator<TWrapper> GetEnumerator() { return Items.Select(p => (TWrapper)Peers.Wrap(p)).ToList().GetEnumerator(); }
        IEnumerator IEnumerable.GetEnumerator() { return GetEnumerator(); }
    }

    // A read-only view whose contents are read from its owner on each use.
    internal sealed class LiveView<T> : ICollection<T>
    {
        private readonly Func<List<T>> read;

        internal LiveView(Func<List<T>> read) { this.read = read; }

        internal static void CopyTo<TItem>(List<TItem> items, TItem[] array, int index)
        {
            if (array == null) throw new ArgumentNullException("array");
            if (index < 0) throw new ArgumentOutOfRangeException("index");
            if (array.Length - index < items.Count) throw new ArgumentException("Destination array is too small.");
            items.CopyTo(array, index);
        }

        public int Count { get { return read().Count; } }
        public bool IsReadOnly { get { return true; } }
        public bool Contains(T item) { return read().Contains(item); }
        public void CopyTo(T[] array, int index) { CopyTo(read(), array, index); }
        public IEnumerator<T> GetEnumerator() { return read().GetEnumerator(); }
        IEnumerator IEnumerable.GetEnumerator() { return GetEnumerator(); }
        public void Add(T item) { throw new NotSupportedException("Read-only view"); }
        public void Clear() { throw new NotSupportedException("Read-only view"); }
        public bool Remove(T item) { throw new NotSupportedException("Read-only view"); }
    }
}
