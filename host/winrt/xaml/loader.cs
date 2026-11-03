// Source XAML through WPF's object writer. UwpSchemaContext hands out WPF's own
// XamlTypes (so templates, styles and markup extensions behave as in WPF) and
// custom ones for app types: their invoker builds the app object and yields its
// WPF peer. A node filter applies x:Class, x:Name and x:ConnectionId the UWP way.
// A plain XamlSchemaContext was tried first: WPF templates need WPF's XamlTypes.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Xaml;
using System.Xaml.Schema;

using AppxPortable.WinRT.Xaml;

namespace Windows.UI.Xaml
{
    public partial class Application
    {
        public static void LoadComponent(object component, Uri resourceLocator) { XamlLoader.Load(component, resourceLocator); }

        public static void LoadComponent(object component, Uri resourceLocator, Windows.UI.Xaml.Controls.Primitives.ComponentResourceLocation componentResourceLocation)
        {
            XamlLoader.Load(component, resourceLocator);
        }
    }
}

namespace AppxPortable.WinRT.Xaml
{
    internal static class XamlLoader
    {
        // ms-appx:/// paths resolve against the package, where the app's executable is.
        internal static string PackageRoot = AppDomain.CurrentDomain.BaseDirectory;

        internal static void Load(object component, Uri uri)
        {
            if (component == null) throw new ArgumentNullException("component");
            if (uri == null) throw new ArgumentNullException("resourceLocator");
            string path = Resolve(uri);
            using (var text = File.OpenText(path)) Load(component, text, uri.OriginalString);
        }

        internal static string Resolve(Uri uri, bool requireFile = true)
        {
            string relative = Uri.UnescapeDataString(uri.AbsolutePath).TrimStart('/');
            string current = PackageRoot;
            foreach (string segment in relative.Split(new[] { '/', '\\' }, StringSplitOptions.RemoveEmptyEntries))
            {
                string next = Path.Combine(current, segment);
                if (!File.Exists(next) && !Directory.Exists(next))
                    next = Directory.Exists(current) ? Directory.EnumerateFileSystemEntries(current)
                        .FirstOrDefault(p => string.Equals(Path.GetFileName(p), segment, StringComparison.OrdinalIgnoreCase)) : null;
                if (next == null && requireFile) throw new FileNotFoundException("XAML not found: " + uri.OriginalString, Path.Combine(PackageRoot, relative));
                current = next ?? Path.Combine(current, segment);
            }
            if (requireFile && !File.Exists(current)) throw new FileNotFoundException("XAML not found: " + uri.OriginalString, current);
            return current;
        }

        private static void Load(object component, TextReader text, string source)
        {
            var app = component as global::Windows.UI.Xaml.Application;
            var wrapper = component as global::Windows.UI.Xaml.DependencyObject;
            if (app == null && wrapper == null) throw new NotSupportedException("Cannot load XAML into " + component.GetType().FullName);
            Host.EnsureTheme();
            if (app != null) app.Loaded();
            else
            {
                var current = global::Windows.UI.Xaml.Application.Current;
                if (current != null) current.Initialize();
            }
            object root = app != null ? new ApplicationPeer() : Values.Dependency(wrapper);
            Reset(root);
            var load = new Loading(component, root, source);
            load.Run(text);
            if (app != null) app.Resources = (global::Windows.UI.Xaml.ResourceDictionary)Peers.Wrap(((ApplicationPeer)root).Resources);
        }

        private static readonly System.Runtime.CompilerServices.ConditionalWeakTable<System.Windows.ResourceDictionary, Uri> sources =
            new System.Runtime.CompilerServices.ConditionalWeakTable<System.Windows.ResourceDictionary, Uri>();

        // ResourceDictionary.Source: the package file's entries load into the dictionary.
        internal static void LoadDictionary(System.Windows.ResourceDictionary dictionary, Uri uri)
        {
            if (uri == null) throw new ArgumentNullException("value");
            Host.EnsureTheme();
            string path = Resolve(uri);
            using (var text = File.OpenText(path)) new Loading(null, dictionary, uri.OriginalString).Run(text);
            sources.Remove(dictionary);
            sources.Add(dictionary, uri);
        }

        internal static Uri SourceOf(System.Windows.ResourceDictionary dictionary)
        {
            Uri value;
            return sources.TryGetValue(dictionary, out value) ? value : null;
        }

