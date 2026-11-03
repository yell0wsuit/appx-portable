using AppxPortable.WinRT.Xaml;

namespace Windows.UI.Xaml.Media.Animation
{
    public sealed partial class Storyboard
    {
        private global::System.Windows.Media.Animation.Storyboard Animation
        {
            get { return (global::System.Windows.Media.Animation.Storyboard)Values.Dependency(this); }
        }

        // Targets come from Storyboard.Target/TargetName on the children, as in UWP.
        public void Begin() { Animation.Begin(); }

        public void Stop() { Animation.Stop(); }

        public TimelineCollection Children { get { return TimelineCollection.Of(Animation.Children, this); } }

        private static global::System.Windows.Media.Animation.Timeline Peer(Timeline timeline)
        {
            if (timeline == null) throw new global::System.ArgumentNullException("timeline");
            return (global::System.Windows.Media.Animation.Timeline)Values.Dependency(timeline);
        }

        public static void SetTarget(Timeline timeline, DependencyObject target)
        {
            global::System.Windows.Media.Animation.Storyboard.SetTarget(Peer(timeline), target == null ? null : Values.Dependency(target));
        }

        public static string GetTargetName(Timeline element) { return global::System.Windows.Media.Animation.Storyboard.GetTargetName(Peer(element)); }
        public static void SetTargetName(Timeline element, string name) { global::System.Windows.Media.Animation.Storyboard.SetTargetName(Peer(element), name); }

        // UWP property paths name owner types by their UWP names; WPF resolves the
        // mapped dependency properties (see PropertyPaths).
        public static string GetTargetProperty(Timeline element) { return PropertyPaths.Text(global::System.Windows.Media.Animation.Storyboard.GetTargetProperty(Peer(element))); }
        public static void SetTargetProperty(Timeline element, string path)
        {
            global::System.Windows.Media.Animation.Storyboard.SetTargetProperty(Peer(element), PropertyPaths.FromUwp(path));
        }
    }

    public partial class Timeline
    {
        private global::System.Collections.Generic.Dictionary<long, global::System.Delegate> completed;

        // WPF raises Completed with the clock as sender; UWP with the timeline.
        public global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken add_Completed(global::System.EventHandler<object> handler)
        {
            if (handler == null) throw new global::System.ArgumentNullException("handler");
            var peer = (global::System.Windows.Media.Animation.Timeline)Values.Dependency(this);
            global::System.EventHandler callback = (s, e) => Host.Callback(() => handler(this, null));
            peer.Completed += callback;
            return Tokens.Add(ref completed, callback);
        }

        public void remove_Completed(global::System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token)
        {
            var peer = (global::System.Windows.Media.Animation.Timeline)Values.Dependency(this);
            var callback = Tokens.Remove(completed, token) as global::System.EventHandler;
            if (callback != null) peer.Completed -= callback;
        }
    }

    public sealed partial class TimelineCollection : global::System.Collections.Generic.IList<Timeline>
    {
        private readonly global::System.Windows.Media.Animation.TimelineCollection items;
        private readonly DependencyObject owner;
        private static readonly global::System.Runtime.CompilerServices.ConditionalWeakTable<global::System.Windows.Media.Animation.TimelineCollection, TimelineCollection> wrappers =
            new global::System.Runtime.CompilerServices.ConditionalWeakTable<global::System.Windows.Media.Animation.TimelineCollection, TimelineCollection>();
        private TimelineCollection(global::System.Windows.Media.Animation.TimelineCollection items, DependencyObject owner) { this.items = items; this.owner = owner; }
        internal static TimelineCollection Of(global::System.Windows.Media.Animation.TimelineCollection items, DependencyObject owner)
        {
            return wrappers.GetValue(items, i => new TimelineCollection(i, owner));
        }
        private global::System.Windows.Media.Animation.TimelineCollection Items { get { owner.CheckThread(); return items; } }
        private static global::System.Windows.Media.Animation.Timeline Peer(Timeline value)
        {
            if (value == null) throw new global::System.ArgumentNullException("value");
            return (global::System.Windows.Media.Animation.Timeline)Values.Dependency(value);
        }
        public Timeline this[int index] { get { return (Timeline)Peers.Wrap(Items[index]); } set { Items[index] = Peer(value); } }
        public int Count { get { return Items.Count; } }
        public bool IsReadOnly { get { return Items.IsFrozen; } }
        public void Add(Timeline value) { Items.Add(Peer(value)); }
        public void Clear() { Items.Clear(); }
        public bool Contains(Timeline value) { return value != null && Items.Contains(Peer(value)); }
        public int IndexOf(Timeline value) { return value == null ? -1 : Items.IndexOf(Peer(value)); }
        public void Insert(int index, Timeline value) { Items.Insert(index, Peer(value)); }
        public bool Remove(Timeline value) { return value != null && Items.Remove(Peer(value)); }
        public void RemoveAt(int index) { Items.RemoveAt(index); }
        public void CopyTo(Timeline[] array, int index) { foreach (var value in this) array[index++] = value; }
        public global::System.Collections.Generic.IEnumerator<Timeline> GetEnumerator() { for (int i = 0; i < Count; ++i) yield return this[i]; }
        global::System.Collections.IEnumerator global::System.Collections.IEnumerable.GetEnumerator() { return GetEnumerator(); }
    }
}

