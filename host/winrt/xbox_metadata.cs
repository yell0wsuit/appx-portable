// Build-time qualification of the native stand-in's supported IUser layouts.
// Read WinMD only. Never inspect or load the Microsoft implementation DLL.
using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;

using Mono.Cecil;
class XboxMetadata
{
    // Native ABI contracts from shim/xbox/xbox_abi.h. IInspectable/interface
    // returns are pointers; enums are Int32 and HSTRINGs keep their own tag.
    static readonly string[] original = {
        "get_Identity()->ptr", "get_Status()->ptr", "GetProfileAsync()->ptr", "SetProfileAsync(ptr)->ptr", "GetFriendsAsync()->ptr",
        "GetAchievementsAsync(u4,u4,u1,ptr)->ptr", "UnlockAchievementAsync(u4)->ptr", "GetAvatarManifestAsync()->ptr", "PostUserMessageAsync(ptr)->ptr",
        "GetUserMessagesAsync(u4,u4,ptr)->ptr", "GetUserMessagesForTitleAsync(u4,u4,u4,ptr)->ptr", "DeleteUserMessageAsync(hstring)->ptr",
        "CreateMultiplayerSessionAsync(ptr,i4)->ptr", "JoinMultiplayerSessionAsync(hstring,i4,ptr)->ptr", "GetMultiplayerSessionSummariesAsync(u4,guid,i4,i4,i4)->ptr", "CanPerformActionAsync(hstring,u8)->ptr" };
    static string Abi(TypeReference type, ModuleDefinition module)
    {
        if (type is TypeSpecification && !(type is GenericInstanceType)) return "unsupported";
        switch (type.FullName)
        {
            case "System.String": return "hstring";
            case "System.Boolean": return "u1";
            case "System.UInt32": return "u4";
            case "System.Int32": return "i4";
            case "System.UInt64": return "u8";
            case "System.Guid": return "guid";
        }
        var definition = module.Types.SingleOrDefault(t => t.FullName == type.FullName);
        if (definition != null && definition.IsEnum)
            return definition.Fields.Single(f => f.Name == "value__").FieldType.FullName == "System.Int32" ? "i4" : "unsupported";
        return type.IsValueType || type.IsGenericParameter || type.FullName == "System.Void" ? "unsupported" : "ptr";
    }
    static string Signature(MethodDefinition method, ModuleDefinition module)
    {
        if (!method.HasThis || method.IsStatic || method.HasGenericParameters) return "unsupported";
        return method.Name + "(" + string.Join(",", method.Parameters.Select(p => Abi(p.ParameterType, module))) + ")->" + Abi(method.ReturnType, module);
    }
    static Guid Iid(TypeDefinition type)
    {
        var a = type.CustomAttributes.Single(x => x.AttributeType.FullName == "Windows.Foundation.Metadata.GuidAttribute").ConstructorArguments;
        if (a.Count != 11) throw new InvalidDataException("Unsupported Xbox IUser IID metadata");
        return new Guid(unchecked((int)Convert.ToUInt32(a[0].Value)), unchecked((short)Convert.ToUInt16(a[1].Value)),
            unchecked((short)Convert.ToUInt16(a[2].Value)), a.Skip(3).Select(x => Convert.ToByte(x.Value)).ToArray());
    }
    static void RequireSlot(ModuleDefinition module, string name, string iid, int slot, string signature)
    {
        var type = module.Types.Single(t => t.FullName == name);
        if (!type.IsInterface || Iid(type) != new Guid(iid) || type.Methods.Count <= slot - 6 ||
            Signature(type.Methods[slot - 6], module) != signature)
            throw new InvalidDataException("Unsupported Xbox " + type.Name + " slot " + slot);
    }
    static void RequireDefault(ModuleDefinition module, string name, string iface)
    {
        var type = module.Types.Single(t => t.FullName == name);
        var primary = type.Interfaces.SingleOrDefault(i => i.CustomAttributes.Any(a => a.AttributeType.FullName == "Windows.Foundation.Metadata.DefaultAttribute"));
        if (primary == null || primary.InterfaceType.FullName != iface)
            throw new InvalidDataException("Unsupported Xbox default interface for " + name);
    }
    static int Main(string[] args)
    {
        try
        {
            if (args.Length != 2) throw new ArgumentException("xbox-metadata XBOX_WINMD OUTPUT_CS");
            using (var module = ModuleDefinition.ReadModule(args[0], new ReaderParameters { ApplyWindowsRuntimeProjections = false }))
            {
                if (module.Assembly.Name.Name != "Microsoft.Xbox") throw new InvalidDataException("Not Microsoft.Xbox metadata");
                var type = module.Types.Single(t => t.FullName == "Microsoft.Xbox.IUser");
                if (!type.IsInterface || Iid(type) != new Guid("d7fba214-e744-4093-9fee-ab68795b70a0"))
                    throw new InvalidDataException("Unsupported Xbox IUser IID");
                var updated = original.Where(n => !n.StartsWith("SetProfileAsync(", StringComparison.Ordinal)).ToList();
                updated.Insert(updated.FindIndex(n => n.StartsWith("GetMultiplayerSessionSummariesAsync(", StringComparison.Ordinal)), "JoinMultiplayerSessionWithBufferAsync(hstring,i4,ptr)->ptr");
                string[] methods = type.Methods.Select(m => Signature(m, module)).ToArray();
                string version = methods.SequenceEqual(original) ? "1.0.0.0" : methods.SequenceEqual(updated) ? "1.2.0.0" : null;
                if (version == null) throw new InvalidDataException("Unsupported Xbox IUser layout");
                RequireDefault(module, "Microsoft.Xbox.User", "Microsoft.Xbox.IUser");
                RequireDefault(module, "Microsoft.Xbox.Foundation.ServiceClient", "Microsoft.Xbox.Foundation.IServiceClient");
                RequireSlot(module, "Microsoft.Xbox.Foundation.IServiceClient", "115ccd10-26c2-471c-b97b-5b3dac160c6e", 10, "get_SignedInUserIdentity()->ptr");
                RequireSlot(module, "Microsoft.Xbox.Foundation.IServiceClient", "115ccd10-26c2-471c-b97b-5b3dac160c6e", 11, "get_Token()->hstring");
                RequireSlot(module, "Microsoft.Xbox.IXboxLIVEService", "4b00e95d-3006-4f02-bd96-3919d984d4c7", 10, "get_SignedInUserIdentity()->ptr");
                RequireSlot(module, "Microsoft.Xbox.IXboxLIVEService", "4b00e95d-3006-4f02-bd96-3919d984d4c7", 11, "get_ServiceClient()->ptr");
                string hash;
                using (var stream = File.OpenRead(args[0]))
                using (var sha = SHA256.Create()) hash = BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();
                // The native factory reads this resource to select IUser10 or IUser12.
                // This is the ABI layout version, not an invented online SDK build.
                File.WriteAllText(args[1], "// Xbox WinMD SHA256 " + hash + "\n" +
                    "[assembly: global::System.Reflection.AssemblyFileVersion(\"" + version + "\")]\n");
                Console.WriteLine("Xbox IUser ABI " + version + ", metadata SHA256 " + hash);
                return 0;
            }
        }
        catch (Exception error) { Console.Error.WriteLine(error.Message); return 1; }
    }
}