        // A Source relative to the file naming it, as package paths are.
        private static Uri Combine(string source, string value)
        {
            if (value.Contains("://")) return new Uri(value);
            string directory = "/";
            Uri current;
            if (Uri.TryCreate(source, UriKind.Absolute, out current))
                directory = current.AbsolutePath.Substring(0, current.AbsolutePath.LastIndexOf('/') + 1);
            return new Uri("ms-appx://" + (value.StartsWith("/", StringComparison.Ordinal) ? value : directory + value));
        }

        // Loading again replaces the component's tree and names, as in UWP.
        private static void Reset(object root)
        {
            var content = root as System.Windows.Controls.ContentControl;
            if (content != null) content.ClearValue(System.Windows.Controls.ContentControl.ContentProperty);
            var panel = root as System.Windows.Controls.Panel;
            if (panel != null) panel.Children.Clear();
        }

        private sealed class Frame
        {
            internal bool Got, InDictionary, Keyed;
            internal System.Windows.ResourceDictionary Theme;
            internal XamlType Type;
            internal XamlMember Member;
            internal string Name, Source, Setting;
            internal int? Connection;
        }

        private sealed class Loading
        {
            private readonly object component, root;
            private readonly string source;
            private readonly System.Windows.NameScope names = new System.Windows.NameScope();
            private readonly Stack<Frame> frames = new Stack<Frame>();
            private Frame closing;
            private Exception failure;
            // Source dictionaries load once their own initialization has ended.
            private readonly Dictionary<object, Uri> pending = new Dictionary<object, Uri>();

            internal Loading(object component, object root, string source) { this.component = component; this.root = root; this.source = source; }

            internal void Run(TextReader text)
            {
                var dependency = root as System.Windows.DependencyObject;
                if (dependency != null) System.Windows.NameScope.SetNameScope(dependency, names);
                using (var xml = System.Xml.XmlReader.Create(text))
                    Run(new XamlXmlReader(xml, UwpSchemaContext.Instance, new XamlXmlReaderSettings { ProvideLineInfo = true }));
            }

            // Builds the nodes into the root, or into a new object it returns.
            internal object Run(XamlReader reader)
            {
                var context = UwpSchemaContext.Instance;
                var settings = new XamlObjectWriterSettings { RootObjectInstance = root };
                // Package dictionaries contain images that are never used. WPF's
                // own delayed creation keeps decoding at first use, including its
                // normal error path, rather than aborting the whole dictionary.
                settings.AfterBeginInitHandler = (sender, args) =>
                {
                    var bitmap = args.Instance as System.Windows.Media.Imaging.BitmapImage;
                    if (bitmap != null) bitmap.CreateOptions |= System.Windows.Media.Imaging.BitmapCreateOptions.DelayCreation;
                };
                settings.AfterPropertiesHandler = (sender, args) => Closed(args.Instance);
                settings.AfterEndInitHandler = (sender, args) =>
                {
                    Uri uri;
                    if (!pending.TryGetValue(args.Instance, out uri)) return;
                    pending.Remove(args.Instance);
                    LoadDictionary((System.Windows.ResourceDictionary)args.Instance, uri);
                };
                var writer = new XamlObjectWriter(context, settings);
                try { Transform(reader, writer); writer.Close(); }
                catch (Exception error)
                {
                    if (failure != null) ExceptionDispatchInfo.Capture(failure).Throw();
                    if (error is XamlException && error.Message.StartsWith(source, StringComparison.Ordinal)) throw;
                    throw new XamlException(source + ": " + Describe(error), error);
                }
                return writer.Result;
            }

            // The writer wraps the cause (a missing resource key, a setter's error).
            private static string Describe(Exception error)
            {
                var parts = new List<string>();
                for (var current = error; current != null; current = current.InnerException)
                    if (!parts.Contains(current.Message)) parts.Add(current.Message);
                return string.Join(" -> ", parts);
            }

            private string Where(XamlReader reader)
            {
                var line = reader as IXamlLineInfo;
                return source + (line != null && line.HasLineInfo ? " line " + line.LineNumber : "");
            }

