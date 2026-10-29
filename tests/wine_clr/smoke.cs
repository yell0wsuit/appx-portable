using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

class Smoke
{
    [DllImport("kernel32.dll")]
    static extern uint GetCurrentThreadId();
    [DllImport("kernel32.dll")]
    static extern bool QueryPerformanceCounter(out long value);
    [DllImport("ntdll.dll")]
    static extern long RtlGetSystemTimePrecise();

    static int Main()
    {
        if (IntPtr.Size != 4 || GetCurrentThreadId() == 0) return 1;
        int total = 0;
        Task[] tasks = new Task[4];
        for (int i = 0; i < tasks.Length; ++i)
        {
            tasks[i] = Task.Run(() =>
            {
                for (int j = 0; j < 1000; ++j)
                {
                    long counter;
                    if (GetCurrentThreadId() == 0 || !QueryPerformanceCounter(out counter)
                        || counter <= 0 || RtlGetSystemTimePrecise() <= 0)
                        throw new InvalidOperationException("native clock failed");
                    Interlocked.Increment(ref total);
                }
                try { throw new InvalidOperationException("expected"); }
                catch (InvalidOperationException e) { if (e.Message != "expected") throw; }
            });
        }
        Task.WaitAll(tasks);
        GC.Collect();
        if (total != 4000) return 2;
        Console.WriteLine("x86 CLR smoke PASS: PInvoke, tasks, exceptions, GC");
        return 0;
    }
}
