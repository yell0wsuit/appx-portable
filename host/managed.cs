// Managed launcher: runs the game's managed exe in-process so the process
// carries this exe's manifest (maxversiontested for XAML Islands).
using System;
using System.IO;
using System.Runtime.InteropServices;

// Posts continuations to the UI thread through a WinForms marshaling control
// (the island's message loop pumps it). Exceptions are logged, not shown in
// WinForms' dialog; a Windows 8 app would route them to UnhandledException.
sealed class UiContext : System.Threading.SynchronizationContext {
    readonly System.Windows.Forms.Control control;
    readonly string log;
    public UiContext(string log) {
        this.log = log;
        control = new System.Windows.Forms.Control();
        control.CreateControl();
        IntPtr unused = control.Handle;
    }
    void Run(System.Threading.SendOrPostCallback d, object state) {
        try { d(state); }
        catch (Exception ex) { File.AppendAllText(log, "[posted continuation threw] " + ex + "\n"); }
    }
    public override void Post(System.Threading.SendOrPostCallback d, object state) {
        control.BeginInvoke(new Action(() => Run(d, state)));
    }
    public override void Send(System.Threading.SendOrPostCallback d, object state) {
        if (control.InvokeRequired) control.Invoke(new Action(() => Run(d, state)));
        else Run(d, state);
    }
    public override System.Threading.SynchronizationContext CreateCopy() { return this; }
}

static class Host {
    [DllImport("appxportable.dll", CallingConvention = CallingConvention.Cdecl)]
    static extern void ShimBootstrap();

    [STAThread]
    static int Main(string[] args) {
        string dir = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
        string log = Path.Combine(dir, "managed.log");
        File.WriteAllText(log, "");
        AppDomain.CurrentDomain.FirstChanceException += (s, e) =>
            File.AppendAllText(log, string.Format("[first-chance] {0} 0x{1:X8}: {2}\n{3}\n",
                e.Exception.GetType().FullName, e.Exception.HResult, e.Exception.Message,
                new System.Diagnostics.StackTrace(1, false)));
        AppDomain.CurrentDomain.UnhandledException += (s, e) =>
            File.AppendAllText(log, "[unhandled] " + e.ExceptionObject + "\n");
        ShimBootstrap();
        // .NET installs the CoreDispatcher context only for packaged (AppX)
        // processes; without one, awaits resume on the thread pool. A WinForms
        // context posts to this thread, which the island's message loop pumps.
        System.Threading.SynchronizationContext.SetSynchronizationContext(new UiContext(log));
        try {
            var manifest = new System.Xml.XmlDocument();
            manifest.XmlResolver = null;
            var settings = new System.Xml.XmlReaderSettings { DtdProcessing = System.Xml.DtdProcessing.Prohibit, XmlResolver = null };
            using (var reader = System.Xml.XmlReader.Create(Path.Combine(dir, "AppxManifest.xml"), settings)) manifest.Load(reader);
            var app = manifest.SelectSingleNode("//*[local-name()='Application']");
            string exe = app.Attributes["Executable"].Value;
            string target = Path.GetFullPath(Path.Combine(dir, exe));
            if (!target.StartsWith(dir + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Executable escapes game folder");
            return AppDomain.CurrentDomain.ExecuteAssembly(target, args);
        } catch (Exception ex) {
            File.AppendAllText(log, "[escaped Main] " + ex + "\n");
            return 1;
        }
    }
}
