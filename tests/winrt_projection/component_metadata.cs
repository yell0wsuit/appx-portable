// Synthetic WinMD: its independent native ABI is in component_projection_fixture.cpp.
using System;
using System.Linq;
using Mono.Cecil;
class ComponentMetadata
{
    static ModuleDefinition module;
    static TypeDefinition Type(string name, TypeAttributes flags, TypeReference parent)
    {
        var type = new TypeDefinition("Example.Component", name, flags | TypeAttributes.WindowsRuntime, parent);
        module.Types.Add(type); return type;
    }
    static void Attribute(ICustomAttributeProvider owner, string name, TypeReference parameter, object value)
    {
        var attribute = new TypeReference("Windows.Foundation.Metadata", name+"Attribute", module, module.TypeSystem.CoreLibrary);
        var ctor = new MethodReference(".ctor", module.TypeSystem.Void, attribute) { HasThis = true };
        ctor.Parameters.Add(new ParameterDefinition(parameter));
        var a = new CustomAttribute(ctor); a.ConstructorArguments.Add(new CustomAttributeArgument(parameter, value));
        owner.CustomAttributes.Add(a);
    }
    static MethodDefinition Method(TypeDefinition owner, string name, TypeReference result, params TypeReference[] arguments)
    {
        var method = new MethodDefinition(name, MethodAttributes.Public | MethodAttributes.Virtual | MethodAttributes.NewSlot | MethodAttributes.Abstract, result);
        for (int i=0; i<arguments.Length; i++) method.Parameters.Add(new ParameterDefinition("p"+i, ParameterAttributes.In, arguments[i]));
        owner.Methods.Add(method); return method;
    }
    static int Main(string[] args)
    {
        if (args[0] == "--bad-reference")
        {
            using (var input = AssemblyDefinition.ReadAssembly(args[1]))
            {
                input.MainModule.AssemblyReferences.Single(r => r.Name == "Example.Component").Version = new Version(1,0,0,0);
                input.Write(args[2]);
            }
            return 0;
        }
        using (var assembly = AssemblyDefinition.CreateAssembly(new AssemblyNameDefinition("Example.Component", new Version(255,255,255,255)), "Example.Component.winmd", ModuleKind.Dll))
        {
            module=assembly.MainModule; module.Kind=ModuleKind.Windows;
            var ts=module.TypeSystem;
            var handler=Type("Changed", TypeAttributes.Public | TypeAttributes.Sealed, module.ImportReference(typeof(MulticastDelegate)));
            Attribute(handler,"Guid",ts.String,"723d9b38-bb73-463e-adc8-25b46f56121a");
            Method(handler,"Invoke",ts.String,ts.Int32,ts.Boolean,ts.String);
            var iface=Type("IEngine", TypeAttributes.NotPublic | TypeAttributes.Interface | TypeAttributes.Abstract, null);
            Attribute(iface,"Guid",ts.String,"2fc62e47-e1ce-471d-9098-014b39ca938e");
            var token=new TypeReference("Windows.Foundation","EventRegistrationToken",module,ts.CoreLibrary,true);
            Method(iface,"Echo",ts.Int32,ts.Int32);
            Method(iface,"SetState",ts.Void,ts.Boolean,ts.Double,ts.UInt32);
            Method(iface,"GetState",ts.Double);
            Method(iface,"EchoText",ts.String,ts.String);
            Method(iface,"SumBytes",ts.UInt32,new ArrayType(ts.Byte));
            var add=Method(iface,"add_Changed",token,handler);
            var remove=Method(iface,"remove_Changed",ts.Void,token);
            Method(iface,"Raise",ts.String,ts.Int32,ts.Boolean,ts.String);
            Method(iface,"Fail",ts.Void);
            iface.Events.Add(new EventDefinition("Changed",EventAttributes.None,handler) { AddMethod=add,RemoveMethod=remove });
            var engine=Type("Engine",TypeAttributes.Public | TypeAttributes.Sealed,ts.Object);
            var implementation=new InterfaceImplementation(iface);
            // DefaultAttribute has no arguments.
            var defaultAttribute=new TypeReference("Windows.Foundation.Metadata","DefaultAttribute",module,ts.CoreLibrary);
            implementation.CustomAttributes.Add(new CustomAttribute(new MethodReference(".ctor",ts.Void,defaultAttribute) { HasThis=true }));
            engine.Interfaces.Add(implementation);
            Attribute(engine,"Activatable",ts.UInt32,(uint)1);
            var ctor=Method(engine,".ctor",ts.Void); ctor.Attributes=MethodAttributes.Public | MethodAttributes.SpecialName | MethodAttributes.RTSpecialName;
            foreach(var method in iface.Methods)
            {
                var copy=Method(engine,method.Name,method.ReturnType);
                foreach(var p in method.Parameters) copy.Parameters.Add(new ParameterDefinition(p.Name,p.Attributes,p.ParameterType));
            }
            engine.Events.Add(new EventDefinition("Changed",EventAttributes.None,handler) { AddMethod=engine.Methods[6], RemoveMethod=engine.Methods[7] });
            if(args.Length>1 && args[1]=="unsupported") { engine.Methods[1].Parameters[0].ParameterType=new ByReferenceType(ts.Int32); iface.Methods[0].Parameters[0].ParameterType=new ByReferenceType(ts.Int32); }
            if (args.Length > 1 && args[1] == "culture") assembly.Name.Culture = "fr";
            if (args.Length > 1 && args[1] == "signed") { assembly.Name.PublicKey = new byte[] { 1,2,3,4,5,6,7,8 }; assembly.Name.HasPublicKey = true; }
            assembly.Write(args[0]);
        }
        return 0;
    }
}
