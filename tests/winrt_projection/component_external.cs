using System;
using System.Runtime.CompilerServices;

using Example.Component;
using Example.External;
class ExternalComponentTest
{
    static void Check(bool value, string name) { if (!value) throw new Exception(name); }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static WeakReference Store(Engine engine)
    {
        var node = new Node();
        Check(node.Read() == 73, "external activation");
        engine.SetNode(node);
        Check(object.ReferenceEquals(node, engine.GetNode()), "external result identity");
        int called = 0;
        var token = engine.add_ExternalChanged(value => { Check(object.ReferenceEquals(node, value), "external callback identity"); called++; });
        engine.RaiseExternal(); Check(called == 1, "external callback");
        engine.remove_ExternalChanged(token); engine.RaiseExternal(); Check(called == 1, "external callback removal");
        Check(engine.NextMode(Mode.First) == Mode.Second, "external enum ABI");
        return new WeakReference(node);
    }
    [MTAThread]
    static int Main(string[] args)
    {
        var engine = new Engine();
        if (Array.IndexOf(args, "reject-iid") >= 0)
        {
            try { engine.SetNode(new Node()); throw new Exception("wrong external IID accepted"); }
            catch (Exception error) { if (error.HResult != unchecked((int)0x80004002)) throw; }
            Console.WriteLine("PASS: external IID rejection"); return 0;
        }
        Check(engine.GetNode() == null, "null external result");
        var weak = Store(engine);
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        Check(!weak.IsAlive, "native retention does not root external wrapper");
        Check(engine.GetNode().Read() == 73, "native retains external object independently");
        engine.SetNode(null); Check(engine.GetNode() == null, "null external argument");
        Console.WriteLine("PASS: external component projection"); return 0;
    }
}
