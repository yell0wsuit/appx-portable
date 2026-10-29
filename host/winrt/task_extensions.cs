// Own CLR adapters over the generated WinRT contracts. Native operations must
// expose these interfaces through an ABI wrapper before they can be awaited.
using System;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;

using Windows.Foundation;

[assembly: System.Reflection.AssemblyVersion("4.0.0.0")]
namespace System
{
    public static class WindowsRuntimeSystemExtensions
    {
        private sealed class Adapter<T>
        {
            private IAsyncInfo source;
            private Func<T> results;
            private readonly TaskCompletionSource<T> completion = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
            private readonly object gate = new object();
            private CancellationTokenRegistration registration;
            private bool finished;
            internal Adapter(IAsyncInfo source, Func<T> results) { this.source = source; this.results = results; }
            internal Task<T> Task { get { return completion.Task; } }
            internal void Finish(AsyncStatus status)
            {
                if (status == AsyncStatus.Started) return;
                CancellationTokenRegistration cleanup;
                IAsyncInfo operation;
                Func<T> getResults;
                lock (gate)
                {
                    if (finished) return;
                    finished = true;
                    cleanup = registration;
                    registration = default(CancellationTokenRegistration);
                    operation = source; getResults = results;
                    // A native operation may retain its completion CCW. Break
                    // the reverse edge once terminal so it cannot form a cycle.
                    source = null; results = null;
                }
                // Cancel may be waiting for this callback on another thread.
                // Disposal waits for an executing token callback, so it must run
                // independently. The finished flag makes later callbacks inert.
                if (!cleanup.Equals(default(CancellationTokenRegistration)))
                    ThreadPool.QueueUserWorkItem(ignored => cleanup.Dispose());
                try
                {
                    if (status == AsyncStatus.Completed) completion.TrySetResult(getResults());
                    else if (status == AsyncStatus.Canceled) completion.TrySetCanceled();
                    else if (status == AsyncStatus.Error)
                        completion.TrySetException(operation.ErrorCode ?? new InvalidOperationException("WinRT operation failed without ErrorCode"));
                    else completion.TrySetException(new InvalidOperationException("Invalid WinRT AsyncStatus: " + status));
                }
                catch (Exception error) { completion.TrySetException(error); }
            }
            internal void Register(CancellationToken token)
            {
                if (!token.CanBeCanceled) return;
                lock (gate) if (finished) return;
                CancellationTokenRegistration created = token.Register(() =>
                {
                    // Cancellation requests are asynchronous. Only the terminal
                    // WinRT status decides whether the resulting task is canceled.
                    IAsyncInfo operation;
                    lock (gate) { if (finished) return; operation = source; }
                    operation.Cancel();
                });
                bool dispose;
                lock (gate) { dispose = finished; if (!dispose) registration = created; }
                if (dispose) created.Dispose();
            }
        }
        // Keep the source-capturing result closure separate from the completion
        // closure, which must retain only the adapter after the operation ends.
        private static Func<object> Results(IAsyncAction source)
        {
            return () => { source.GetResults(); return null; };
        }
        private static Func<object> Results<TProgress>(IAsyncActionWithProgress<TProgress> source)
        {
            return () => { source.GetResults(); return null; };
        }
        private static AsyncActionProgressHandler<TProgress> Progress<TProgress>(IProgress<TProgress> progress)
        {
            return (operation, value) => progress.Report(value);
        }
        private static AsyncOperationProgressHandler<T, TProgress> Progress<T, TProgress>(IProgress<TProgress> progress)
        {
            return (operation, value) => progress.Report(value);
        }
        public static Task AsTask(this IAsyncAction source) { return AsTask(source, CancellationToken.None); }
        public static Task AsTask(this IAsyncAction source, CancellationToken cancellationToken)
        {
            if (source == null) throw new ArgumentNullException("source");
            var adapter = new Adapter<object>(source, Results(source));
            source.Completed = (operation, status) => adapter.Finish(status);
            adapter.Register(cancellationToken);
            return adapter.Task;
        }
        public static Task<T> AsTask<T>(this IAsyncOperation<T> source) { return AsTask(source, CancellationToken.None); }
        public static Task<T> AsTask<T>(this IAsyncOperation<T> source, CancellationToken cancellationToken)
        {
            if (source == null) throw new ArgumentNullException("source");
            var adapter = new Adapter<T>(source, source.GetResults);
            source.Completed = (operation, status) => adapter.Finish(status);
            adapter.Register(cancellationToken);
            return adapter.Task;
        }
        public static Task AsTask<TProgress>(this IAsyncActionWithProgress<TProgress> source) { return AsTask(source, CancellationToken.None, null); }
        public static Task AsTask<TProgress>(this IAsyncActionWithProgress<TProgress> source, CancellationToken token) { return AsTask(source, token, null); }
        public static Task AsTask<TProgress>(this IAsyncActionWithProgress<TProgress> source, IProgress<TProgress> progress) { return AsTask(source, CancellationToken.None, progress); }
        public static Task AsTask<TProgress>(this IAsyncActionWithProgress<TProgress> source, CancellationToken token, IProgress<TProgress> progress)
        {
            if (source == null) throw new ArgumentNullException("source");
            var adapter = new Adapter<object>(source, Results(source));
            if (progress != null) source.Progress = Progress<TProgress>(progress);
            source.Completed = (operation, status) => adapter.Finish(status);
            adapter.Register(token);
            return adapter.Task;
        }
        public static Task<T> AsTask<T, TProgress>(this IAsyncOperationWithProgress<T, TProgress> source) { return AsTask(source, CancellationToken.None, null); }
        public static Task<T> AsTask<T, TProgress>(this IAsyncOperationWithProgress<T, TProgress> source, CancellationToken token) { return AsTask(source, token, null); }
        public static Task<T> AsTask<T, TProgress>(this IAsyncOperationWithProgress<T, TProgress> source, IProgress<TProgress> progress) { return AsTask(source, CancellationToken.None, progress); }
        public static Task<T> AsTask<T, TProgress>(this IAsyncOperationWithProgress<T, TProgress> source, CancellationToken token, IProgress<TProgress> progress)
        {
            if (source == null) throw new ArgumentNullException("source");
            var adapter = new Adapter<T>(source, source.GetResults);
            if (progress != null) source.Progress = Progress<T, TProgress>(progress);
            source.Completed = (operation, status) => adapter.Finish(status);
            adapter.Register(token);
            return adapter.Task;
        }
        public static TaskAwaiter GetAwaiter(this IAsyncAction source) { return source.AsTask().GetAwaiter(); }
        public static TaskAwaiter<T> GetAwaiter<T>(this IAsyncOperation<T> source) { return source.AsTask().GetAwaiter(); }
        public static TaskAwaiter GetAwaiter<TProgress>(this IAsyncActionWithProgress<TProgress> source) { return source.AsTask().GetAwaiter(); }
        public static TaskAwaiter<T> GetAwaiter<T, TProgress>(this IAsyncOperationWithProgress<T, TProgress> source) { return source.AsTask().GetAwaiter(); }
    }
}