            private void Transform(XamlReader reader, XamlObjectWriter writer)
            {
                reader.Read();
                while (!reader.IsEof)
                {
                    switch (reader.NodeType)
                    {
                        case XamlNodeType.StartObject:
                            if (reader.Type.IsUnknown)
                                throw new XamlException(Where(reader) + ": no WPF mapping for type " + reader.Type.PreferredXamlNamespace + ":" + reader.Type.Name);
                            frames.Push(new Frame { Type = reader.Type, InDictionary = InDictionary() });
                            break;
                        case XamlNodeType.GetObject:
                            frames.Push(new Frame { Got = true, Type = frames.Count == 0 || frames.Peek().Member == null ? null : frames.Peek().Member.Type });
                            break;
                        case XamlNodeType.EndMember:
                            if (frames.Count != 0) frames.Peek().Member = null;
                            break;
                        case XamlNodeType.EndObject:
                            var frame = frames.Pop();
                            closing = frame.Got ? null : frame;
                            break;
                        case XamlNodeType.Value:
                            if (frames.Count != 0)
                            {
                                var valueFrame = frames.Peek();
                                if (valueFrame.Type != null && valueFrame.Type.UnderlyingType == typeof(System.Windows.Setter) && valueFrame.Member.Name == "Property")
                                    valueFrame.Setting = reader.Value as string;
                                var image = reader.Value as string;
                                if (image != null && ImageSource(valueFrame.Member))
                                {
                                    Uri uri;
                                    if (!Uri.TryCreate(image, UriKind.Absolute, out uri) || uri.Scheme == "ms-appx" || image.StartsWith("/", StringComparison.Ordinal))
                                    {
                                        writer.WriteValue(new Uri(Resolve(Combine(source, image), false), UriKind.Absolute).AbsoluteUri);
                                        reader.Read();
                                        continue;
                                    }
                                }
                                // WPF has no inside-track tick layout. Its stock
                                // visible tick bar is the closest display mapping.
                                if (Equals(reader.Value, "Inline") && valueFrame.Member != null &&
                                    valueFrame.Member.Type.UnderlyingType == typeof(System.Windows.Controls.Primitives.TickPlacement))
                                {
                                    writer.WriteValue("BottomRight");
                                    reader.Read();
                                    continue;
                                }
                                if (Equals(reader.Value, "SemiLight") && FontWeight(valueFrame))
                                {
                                    writer.WriteValue("350");
                                    reader.Read();
                                    continue;
                                }
                                // Package fonts ("/Fonts/a.otf#Face", ms-appx:///...) name files in
                                // the package; WPF resolves them against a BaseUri this loader does
                                // not have, so they become absolute file URIs.
                                if (reader.Value is string && FontFamily(valueFrame))
                                {
                                    writer.WriteValue(PackageFonts((string)reader.Value));
                                    reader.Read();
                                    continue;
                                }
                            }
                            break;
                        case XamlNodeType.StartMember:
                            var member = reader.Member;
                            if (frames.Count != 0) frames.Peek().Member = member;
                            if (member == XamlLanguage.Key) frames.Peek().Keyed = true;
                            if (member == XamlLanguage.Class) { reader.Skip(); continue; }
                            if (member == XamlLanguage.ConnectionId)
                            {
                                reader.Read();
                                frames.Peek().Connection = Convert.ToInt32(reader.Value, System.Globalization.CultureInfo.InvariantCulture);
                                reader.Read(); reader.Read();
                                continue;
                            }
                            if (member.Name == "Source" && member.DeclaringType != null && member.DeclaringType.UnderlyingType != null &&
                                typeof(System.Windows.ResourceDictionary).IsAssignableFrom(member.DeclaringType.UnderlyingType))
                            {
                                reader.Read();
                                frames.Peek().Source = (string)reader.Value;
                                reader.Read(); reader.Read();
                                continue;
                            }
                            if (member.Name == "ThemeDictionaries" && Dictionary(member.DeclaringType))
                            {
                                frames.Peek().Theme = Theme(reader);
                                continue;
                            }
                            // States convert UWP string key frames (see UwpVisualStateManager).
                            if (member.IsAttachable && member.Name == "VisualStateGroups" && member.DeclaringType != null &&
                                member.DeclaringType.UnderlyingType == typeof(System.Windows.VisualStateManager))
                            {
                                writer.WriteStartMember(member.DeclaringType.GetAttachableMember("CustomVisualStateManager"));
                                writer.WriteValue(UwpVisualStateManager.Instance);
                                writer.WriteEndMember();
                            }
                            if (member.IsUnknown)
                                throw new XamlException(Where(reader) + ": no WPF mapping for member " +
                                    (member.DeclaringType == null ? "" : member.DeclaringType.Name + ".") + member.Name);
                            if (member == XamlLanguage.Name || (frames.Count != 0 && IsNameAlias(member)))
                            {
                                var named = frames.Peek();
                                writer.WriteNode(reader);
                                reader.Read();
                                if (reader.NodeType != XamlNodeType.Value) continue;
                                named.Name = (string)reader.Value;
                                writer.WriteNode(reader);
                                reader.Read();
                                writer.WriteNode(reader);   // EndMember
                                reader.Read();
                                named.Member = null;
                                // UWP keys a dictionary entry that has only x:Name by that name.
                                if (named.InDictionary && !named.Keyed)
                                {
                                    named.Keyed = true;
                                    writer.WriteStartMember(XamlLanguage.Key);
                                    writer.WriteValue(named.Name);
                                    writer.WriteEndMember();
                                }
                                continue;
                            }
                            break;
                    }
                    writer.WriteNode(reader);
                    reader.Read();
                }
            }

