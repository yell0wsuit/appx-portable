using System;
using System.Runtime.CompilerServices;
using Windows.UI.Xaml;
// A member outside the implemented surface binds and then names itself when called.
// Windows types stay out of Main's class and method so a binding failure is
// catchable here: Mono aborts with exit status 0 when Main's own class cannot
// load, and a failure while compiling Main escapes its try block.
static class Calls
{
    internal static object Start() { return Window.Current; }
}

class UnimplementedProjectionTest
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void Start() { Calls.Start(); }

    static int Main()
    {
        try { Start(); }
        catch (NotImplementedException error)
        {
            Console.WriteLine("UNIMPLEMENTED: " + error.Message);
            return error.Message == "Windows.UI.Xaml.Window.get_Current" ? 0 : 1;
        }
        catch (TypeLoadException error)
        {
            Console.WriteLine("UNBOUND: " + error.Message);
            return 2;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
        Console.Error.WriteLine("Window.Current returned");
        return 1;
    }
}
