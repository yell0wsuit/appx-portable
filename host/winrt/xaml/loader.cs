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

        internal static string Resolve(Uri uri)
        {
            string relative = Uri.UnescapeDataString(uri.AbsolutePath).TrimStart('/');
            string current = PackageRoot;
            foreach (string segment in relative.Split(new[] { '/', '\\' }, StringSplitOptions.RemoveEmptyEntries))
            {
                string next = Path.Combine(current, segment);
                if (!File.Exists(next) && !Directory.Exists(next))
                    next = Directory.Exists(current) ? Directory.EnumerateFileSystemEntries(current)
                        .FirstOrDefault(p => string.Equals(Path.GetFileName(p), segment, StringComparison.OrdinalIgnoreCase)) : null;
                if (next == null) throw new FileNotFoundException("XAML not found: " + uri.OriginalString, Path.Combine(PackageRoot, relative));
                current = next;
            }
            if (!File.Exists(current)) throw new FileNotFoundException("XAML not found: " + uri.OriginalString, current);
            return current;
        }

        private static void Load(object component, TextReader text, string source)
        {
            var app = component as global::Windows.UI.Xaml.Application;
            var wrapper = component as global::Windows.UI.Xaml.DependencyObject;
            if (app == null && wrapper == null) throw new NotSupportedException("Cannot load XAML into " + component.GetType().FullName);
            Host.EnsureTheme();
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
            internal XamlType Type;
            internal XamlMember Member;
            internal string Name, Source;
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
                var context = UwpSchemaContext.Instance;
                var dependency = root as System.Windows.DependencyObject;
                if (dependency != null) System.Windows.NameScope.SetNameScope(dependency, names);
                var settings = new XamlObjectWriterSettings { RootObjectInstance = root };
                settings.AfterPropertiesHandler = (sender, args) => Closed(args.Instance);
                settings.AfterEndInitHandler = (sender, args) =>
                {
                    Uri uri;
                    if (!pending.TryGetValue(args.Instance, out uri)) return;
                    pending.Remove(args.Instance);
                    LoadDictionary((System.Windows.ResourceDictionary)args.Instance, uri);
                };
                var writer = new XamlObjectWriter(context, settings);
                using (var xml = System.Xml.XmlReader.Create(text))
                {
                    var reader = new XamlXmlReader(xml, context, new XamlXmlReaderSettings { ProvideLineInfo = true });
                    try { Transform(reader, writer); writer.Close(); }
                    catch (Exception error)
                    {
                        if (failure != null) ExceptionDispatchInfo.Capture(failure).Throw();
                        if (error is XamlException && error.Message.StartsWith(source, StringComparison.Ordinal)) throw;
                        throw new XamlException(source + ": " + Describe(error), error);
                    }
                }
            }

            // The writer wraps the cause (a missing resource key, a setter's error).
            private static string Describe(Exception error)
            {
                var parts = new List<string>();
                for (var current = error; current != null; current = current.InnerException)
                    if (!parts.Contains(current.Message)) parts.Add(current.Message);
                return string.Join(" -> ", parts);
            }

            private string Where(XamlXmlReader reader)
            {
                var line = (IXamlLineInfo)reader;
                return source + (line.HasLineInfo ? " line " + line.LineNumber : "");
            }

            private void Transform(XamlXmlReader reader, XamlObjectWriter writer)
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
            // UWP element names: the wrapper types with a WPF peer, by simple name.
            foreach (var pair in Peers.PeerTypes)
                if (!uwp.ContainsKey(pair.Key.Name))
                {
                    uwp.Add(pair.Key.Name, pair.Value);
                    var names = Peers.Aliases(pair.Key);
                    if (names.Count != 0) aliased.Add(pair.Key.Name, new PeerXamlType(wpf.GetXamlType(pair.Value), this, names));
                }
            uwp["Application"] = typeof(ApplicationPeer);
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
        private readonly Dictionary<string, string> names;

        internal PeerXamlType(XamlType peer, XamlSchemaContext context, Dictionary<string, string> names) : base(peer.UnderlyingType, context)
        {
            this.peer = peer; this.names = names;
        }

        protected override XamlTypeInvoker LookupInvoker() { return peer.Invoker; }
        protected override XamlMember LookupContentProperty() { return peer.ContentProperty; }
        protected override XamlMember LookupAliasedProperty(XamlDirective directive) { return peer.GetAliasedProperty(directive); }
        protected override XamlMember LookupAttachableMember(string name) { return peer.GetAttachableMember(name); }
        protected override IEnumerable<XamlMember> LookupAllMembers() { return peer.GetAllMembers(); }

        protected override XamlMember LookupMember(string name, bool skipReadOnlyCheck)
        {
            string wpfName;
            return peer.GetMember(names.TryGetValue(name, out wpfName) ? wpfName : name);
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