            private static bool ImageSource(XamlMember member)
            {
                if (member == null || member.Type.UnderlyingType == null) return false;
                return typeof(System.Windows.Media.ImageSource).IsAssignableFrom(member.Type.UnderlyingType) ||
                    member.Name == "UriSource" && member.DeclaringType != null && member.DeclaringType.UnderlyingType != null &&
                    typeof(System.Windows.Media.Imaging.BitmapImage).IsAssignableFrom(member.DeclaringType.UnderlyingType);
            }

            // UWP names the OpenType weight 350; WPF accepts its numeric spelling.
            private static bool FontWeight(Frame frame)
            {
                return frame.Type != null && (frame.Type.UnderlyingType == typeof(System.Windows.FontWeight) ||
                    frame.Member != null && frame.Member.Type.UnderlyingType == typeof(System.Windows.FontWeight) ||
                    frame.Type.UnderlyingType == typeof(System.Windows.Setter) && frame.Member != null && frame.Member.Name == "Value" &&
                    (frame.Setting == "FontWeight" || frame.Setting != null && frame.Setting.EndsWith(".FontWeight", StringComparison.Ordinal)));
            }

            private static bool FontFamily(Frame frame)
            {
                return frame.Type != null && (frame.Type.UnderlyingType == typeof(System.Windows.Media.FontFamily) ||
                    frame.Member != null && frame.Member.Type.UnderlyingType == typeof(System.Windows.Media.FontFamily) ||
                    frame.Type.UnderlyingType == typeof(System.Windows.Setter) && frame.Member != null && frame.Member.Name == "Value" &&
                    (frame.Setting == "FontFamily" || frame.Setting != null && frame.Setting.EndsWith(".FontFamily", StringComparison.Ordinal)));
            }

            // Each comma-separated family: package-rooted file references become file URIs.
            internal static string PackageFonts(string value)
            {
                var families = value.Split(',');
                for (int i = 0; i < families.Length; ++i)
                {
                    string family = families[i].Trim(), path = null;
                    if (family.StartsWith("ms-appx:///", StringComparison.OrdinalIgnoreCase)) path = family.Substring("ms-appx:///".Length);
                    else if (family.StartsWith("/", StringComparison.Ordinal)) path = family.Substring(1);
                    if (path == null) continue;
                    int face = path.IndexOf('#');
                    string file = face < 0 ? path : path.Substring(0, face);
                    var uri = new Uri(System.IO.Path.Combine(PackageRoot, Uri.UnescapeDataString(file).Replace('/', System.IO.Path.DirectorySeparatorChar)));
                    families[i] = uri.AbsoluteUri + (face < 0 ? "" : path.Substring(face));
                }
                return string.Join(",", families);
            }

            private static bool Dictionary(XamlType type)
            {
                return type != null && type.UnderlyingType != null && typeof(System.Windows.ResourceDictionary).IsAssignableFrom(type.UnderlyingType);
            }

