using System;
using System.IO;
using System.Linq;

using Mono.Cecil;
class MutateMetadata
{
    static int Main(string[] args)
    {
        Directory.CreateDirectory(args[1]);
        foreach (string path in Directory.GetFiles(args[0], "*.winmd"))
            File.Copy(path, Path.Combine(args[1], Path.GetFileName(path)), true);
        string target = Path.Combine(args[1], "Windows.Foundation.UniversalApiContract.winmd");
        using (ModuleDefinition module = ModuleDefinition.ReadModule(target,
            new ReaderParameters { ApplyWindowsRuntimeProjections = false, InMemory = true }))
        {
            TypeDefinition type = module.Types.Single(t => t.FullName == "Windows.ApplicationModel.IPackageId");
            CustomAttribute iid = type.CustomAttributes.Single(a => a.AttributeType.FullName == "Windows.Foundation.Metadata.GuidAttribute");
            uint changed = Convert.ToUInt32(iid.ConstructorArguments[0].Value) ^ 0x10000U;
            iid.ConstructorArguments[0] = new CustomAttributeArgument(module.TypeSystem.UInt32, changed);
            type.Methods.Insert(0, new MethodDefinition("ProjectionDummy", MethodAttributes.Public |
                MethodAttributes.Abstract | MethodAttributes.Virtual | MethodAttributes.NewSlot, module.TypeSystem.Void));
            module.Write(target + ".tmp");
            File.Delete(target);
            File.Move(target + ".tmp", target);
            Console.WriteLine("Changed IID first word=" + changed.ToString("x8") + "; Name slot=7");
        }
        return 0;
    }
}