namespace AppxPortable.WinRT.Xaml
{
    // Theme fades animate Opacity; their exact Windows timing is not reproduced.
    public sealed class FadeInPeer : System.Windows.Media.Animation.DoubleAnimation
    {
        public FadeInPeer()
        {
            To = 1;
            Duration = new System.Windows.Duration(System.TimeSpan.FromMilliseconds(300));
            System.Windows.Media.Animation.Storyboard.SetTargetProperty(this, new System.Windows.PropertyPath(System.Windows.UIElement.OpacityProperty));
        }

        protected override System.Windows.Freezable CreateInstanceCore() { return new FadeInPeer(); }
    }

    public sealed class FadeOutPeer : System.Windows.Media.Animation.DoubleAnimation
    {
        public FadeOutPeer()
        {
            To = 0;
            Duration = new System.Windows.Duration(System.TimeSpan.FromMilliseconds(300));
            System.Windows.Media.Animation.Storyboard.SetTargetProperty(this, new System.Windows.PropertyPath(System.Windows.UIElement.OpacityProperty));
        }

        protected override System.Windows.Freezable CreateInstanceCore() { return new FadeOutPeer(); }
    }
}

namespace AppxPortable.WinRT.Xaml
{
    // UWP animation paths: "Opacity", "(UIElement.Opacity)",
    // "(UIElement.RenderTransform).(CompositeTransform.TranslateX)". Owner types are
    // UWP names; each qualified step becomes the WPF property its wrapper maps to.
    internal static class PropertyPaths
    {
        internal static System.Windows.PropertyPath FromUwp(string path)
        {
            if (path == null) throw new System.ArgumentNullException("path");
            var steps = new System.Collections.Generic.List<string>();
            var properties = new System.Collections.Generic.List<object>();
            foreach (string step in Split(path))
            {
                if (!step.StartsWith("(", System.StringComparison.Ordinal)) { steps.Add(step); continue; }
                string inner = step.Substring(1, step.Length - 2);
                int dot = inner.LastIndexOf('.');
                if (dot < 0) throw new System.ArgumentException("Unsupported property path step: " + step, "path");
                steps.Add("(" + properties.Count + ")");
                properties.Add(Resolve(inner.Substring(0, dot), inner.Substring(dot + 1), path));
            }
            return new System.Windows.PropertyPath(string.Join(".", steps), properties.ToArray());
        }

        internal static string Text(System.Windows.PropertyPath path) { return path == null ? null : path.Path; }

        private static System.Collections.Generic.IEnumerable<string> Split(string path)
        {
            int depth = 0, start = 0;
            for (int i = 0; i < path.Length; ++i)
            {
                if (path[i] == '(') ++depth;
                else if (path[i] == ')') --depth;
                else if (path[i] == '.' && depth == 0) { yield return path.Substring(start, i - start).Trim(); start = i + 1; }
            }
            yield return path.Substring(start).Trim();
        }

        private static System.Windows.DependencyProperty Resolve(string owner, string name, string path)
        {
            string local = owner.Substring(owner.IndexOf(':') + 1);
            foreach (var pair in Peers.PeerTypes)
            {
                if (pair.Key.Name != local) continue;
                for (System.Type type = pair.Key; type != null; type = type.BaseType)
                {
                    var property = type.GetProperty(name + "Property", System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Static);
                    if (property != null) return global::Windows.UI.Xaml.DependencyProperty.WpfOf((global::Windows.UI.Xaml.DependencyProperty)property.GetValue(null, null));
                }
                // UWP-only owner members fall back to the WPF peer's own property.
                var field = pair.Value.GetField(name + "Property", System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.FlattenHierarchy);
                if (field != null) return (System.Windows.DependencyProperty)field.GetValue(null);
            }
            throw new System.ArgumentException("Unsupported property path owner: " + owner + "." + name + " in " + path, "path");
        }
    }
}
