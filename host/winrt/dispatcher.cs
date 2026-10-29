using System;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Threading;

namespace AppxPortable.WinRT
{
    // Continuations of managed code on the shim's view thread return to it, as
    // WinRT XAML apps' do: Post goes through CoreDispatcher.RunAsync.
    internal sealed class DispatcherContext : SynchronizationContext
    {
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int InvokeCall(IntPtr self);

        // Windows.UI.Core.IDispatchedHandler::Invoke, slot 3
        private static readonly InvokeCall invoke = self => ManagedObject.Call(self, target => Run((Action)target));
        private static readonly NativeInterface handler =
            new NativeInterface(new Guid("d1f276c4-98d8-4636-bf49-eb79507548e9"), false, invoke);
        [ThreadStatic] private static bool examined;
        private readonly NativeObject dispatcher;

        private DispatcherContext(NativeObject dispatcher) { this.dispatcher = dispatcher; }

        // Runs before managed code called from native code. A thread with a
        // CoreWindow, the view thread, gets this context once.
        internal static void Install()
        {
            if (examined) return;
            examined = true;
            if (Current != null) return;
            NativeObject statics;
            // Without the shim or its CoreWindow class there is no view thread.
            try { statics = NativeObject.Factory("Windows.UI.Core.CoreWindow", new Guid("4d239005-3c2a-41b1-9022-536bb9cf93b1")); }
            catch (DllNotFoundException) { return; }
            catch (COMException error)
            {
                if (error.HResult == unchecked((int)0x80040111)) return;
                throw;
            }
            // Windows.UI.Core.ICoreWindowStatic::GetForCurrentThread, slot 6
            using (statics)
            using (NativeObject window = statics.Object(6))
            {
                if (window == null) return;
                // Windows.UI.Core.ICoreWindow::get_Dispatcher, slot 9
                using (NativeObject core = window.QueryInterface(new Guid("79b9d5f2-879e-4b89-b798-79e47598030c")))
                {
                    NativeObject dispatcher = core.Object(9);
                    // Windows.UI.Core.ICoreDispatcher, 60db2fa8-b705-4fde-a7d6-ebbb1891d39e
                    if (dispatcher != null) SetSynchronizationContext(new DispatcherContext(dispatcher));
                }
            }
        }

        // As on Windows, an exception escaping a dispatched continuation ends the
        // app. Rethrown off the dispatcher, it reaches AppDomain.UnhandledException.
        private static void Run(Action work)
        {
            try { work(); }
            catch (Exception error)
            {
                ExceptionDispatchInfo failure = ExceptionDispatchInfo.Capture(error);
                new Thread(() => failure.Throw()).Start();
            }
        }

        public override void Post(SendOrPostCallback d, object state)
        {
            if (d == null) throw new ArgumentNullException("d");
            Action work = () => d(state);
            // Windows.UI.Core.ICoreDispatcher::RunAsync, slot 8, at CoreDispatcherPriority.Normal.
            using (var argument = ManagedObject.Hold(work, handler))
            using (dispatcher.Call(8, 0, argument.Pointer)) { }
        }

        // The CLR's WinRT dispatcher context does not support Send either.
        public override void Send(SendOrPostCallback d, object state)
        {
            throw new NotSupportedException("Send is not supported on the view thread's synchronization context");
        }

        public override SynchronizationContext CreateCopy() { return this; }
    }
}
