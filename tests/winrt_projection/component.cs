using System;
using System.Runtime.CompilerServices;
using Example.Component;
class ComponentTest
{
    static void Require(bool value, string message) { if (!value) throw new Exception(message); }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static WeakReference Subscribe(Engine engine, out System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token)
    {
        var expected = new[] { -42 };
        Changed handler = (count, active, text) => { Require(count == expected[0] && active && text == "đá\0💎", "callback ABI"); return text + count; };
        token = engine.add_Changed(handler); return new WeakReference(handler);
    }
    [MTAThread]
    static int Main()
    {
        var engine = new Engine();
        Require(engine.Echo(-1234567) == -1234567, "signed scalar result");
        engine.SetState(true, 1.25, 0xf0000000U);
        Require(engine.GetState() == 4026531841.25, "mixed-width parameters");
        engine.SetState(false, 2.5, 1);
        Require(engine.GetState() == -2.5, "one-byte false");
        Require(engine.EchoText("đá\0💎") == "đá\0💎", "HSTRING ownership and length");
        Require(engine.EchoText(null) == "", "null HSTRING");
        Require(engine.SumBytes(new byte[] { 0, 1, 128, 255 }) == 384, "array length and unsigned bytes");
        Require(engine.SumBytes(new byte[0]) == 0 && engine.SumBytes(null) == 0, "empty arrays");
        var large = new byte[1024*1024]; large[large.Length-1] = 255;
        Require(engine.SumBytes(large) == 255, "large pinned array");
        System.Runtime.InteropServices.WindowsRuntime.EventRegistrationToken token;
        var weak = Subscribe(engine, out token);
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        Require(weak.IsAlive, "native retained delegate");
        Require(engine.Raise(-42, true, "đá\0💎") == "đá\0💎-42", "string-returning callback");
        engine.remove_Changed(token);
        Require(engine.Raise(1, false, "unused") == "", "removed callback");
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        Require(!weak.IsAlive, "removed delegate collectable");
        var failure = new InvalidOperationException("component callback failure");
        token = engine.add_Changed((count, active, text) => { throw failure; });
        try { engine.Raise(1, false, ""); throw new Exception("callback exception lost"); }
        catch (InvalidOperationException ex) { Require(ex.HResult == failure.HResult, "callback HRESULT"); }
        engine.remove_Changed(token);
        try { engine.Fail(); throw new Exception("native error lost"); }
        catch (ArgumentException) { }
        Console.WriteLine("PASS: generic component ABI");
        return 0;
    }
}