            // ResourceDictionary.ThemeDictionaries: only the dark theme exists here, so
            // its dictionary (Dark, else Default) is built and the rest are skipped.
            private System.Windows.ResourceDictionary Theme(XamlReader reader)
            {
                var nodes = new XamlNodeList(reader.SchemaContext);
                // Reading the subtree to its end leaves the reader on the following node.
                using (var subtree = reader.ReadSubtree()) XamlServices.Transform(subtree, nodes.Writer, false);
                nodes.Writer.Close();
                List<XamlNodeList> entries = new List<XamlNodeList>();
                var keys = new List<string>();
                XamlNodeList current = null;
                int depth = 0;
                string key = null;
                var all = nodes.GetReader();
                while (all.Read())
                {
                    bool start = all.NodeType == XamlNodeType.StartObject || all.NodeType == XamlNodeType.GetObject;
                    if (start && ++depth == 1 && all.NodeType == XamlNodeType.StartObject) { current = new XamlNodeList(reader.SchemaContext); key = null; }
                    if (current != null && depth >= 1 && all.NodeType == XamlNodeType.StartMember && all.Member == XamlLanguage.Key && depth == 1)
                    {
                        all.Read(); key = all.Value as string; all.Read();
                        continue;
                    }
                    if (current != null) current.Writer.WriteNode(all);
                    if (all.NodeType == XamlNodeType.EndObject && --depth == 0 && current != null)
                    {
                        current.Writer.Close();
                        entries.Add(current); keys.Add(key);
                        current = null;
                    }
                }
                int chosen = keys.IndexOf("Dark");
                if (chosen < 0) chosen = keys.IndexOf("Default");
                if (chosen < 0) return null;
                return (System.Windows.ResourceDictionary)new Loading(component, null, source).Run(entries[chosen].GetReader());
            }

            // The object starting now is an entry of the dictionary being filled.
            private bool InDictionary()
            {
                if (frames.Count == 0) return false;
                var parent = frames.Peek();
                return parent.Member == XamlLanguage.Items && parent.Type != null && parent.Type.IsDictionary;
            }

            private static bool IsNameAlias(XamlMember member)
            {
                return member.DeclaringType != null && member.DeclaringType.GetAliasedProperty(XamlLanguage.Name) == member;
            }

            // After an element's properties: its name joins the component's namescope,
            // then its connection reaches the component.
            private void Closed(object instance)
            {
                var frame = closing;
                closing = null;
                if (frame == null || failure != null) return;
                // Looked up after the dictionary's own and merged entries, as in UWP.
                if (frame.Theme != null) ((System.Windows.ResourceDictionary)instance).MergedDictionaries.Insert(0, frame.Theme);
                if (frame.Source != null) pending[instance] = Combine(source, frame.Source);
                if (frame.Name != null && names.FindName(frame.Name) == null) names.RegisterName(frame.Name, instance);
                if (frame.Connection == null) return;
                var connector = component as global::Windows.UI.Xaml.Markup.IComponentConnector;
                if (connector == null) throw new XamlException(source + ": x:ConnectionId without a component connector");
                object target = Values.FromWpf(instance, typeof(object));
                try { Host.Callback(() => connector.Connect(frame.Connection.Value, target)); }
                catch (Exception error) { failure = error; throw; }
            }
        }
    }

    // An Application's XAML root: it collects the resources UWP keeps on Application.
    public sealed class ApplicationPeer : System.Windows.FrameworkElement
    {
        // Only the dark theme is provided (theme.xaml).
        public string RequestedTheme
        {
            get { return "Dark"; }
            set { if (value != "Dark") throw new NotSupportedException("Only the dark theme is provided, not " + value); }
        }
    }

    internal sealed class UwpSchemaContext : XamlSchemaContext
    {
        private const string Presentation = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";
        private static UwpSchemaContext instance;
        private static readonly XamlSchemaContext wpf = System.Windows.Markup.XamlReader.GetWpfSchemaContext();
        private readonly Dictionary<string, Type> uwp = new Dictionary<string, Type>(StringComparer.Ordinal);
        private readonly Dictionary<string, XamlType> aliased = new Dictionary<string, XamlType>(StringComparer.Ordinal);
        private readonly Dictionary<string, XamlType> app = new Dictionary<string, XamlType>(StringComparer.Ordinal);

        internal static UwpSchemaContext Instance { get { return instance ?? (instance = new UwpSchemaContext()); } }

