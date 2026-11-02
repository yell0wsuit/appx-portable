// Synthetic WinMD: its independent native ABI is in component_projection_fixture.cpp.
using System;
using System.Linq;

using Mono.Cecil;
class ComponentMetadata
{
    static ModuleDefinition module;
    static string space = "Example.Component";
    static TypeDefinition Type(string name, TypeAttributes flags, TypeReference parent)
    {
        var type = new TypeDefinition(space, name, flags | TypeAttributes.WindowsRuntime, parent);
        module.Types.Add(type); return type;
    }
    static void Attribute(ICustomAttributeProvider owner, string name, TypeReference parameter, object value)
    {
        var attribute = new TypeReference("Windows.Foundation.Metadata", name + "Attribute", module, module.TypeSystem.CoreLibrary);
        var ctor = new MethodReference(".ctor", module.TypeSystem.Void, attribute) { HasThis = true };
        ctor.Parameters.Add(new ParameterDefinition(parameter));
        var a = new CustomAttribute(ctor); a.ConstructorArguments.Add(new CustomAttributeArgument(parameter, value));
        owner.CustomAttributes.Add(a);
    }
    static MethodDefinition Method(TypeDefinition owner, string name, TypeReference result, params TypeReference[] arguments)
    {
        var method = new MethodDefinition(name, MethodAttributes.Public | MethodAttributes.Virtual | MethodAttributes.NewSlot | MethodAttributes.Abstract, result);
        for (int i = 0; i < arguments.Length; i++) method.Parameters.Add(new ParameterDefinition("p" + i, ParameterAttributes.In, arguments[i]));
        owner.Methods.Add(method); return method;
    }
    static void Default(TypeDefinition type, TypeDefinition iface)
    {
        var implementation = new InterfaceImplementation(iface);
        var attr = new TypeReference("Windows.Foundation.Metadata", "DefaultAttribute", module, module.TypeSystem.CoreLibrary);
        implementation.CustomAttributes.Add(new CustomAttribute(new MethodReference(".ctor", module.TypeSystem.Void, attr) { HasThis = true }));
        type.Interfaces.Add(implementation);
    }
    static int External(string path)
    {
        using (var assembly = AssemblyDefinition.CreateAssembly(new AssemblyNameDefinition("Example.External", new Version(255, 255, 255, 255)), "Example.External.winmd", ModuleKind.Dll))
        {
            module = assembly.MainModule; space = "Example.External";
            var ts = module.TypeSystem;
            var iface = Type("INode", TypeAttributes.Interface | TypeAttributes.Abstract | TypeAttributes.NotPublic, null);
            Attribute(iface, "Guid", ts.String, "239635fc-88f0-4fd6-80b1-b21ac63c939a");
            Method(iface, "Read", ts.Int32);
            var node = Type("Node", TypeAttributes.Public | TypeAttributes.Sealed, ts.Object);
            Default(node, iface); Attribute(node, "Activatable", ts.UInt32, (uint)1);
            var ctor = Method(node, ".ctor", ts.Void); ctor.Attributes = MethodAttributes.Public | MethodAttributes.SpecialName | MethodAttributes.RTSpecialName;
            Method(node, "Read", ts.Int32);
            var mode = Type("Mode", TypeAttributes.Public | TypeAttributes.Sealed, module.ImportReference(typeof(Enum)));
            mode.Fields.Add(new FieldDefinition("value__", FieldAttributes.Public | FieldAttributes.SpecialName | FieldAttributes.RTSpecialName, ts.Int32));
            mode.Fields.Add(new FieldDefinition("First", FieldAttributes.Public | FieldAttributes.Static | FieldAttributes.Literal | FieldAttributes.HasDefault, mode) { Constant = 7 });
            mode.Fields.Add(new FieldDefinition("Second", FieldAttributes.Public | FieldAttributes.Static | FieldAttributes.Literal | FieldAttributes.HasDefault, mode) { Constant = 19 });
            assembly.Write(path);
        }
        return 0;
    }
    static int Main(string[] args)
    {
        if (args.Length > 1 && args[1] == "external") return External(args[0]);
        if (args[0] == "--bad-projection-iid")
        {
            using (var input = AssemblyDefinition.ReadAssembly(args[1]))
            {
                var descriptor = input.MainModule.Types.Single(t => t.FullName == "Example.External.Node").CustomAttributes.Single(a => a.AttributeType.Name == "ProjectionTypeAttribute");
                descriptor.ConstructorArguments[0] = new CustomAttributeArgument(input.MainModule.TypeSystem.String, "6b043574-7239-4c43-bbb1-42822f4451fb");
                input.Write(args[2]);
            }
            return 0;
        }
        if (args[0] == "--bad-reference")
        {
            using (var input = AssemblyDefinition.ReadAssembly(args[1]))
            {
                input.MainModule.AssemblyReferences.Single(r => r.Name == "Example.Component").Version = new Version(1, 0, 0, 0);
                input.Write(args[2]);
            }
            return 0;
        }
        using (var assembly = AssemblyDefinition.CreateAssembly(new AssemblyNameDefinition("Example.Component", new Version(255, 255, 255, 255)), "Example.Component.winmd", ModuleKind.Dll))
        {
            module = assembly.MainModule; module.Kind = ModuleKind.Windows;
            var ts = module.TypeSystem;
            var handler = Type("Changed", TypeAttributes.Public | TypeAttributes.Sealed, module.ImportReference(typeof(MulticastDelegate)));
            Attribute(handler, "Guid", ts.String, "723d9b38-bb73-463e-adc8-25b46f56121a");
            Method(handler, "Invoke", ts.String, ts.Int32, ts.Boolean, ts.String);
            var iface = Type("IEngine", TypeAttributes.NotPublic | TypeAttributes.Interface | TypeAttributes.Abstract, null);
            Attribute(iface, "Guid", ts.String, "2fc62e47-e1ce-471d-9098-014b39ca938e");
            var token = new TypeReference("Windows.Foundation", "EventRegistrationToken", module, ts.CoreLibrary, true);
            Method(iface, "Echo", ts.Int32, ts.Int32);
            Method(iface, "SetState", ts.Void, ts.Boolean, ts.Double, ts.UInt32);
            Method(iface, "GetState", ts.Double);
            Method(iface, "EchoText", ts.String, ts.String);
            Method(iface, "SumBytes", ts.UInt32, new ArrayType(ts.Byte));
            var add = Method(iface, "add_Changed", token, handler);
            var remove = Method(iface, "remove_Changed", ts.Void, token);
            Method(iface, "Raise", ts.String, ts.Int32, ts.Boolean, ts.String);
            Method(iface, "Fail", ts.Void);
            iface.Events.Add(new EventDefinition("Changed", EventAttributes.None, handler) { AddMethod = add, RemoveMethod = remove });
            TypeDefinition externalHandler = null;
            if (args.Length > 1 && args[1].StartsWith("cross", StringComparison.Ordinal))
            {
                var reference = new AssemblyNameReference("Example.External", new Version(255, 255, 255, 255));
                module.AssemblyReferences.Add(reference);
                if (args[1] == "cross-version") reference.Version = new Version(1, 0, 0, 0);
                if (args[1] == "cross-culture") reference.Culture = "fr";
                if (args[1] == "cross-token") reference.PublicKeyToken = new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 };
                var node = new TypeReference("Example.External", "Node", module, reference);
                var mode = new TypeReference("Example.External", "Mode", module, reference, true);
                externalHandler = Type("ExternalChanged", TypeAttributes.Public | TypeAttributes.Sealed, module.ImportReference(typeof(MulticastDelegate)));
                Attribute(externalHandler, "Guid", ts.String, "45684efa-c695-4027-8fc4-51bf12a079ac");
                Method(externalHandler, "Invoke", ts.Void, node);
                Method(iface, "SetNode", ts.Void, node);
                Method(iface, "GetNode", node);
                Method(iface, "NextMode", mode, mode);
                var externalAdd = Method(iface, "add_ExternalChanged", token, externalHandler);
                var externalRemove = Method(iface, "remove_ExternalChanged", ts.Void, token);
                Method(iface, "RaiseExternal", ts.Void);
                iface.Events.Add(new EventDefinition("ExternalChanged", EventAttributes.None, externalHandler) { AddMethod = externalAdd, RemoveMethod = externalRemove });
            }
            var engine = Type("Engine", TypeAttributes.Public | TypeAttributes.Sealed, ts.Object);
            var implementation = new InterfaceImplementation(iface);
            // DefaultAttribute has no arguments.
            var defaultAttribute = new TypeReference("Windows.Foundation.Metadata", "DefaultAttribute", module, ts.CoreLibrary);
            implementation.CustomAttributes.Add(new CustomAttribute(new MethodReference(".ctor", ts.Void, defaultAttribute) { HasThis = true }));
            engine.Interfaces.Add(implementation);
            Attribute(engine, "Activatable", ts.UInt32, (uint)1);
            var ctor = Method(engine, ".ctor", ts.Void); ctor.Attributes = MethodAttributes.Public | MethodAttributes.SpecialName | MethodAttributes.RTSpecialName;
            foreach (var method in iface.Methods)
            {
                var copy = Method(engine, method.Name, method.ReturnType);
                foreach (var p in method.Parameters) copy.Parameters.Add(new ParameterDefinition(p.Name, p.Attributes, p.ParameterType));
            }
            engine.Events.Add(new EventDefinition("Changed", EventAttributes.None, handler) { AddMethod = engine.Methods[6], RemoveMethod = engine.Methods[7] });
            if (externalHandler != null)
                engine.Events.Add(new EventDefinition("ExternalChanged", EventAttributes.None, externalHandler)
                {
                    AddMethod = engine.Methods.Single(m => m.Name == "add_ExternalChanged"),
                    RemoveMethod = engine.Methods.Single(m => m.Name == "remove_ExternalChanged")
                });
            // Gauge(string): activated through its factory interface, not ActivateInstance.
            var gaugeInterface = Type("IGauge", TypeAttributes.NotPublic | TypeAttributes.Interface | TypeAttributes.Abstract, null);
            Attribute(gaugeInterface, "Guid", ts.String, "8c1f2a90-6d3e-4b57-9e21-3a4c5d6e7f80");
            var getName = Method(gaugeInterface, "get_Name", ts.String);
            gaugeInterface.Properties.Add(new PropertyDefinition("Name", PropertyAttributes.None, ts.String) { GetMethod = getName });
            var gauge = Type("Gauge", TypeAttributes.Public | TypeAttributes.Sealed, ts.Object);
            var gaugeFactory = Type("IGaugeFactory", TypeAttributes.NotPublic | TypeAttributes.Interface | TypeAttributes.Abstract, null);
            Attribute(gaugeFactory, "Guid", ts.String, "5b2e7c41-9a0d-4f3b-8c62-1d4e5f6a7b8c");
            Method(gaugeFactory, "Create", gauge, ts.String);
            Default(gauge, gaugeInterface);
            var activatable = new TypeReference("Windows.Foundation.Metadata", "ActivatableAttribute", module, ts.CoreLibrary);
            var factoryActivation = new MethodReference(".ctor", ts.Void, activatable) { HasThis = true };
            var systemType = module.ImportReference(typeof(Type));
            factoryActivation.Parameters.Add(new ParameterDefinition(systemType));
            factoryActivation.Parameters.Add(new ParameterDefinition(ts.UInt32));
            var gaugeActivation = new CustomAttribute(factoryActivation);
            gaugeActivation.ConstructorArguments.Add(new CustomAttributeArgument(systemType, gaugeFactory));
            gaugeActivation.ConstructorArguments.Add(new CustomAttributeArgument(ts.UInt32, (uint)1));
            gauge.CustomAttributes.Add(gaugeActivation);
            var gaugeConstructor = new MethodDefinition(".ctor", MethodAttributes.Public | MethodAttributes.SpecialName | MethodAttributes.RTSpecialName | MethodAttributes.HideBySig, ts.Void);
            gaugeConstructor.Parameters.Add(new ParameterDefinition("name", ParameterAttributes.In, ts.String));
            gaugeConstructor.ImplAttributes = MethodImplAttributes.Runtime;
            gauge.Methods.Add(gaugeConstructor);
            var gaugeName = Method(gauge, "get_Name", ts.String);
            gauge.Properties.Add(new PropertyDefinition("Name", PropertyAttributes.None, ts.String) { GetMethod = gaugeName });
            if (args.Length > 1 && args[1] == "unsupported") { engine.Methods[1].Parameters[0].ParameterType = new ByReferenceType(ts.Int32); iface.Methods[0].Parameters[0].ParameterType = new ByReferenceType(ts.Int32); }
            if (args.Length > 1 && args[1] == "culture") assembly.Name.Culture = "fr";
            if (args.Length > 1 && args[1] == "signed") { assembly.Name.PublicKey = new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 }; assembly.Name.HasPublicKey = true; }
            assembly.Write(args[0]);
        }
        return 0;
    }
}
