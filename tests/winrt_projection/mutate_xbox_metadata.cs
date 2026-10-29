using System;
using System.Linq;

using Mono.Cecil;
class MutateXboxMetadata
{
    static int Main(string[] args)
    {
        using (var module = ModuleDefinition.ReadModule(args[0], new ReaderParameters { ApplyWindowsRuntimeProjections = false, InMemory = true }))
        {
            var type = module.Types.Single(t => t.FullName == "Microsoft.Xbox.IUser");
            if (args[2] == "layout") type.Methods[0].Name = "UnknownSlot";
            else if (args[2] == "signature") type.Methods.Single(m => m.Name == "GetAchievementsAsync").Parameters[0].ParameterType = module.TypeSystem.UInt64;
            else if (args[2] == "service-slot" || args[2] == "live-slot")
            {
                var service = module.Types.Single(t => t.FullName == (args[2] == "service-slot" ? "Microsoft.Xbox.Foundation.IServiceClient" : "Microsoft.Xbox.IXboxLIVEService"));
                service.Methods.Insert(0, new MethodDefinition("InsertedSlot", MethodAttributes.Public | MethodAttributes.Abstract | MethodAttributes.Virtual, module.TypeSystem.Void));
            }
            else if (args[2] == "iid")
            {
                var attribute = type.CustomAttributes.Single(a => a.AttributeType.FullName == "Windows.Foundation.Metadata.GuidAttribute");
                var word = attribute.ConstructorArguments[0];
                attribute.ConstructorArguments[0] = new CustomAttributeArgument(word.Type, Convert.ToUInt32(word.Value) ^ 0x10000U);
            }
            else throw new ArgumentException("mutation");
            module.Write(args[1]);
            return 0;
        }
    }
}
