// Managed launcher: runs the game's managed exe in-process so the process
// carries this exe's manifest (maxversiontested for XAML Islands).
using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using System.Xml;

// Posts continuations to the UI thread through a WinForms marshaling control
// (the island's message loop pumps it). Exceptions are logged, not shown in
// WinForms' dialog; a Windows 8 app would route them to UnhandledException.
internal sealed class UiContext : SynchronizationContext
{
    private readonly System.Windows.Forms.Control control;
    private readonly string log;
    public UiContext(string log)
    {
        this.log = log;
        control = new System.Windows.Forms.Control();
        control.CreateControl();
        IntPtr unused = control.Handle;
    }
    private void Run(SendOrPostCallback d, object state)
    {
        try { d(state); }
        catch (Exception ex) { Host.AppendLog(log, "[posted continuation threw] " + ex + "\n"); }
    }
    public override void Post(SendOrPostCallback d, object state)
    {
        IAsyncResult unused = control.BeginInvoke(new Action(() => Run(d, state)));
    }
    public override void Send(SendOrPostCallback d, object state)
    {
        if (control.InvokeRequired)
        {
            object unused = control.Invoke(new Action(() => Run(d, state)));
        }
        else
        {
            Run(d, state);
        }
    }
    public override SynchronizationContext CreateCopy() { return this; }
}

internal static class Host
{
    private static readonly object logLock = new object();
    [ThreadStatic]
    private static bool writingLog;

    internal static void AppendLog(string path, string text)
    {
        // First-chance events can arrive concurrently. A sharing violation in
        // the exception observer itself can terminate the CLR. Logging must
        // never throw or recursively log its own failures.
        if (writingLog)
        {
            return;
        }
        writingLog = true;
        try
        {
            lock (logLock) { File.AppendAllText(path, text); }
        }
        catch (Exception) { }
        finally { writingLog = false; }
    }

    [DllImport("appxportable.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern void ShimBootstrap();
    [DllImport("appxportable.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern void ShimSetManagedCoreWindowMode();

    [MTAThread]
    private static int Main(string[] args)
    {
        string dir = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
        string log = Path.Combine(dir, "managed.log");
        File.WriteAllText(log, "");
        AppDomain.CurrentDomain.FirstChanceException += (s, e) =>
            AppendLog(log, string.Format("[first-chance] {0} 0x{1:X8}: {2}\n{3}\n",
                e.Exception.GetType().FullName, e.Exception.HResult, e.Exception.Message,
                new System.Diagnostics.StackTrace(1, false)));
        AppDomain.CurrentDomain.UnhandledException += (s, e) =>
            AppendLog(log, "[unhandled] " + e.ExceptionObject + "\n");
        ShimBootstrap();
        try
        {
            XmlDocument manifest = new XmlDocument
            {
                XmlResolver = null
            };
            XmlReaderSettings settings = new XmlReaderSettings { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null };
            using (XmlReader reader = XmlReader.Create(Path.Combine(dir, "AppxManifest.xml"), settings))
            {
                manifest.Load(reader);
            }

            XmlNode app = manifest.SelectSingleNode("//*[local-name()='Application']");
            string exe = app.Attributes["Executable"].Value;
            string target = Path.GetFullPath(Path.Combine(dir, exe));
            if (!target.StartsWith(dir + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException("Executable escapes game folder");
            }

            Assembly assembly = Assembly.LoadFrom(target);
            bool coreWindow = assembly.EntryPoint.IsDefined(typeof(MTAThreadAttribute), false);
            AppendLog(log, coreWindow ? "[host] managed CoreWindow (MTA)\n" : "[host] managed XAML Islands (STA)\n");
            if (coreWindow)
            {
                ShimSetManagedCoreWindowMode();
                return AppDomain.CurrentDomain.ExecuteAssembly(target, args);
            }
            int result = 1;
            Thread ui = new Thread(() =>
            {
                try
                {
                    // Unpackaged CLR does not install a UI synchronization context.
                    SynchronizationContext.SetSynchronizationContext(new UiContext(log));
                    result = AppDomain.CurrentDomain.ExecuteAssembly(target, args);
                }
                catch (Exception ex)
                {
                    AppendLog(log, "[escaped Main] " + ex + "\n");
                }
            });
            ui.SetApartmentState(ApartmentState.STA);
            ui.Start();
            ui.Join();
            return result;
        }
        catch (Exception ex)
        {
            AppendLog(log, "[escaped Main] " + ex + "\n");
            return 1;
        }
    }
}
