using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Diagnostics;
using System.Runtime.Serialization;

using Microsoft.Xbox;

// A game-shaped reference: these field types must bind before its ctor can run.
class XboxFields
{
    public User CurrentUser;
    public UserProfile Profile;
}
class XboxProjectionTest
{
    [DllImport("combase.dll")] private static extern int RoInitialize(uint apartment);
    static void Check(bool value, string name) { if (!value) throw new Exception(name); }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void Run(string expectedVersion)
    {
        var fields = new XboxFields { CurrentUser = new User(), Profile = null };
        Check(fields.CurrentUser.Identity == null && fields.Profile == null, "native user is signed out");
        Check(XboxLIVEService.SignedInUserIdentity == null, "no signed-in identity");
        var client = XboxLIVEService.ServiceClient;
        Check(client != null && client.Token == "" && client.SignedInUserIdentity == null, "offline native client");
        Check(ReferenceEquals(client, XboxLIVEService.ServiceClient), "client wrapper identity");
        // This is supported binding, with no fabricated profile behavior.
        Check(typeof(UserProfile).Assembly.GetName().Name == "Microsoft.Xbox", "profile field binds to replacement assembly");
        if (expectedVersion != null)
        {
            var version = FileVersionInfo.GetVersionInfo(typeof(User).Assembly.Location);
            Check(version.FileMajorPart + "." + version.FileMinorPart + "." + version.FileBuildPart + "." + version.FilePrivatePart == expectedVersion,
                "native version resource must preserve the qualified ABI layout");
        }
        var unimplemented = (UserProfile)FormatterServices.GetUninitializedObject(typeof(UserProfile));
        try { string unused = unimplemented.Gamertag; throw new Exception("fabricated profile behavior"); }
        catch (NotImplementedException error) { Check(error.Message == "Microsoft.Xbox.UserProfile.get_Gamertag", "unimplemented profile member names itself"); }
        Console.WriteLine("PASS: Xbox projection uses native signed-out user and service client");
    }
    [STAThread]
    static int Main(string[] args)
    {
        try { Marshal.ThrowExceptionForHR(RoInitialize(0)); Run(args.Length == 0 ? null : args[0]); return 0; }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