        private UwpSchemaContext() : base(new[] { typeof(System.Windows.FrameworkElement).Assembly })
        {
            CompatibilityProperties.Initialize();
            // UWP element names: the wrapper types with a WPF peer, by simple name.
            foreach (var pair in Peers.PeerTypes)
                if (!uwp.ContainsKey(pair.Key.Name))
                {
                    uwp.Add(pair.Key.Name, pair.Value);
                    var names = Peers.Aliases(pair.Key);
                    if (names.Count != 0) aliased.Add(pair.Key.Name, new PeerXamlType(wpf.GetXamlType(pair.Value), this, names));
                }
            uwp["Application"] = typeof(ApplicationPeer);
            uwp["TransitionCollection"] = typeof(TransitionCollectionPeer);
        }

        public override XamlDirective GetXamlDirective(string xamlNamespace, string name) { return wpf.GetXamlDirective(xamlNamespace, name); }

        protected override XamlType GetXamlType(string xamlNamespace, string name, params XamlType[] typeArguments)
        {
            Type peer;
            XamlType known;
            if (xamlNamespace == Presentation && aliased.TryGetValue(name, out known)) return known;
            if (xamlNamespace == Presentation && (typeArguments == null || typeArguments.Length == 0) && uwp.TryGetValue(name, out peer))
                return wpf.GetXamlType(peer);
            if (xamlNamespace == Presentation && (name == "Binding" || name == "BindingExtension"))
                return wpf.GetXamlType(typeof(UwpBinding));
            // Theme resources are fixed (dark theme), so a static lookup serves.
            if (xamlNamespace == Presentation && (name == "ThemeResource" || name == "ThemeResourceExtension"))
                return wpf.GetXamlType(typeof(System.Windows.StaticResourceExtension));
            if (xamlNamespace.StartsWith("using:", StringComparison.Ordinal)) return App(xamlNamespace.Substring(6) + "." + name);
            return wpf.GetXamlType(new XamlTypeName(xamlNamespace, name,
                typeArguments == null ? null : typeArguments.Select(t => new XamlTypeName(t))));
        }

        // An app type, from the app's XAML metadata as UWP finds it, else its assemblies.
        private XamlType App(string fullName)
        {
            XamlType value;
            if (app.TryGetValue(fullName, out value)) return value;
            var provider = global::Windows.UI.Xaml.Application.Current as global::Windows.UI.Xaml.Markup.IXamlMetadataProvider;
            var metadata = provider == null ? null : provider.GetXamlType(fullName);
            Type type = metadata != null ? metadata.UnderlyingType : AppDomain.CurrentDomain.GetAssemblies()
                .Select(a => a.GetType(fullName, false)).FirstOrDefault(t => t != null);
            if (type == null) return null;
            Type wrapper = type;
            while (wrapper != null && Peers.PeerType(wrapper) == null) wrapper = wrapper.BaseType;
            value = wrapper == null ? (XamlType)new AppObjectType(type, metadata, this) : new AppXamlType(type, wpf.GetXamlType(Peers.PeerType(wrapper)), metadata, this);
            app.Add(fullName, value);
            return value;
        }
    }

    // An app class derived from a UWP wrapper: WPF sees its peer, with the peer's
    // WPF members (dependency properties, so bindings and styles apply to them).
    internal sealed class AppXamlType : XamlType
    {
        private readonly Type type;
        private readonly XamlType peer;
        private readonly global::Windows.UI.Xaml.Markup.IXamlType metadata;

        internal AppXamlType(Type type, XamlType peer, global::Windows.UI.Xaml.Markup.IXamlType metadata, XamlSchemaContext context)
            : base(peer.UnderlyingType, context)
        {
            this.type = type; this.peer = peer; this.metadata = metadata;
        }

        protected override XamlTypeInvoker LookupInvoker() { return new Creator(this); }
        protected override XamlMember LookupContentProperty() { return peer.ContentProperty; }
        protected override XamlMember LookupAliasedProperty(XamlDirective directive) { return peer.GetAliasedProperty(directive); }
        protected override XamlMember LookupAttachableMember(string name) { return peer.GetAttachableMember(name); }
        protected override IEnumerable<XamlMember> LookupAllMembers() { return peer.GetAllMembers(); }

        protected override XamlMember LookupMember(string name, bool skipReadOnlyCheck)
        {
            var member = peer.GetMember(name);
            if (member != null) return member;
            var property = GameProperties.Find(type, name);
            return property == null ? null : new AppMember(name, this, property);
        }

        private sealed class Creator : XamlTypeInvoker
        {
            private readonly AppXamlType owner;

