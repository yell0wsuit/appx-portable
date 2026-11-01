using System;
using System.Runtime.InteropServices;
using AppxPortable.WinRT.Interop;
[assembly: ProjectionProvider(typeof(TestProvider))]
sealed class TestHandle : SafeHandle
{
    internal int Releases;
    internal TestHandle() : base(IntPtr.Zero, true) { SetHandle(new IntPtr(1)); }
    public override bool IsInvalid { get { return false; } }
    protected override bool ReleaseHandle() { Releases++; return true; }
}
public sealed class TestTarget { }
public sealed class TestProvider : IProjectionProvider
{
    internal static bool Fails;
    public object Wrap(Type type, SafeHandle borrowed)
    {
        if (borrowed.IsClosed) throw new Exception("lease closed too early");
        if (Fails) throw new InvalidOperationException("provider failed");
        return new TestTarget();
    }
}
class InteropLifetimeTest
{
    static void Check(bool value) { if (!value) throw new Exception("lease ownership failure"); }
    static int Main()
    {
        var owned = new TestHandle();
        Check(ProjectionInterop.Wrap<TestTarget>(owned) != null);
        Check(owned.IsClosed && owned.Releases == 1);
        TestProvider.Fails = true; owned = new TestHandle();
        try { ProjectionInterop.Wrap<TestTarget>(owned); throw new Exception("provider failure lost"); }
        catch (InvalidOperationException) { }
        Check(owned.IsClosed && owned.Releases == 1);
        owned = new TestHandle();
        try { ProjectionInterop.Wrap<string>(owned); throw new Exception("missing provider accepted"); }
        catch (NotSupportedException) { }
        Check(owned.IsClosed && owned.Releases == 1);
        Check(ProjectionInterop.Wrap<TestTarget>(null) == null);
        Console.WriteLine("PASS: interop lease ownership"); return 0;
    }
}