            internal Creator(AppXamlType owner) : base(owner) { this.owner = owner; }

            public override object CreateInstance(object[] arguments)
            {
                object value = owner.metadata != null ? owner.metadata.ActivateInstance() : Activator.CreateInstance(owner.type);
                return Peers.Of((global::Windows.UI.Xaml.DependencyObject)value);
            }
        }
    }

    // A UWP element whose mapped members are named differently in WPF (ProgressRing
    // IsActive on a ProgressBar's IsIndeterminate): WPF's members under UWP names.
    internal sealed class PeerXamlType : XamlType
    {
        private readonly XamlType peer;
        private readonly Dictionary<string, System.Windows.DependencyProperty> names;

        internal PeerXamlType(XamlType peer, XamlSchemaContext context, Dictionary<string, System.Windows.DependencyProperty> names) : base(peer.UnderlyingType, context)
        {
            this.peer = peer; this.names = names;
        }

        protected override XamlTypeInvoker LookupInvoker() { return peer.Invoker; }
        protected override XamlMember LookupContentProperty()
        {
            return peer.ContentProperty ?? (peer.UnderlyingType == typeof(System.Windows.Controls.ContentPresenter) ? peer.GetMember("Content") : null);
        }
        protected override XamlMember LookupAliasedProperty(XamlDirective directive) { return peer.GetAliasedProperty(directive); }
        protected override XamlMember LookupAttachableMember(string name)
        {
            System.Windows.DependencyProperty property;
            return names.TryGetValue(name, out property) ? System.Windows.Markup.XamlReader.GetWpfSchemaContext()
                .GetXamlType(property.OwnerType).GetAttachableMember(property.Name) ?? peer.GetAttachableMember(name) : peer.GetAttachableMember(name);
        }
        protected override IEnumerable<XamlMember> LookupAllMembers() { return peer.GetAllMembers(); }

        protected override XamlMember LookupMember(string name, bool skipReadOnlyCheck)
        {
            System.Windows.DependencyProperty property;
            if (!names.TryGetValue(name, out property)) return peer.GetMember(name);
            return peer.GetMember(property.Name) ?? System.Windows.Markup.XamlReader.GetWpfSchemaContext()
                .GetXamlType(property.OwnerType).GetAttachableMember(property.Name);
        }
    }

    // Any other app class (converters, view models): created as the app's XAML
    // metadata creates it, which reaches internal types too.
    internal sealed class AppObjectType : XamlType
    {
        private readonly global::Windows.UI.Xaml.Markup.IXamlType metadata;

        internal AppObjectType(Type type, global::Windows.UI.Xaml.Markup.IXamlType metadata, XamlSchemaContext context) : base(type, context)
        {
            this.metadata = metadata;
        }

        protected override bool LookupIsPublic() { return true; }
        protected override bool LookupIsConstructible() { return true; }
        protected override XamlTypeInvoker LookupInvoker() { return new Creator(this); }

        private sealed class Creator : XamlTypeInvoker
        {
            private readonly AppObjectType owner;

            internal Creator(AppObjectType owner) : base(owner) { this.owner = owner; }

            public override object CreateInstance(object[] arguments)
            {
                return owner.metadata != null ? owner.metadata.ActivateInstance() : Activator.CreateInstance(owner.UnderlyingType, true);
            }
        }
    }

    // A dependency property the app registered, set from markup on its peer.
    internal sealed class AppMember : XamlMember
    {
        private readonly global::Windows.UI.Xaml.DependencyProperty property;

        internal AppMember(string name, XamlType owner, global::Windows.UI.Xaml.DependencyProperty property) : base(name, owner, false)
        {
            this.property = property;
        }

        protected override XamlType LookupType() { return DeclaringType.SchemaContext.GetXamlType(property.Type); }
        protected override bool LookupIsUnknown() { return false; }
        protected override XamlMemberInvoker LookupInvoker() { return new Accessor(this); }

        private sealed class Accessor : XamlMemberInvoker
        {
            private readonly AppMember owner;

            internal Accessor(AppMember owner) : base(owner) { this.owner = owner; }

            public override object GetValue(object instance) { return Peers.Wrap(instance).GetValue(owner.property); }
            public override void SetValue(object instance, object value) { Peers.Wrap(instance).SetValue(owner.property, value); }
        }
    }
}
