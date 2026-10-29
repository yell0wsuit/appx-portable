using System;
using System.Threading;
using System.Threading.Tasks;

using Windows.Foundation;

class AsyncProjectionTest
{
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    sealed class Operation : IAsyncOperation<int>
    {
        public uint Id { get { return 1; } }
        public AsyncStatus Status { get; set; }
        public Exception ErrorCode { get; set; }
        public int Results, Cancellations, Closes;
        public bool CompleteOnSubscribe, RejectSubscription;
        public bool CompleteOnCancelThread, CancelJoined;
        public Exception ResultFailure;
        AsyncOperationCompletedHandler<int> handler;
        public AsyncOperationCompletedHandler<int> Completed
        {
            get { return handler; }
            set
            {
                if (RejectSubscription) throw new InvalidOperationException("subscription rejected");
                handler = value;
                if (CompleteOnSubscribe) Finish(AsyncStatus.Completed);
                else if (Status != AsyncStatus.Started) value(this, Status);
            }
        }
        public int GetResults() { ++Results; if (ResultFailure != null) throw ResultFailure; return 42; }
        public void Cancel()
        {
            ++Cancellations;
            if (CompleteOnCancelThread)
            {
                var worker = new Thread(() => Finish(AsyncStatus.Canceled));
                worker.Start(); CancelJoined = worker.Join(2000);
            }
        }
        public void Close() { ++Closes; }
        public void Finish(AsyncStatus status) { Status = status; if (handler != null) handler(this, status); }
    }
    sealed class ActionOperation : IAsyncAction
    {
        public uint Id { get { return 2; } }
        public AsyncStatus Status { get; set; }
        public Exception ErrorCode { get; set; }
        public AsyncActionCompletedHandler Completed { get; set; }
        public bool Read;
        public void GetResults() { Read = true; }
        public void Cancel() { Status = AsyncStatus.Canceled; Completed(this, Status); }
        public void Close() { throw new Exception("adapter closed its caller's operation"); }
        public void Finish() { Status = AsyncStatus.Completed; Completed(this, Status); }
    }
    sealed class ProgressOperation : IAsyncOperationWithProgress<string, uint>
    {
        public uint Id { get { return 3; } }
        public AsyncStatus Status { get; set; }
        public Exception ErrorCode { get { return null; } }
        public AsyncOperationWithProgressCompletedHandler<string, uint> Completed { get; set; }
        public AsyncOperationProgressHandler<string, uint> Progress { get; set; }
        public string GetResults() { return "done"; }
        public void Cancel() { Status = AsyncStatus.Canceled; Completed(this, Status); }
        public void Close() { throw new Exception("unexpected close"); }
    }
    sealed class ProgressSink : IProgress<uint>
    {
        public uint Value;
        public object Owner { get; set; }
        public void Report(uint value) { Value = value; }
    }
    sealed class ProgressAction : IAsyncActionWithProgress<uint>
    {
        public uint Id { get { return 4; } }
        public AsyncStatus Status { get; set; }
        public Exception ErrorCode { get { return null; } }
        public AsyncActionWithProgressCompletedHandler<uint> Completed { get; set; }
        public AsyncActionProgressHandler<uint> Progress { get; set; }
        public void GetResults() { }
        public void Cancel() { Status = AsyncStatus.Canceled; Completed(this, Status); }
        public void Close() { throw new Exception("unexpected close"); }
    }
    static async Task<int> AwaitOperation(Operation operation) { return await operation; }
    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    static WeakReference CompletedLifetime(out Delegate handler)
    {
        var operation = new Operation { Status = AsyncStatus.Completed };
        Check(operation.AsTask().Result == 42, "lifetime result");
        handler = operation.Completed;
        return new WeakReference(operation);
    }
    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    static WeakReference CompletedActionLifetime(out Delegate handler)
    {
        var operation = new ActionOperation();
        Task task = operation.AsTask(); operation.Finish();
        Check(task.IsCompleted, "action lifetime completion");
        handler = operation.Completed;
        return new WeakReference(operation);
    }
    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    static WeakReference ProgressLifetime(out Delegate handler, out WeakReference sinkReference)
    {
        var operation = new ProgressOperation();
        var sink = new ProgressSink { Owner = operation };
        Task<string> task = operation.AsTask(sink);
        operation.Status = AsyncStatus.Completed; operation.Completed(operation, operation.Status);
        Check(task.Result == "done", "progress lifetime result");
        handler = operation.Completed;
        // Isolate the completion handler's ownership from the separately stored
        // progress handler, which belongs to the provider.
        operation.Progress = null;
        sinkReference = new WeakReference(sink);
        return new WeakReference(operation);
    }
    static void Run()
    {
        var early = new Operation { Status = AsyncStatus.Completed };
        Check(early.AsTask().Result == 42 && early.Results == 1 && early.Closes == 0, "completed result and caller ownership");
        var reentrant = new Operation { CompleteOnSubscribe = true };
        Check(reentrant.AsTask().Result == 42 && reentrant.Results == 1, "reentrant completion");
        var later = new Operation();
        Task<int> waiting = AwaitOperation(later);
        Check(!waiting.IsCompleted, "Started task completed early");
        var worker = new Thread(() => later.Finish(AsyncStatus.Completed));
        worker.Start(); Check(worker.Join(5000) && waiting.Wait(5000) && waiting.Result == 42, "worker completion and await");
        var failure = new InvalidOperationException("async failure identity");
        var failed = new Operation { Status = AsyncStatus.Error, ErrorCode = failure };
        try { failed.AsTask().GetAwaiter().GetResult(); throw new Exception("error accepted"); }
        catch (InvalidOperationException error) { Check(ReferenceEquals(error, failure), "ErrorCode identity"); }
        Check(failed.Results == 0, "GetResults called after error");
        Check(new Operation { Status = AsyncStatus.Canceled }.AsTask().IsCanceled, "canceled status");
        using (var cancellation = new CancellationTokenSource())
        {
            var pending = new Operation();
            var task = pending.AsTask(cancellation.Token);
            cancellation.Cancel();
            Check(pending.Cancellations == 1 && !task.IsCompleted, "cancellation request must await terminal status");
            pending.Finish(AsyncStatus.Canceled);
            Check(task.IsCanceled && pending.Results == 0, "cancellation completion");
        }
        using (var cancellation = new CancellationTokenSource())
        {
            var crossThread = new Operation { CompleteOnCancelThread = true };
            Task<int> task = crossThread.AsTask(cancellation.Token);
            cancellation.Cancel();
            Check(crossThread.CancelJoined, "cancellation registration deadlocked the native completion callback");
            Check(task.IsCanceled, "cross-thread cancellation status");
        }
        var completed = new Operation { Status = AsyncStatus.Completed };
        using (var cancellation = new CancellationTokenSource())
        {
            Check(completed.AsTask(cancellation.Token).Result == 42, "completed before cancellation");
            cancellation.Cancel(); Check(completed.Cancellations == 0, "completed cancellation registration retained");
        }
        using (var cancellation = new CancellationTokenSource())
        {
            cancellation.Cancel();
            var pending = new Operation();
            Task<int> task = pending.AsTask(cancellation.Token);
            Check(pending.Cancellations == 1 && !task.IsCompleted, "pre-canceled token requests native cancellation");
            pending.Finish(AsyncStatus.Completed);
            Check(task.Result == 42, "completion can win cancellation");
        }
        var duplicate = new Operation();
        Task<int> once = duplicate.AsTask();
        duplicate.Finish(AsyncStatus.Completed); duplicate.Finish(AsyncStatus.Completed);
        Check(once.Result == 42 && duplicate.Results == 1, "duplicate callback consumed results twice");
        var resultFailure = new Operation { Status = AsyncStatus.Completed, ResultFailure = failure };
        try { resultFailure.AsTask().GetAwaiter().GetResult(); throw new Exception("GetResults error accepted"); }
        catch (InvalidOperationException error) { Check(ReferenceEquals(error, failure), "GetResults exception identity"); }
        try { new Operation { RejectSubscription = true }.AsTask(); throw new Exception("subscription error accepted"); }
        catch (InvalidOperationException error) { Check(error.Message == "subscription rejected", "subscription error"); }
        var action = new ActionOperation();
        Task actionTask = action.AsTask(); action.Finish();
        Check(actionTask.IsCompleted && action.Read, "action completion");
        var sink = new ProgressSink();
        var progress = new ProgressOperation();
        Task<string> progressTask = progress.AsTask(sink);
        progress.Progress(progress, 67);
        progress.Status = AsyncStatus.Completed; progress.Completed(progress, progress.Status);
        Check(progressTask.Result == "done" && sink.Value == 67, "operation progress");
        var progressAction = new ProgressAction();
        Task progressActionTask = progressAction.AsTask(sink);
        progressAction.Progress(progressAction, 91);
        progressAction.Status = AsyncStatus.Completed; progressAction.Completed(progressAction, progressAction.Status);
        Check(progressActionTask.IsCompleted && sink.Value == 91, "action progress");
        try { WindowsRuntimeSystemExtensions.AsTask((IAsyncAction)null); throw new Exception("null accepted"); }
        catch (ArgumentNullException) { }
        Delegate completedHandler, completedActionHandler, progressHandler;
        WeakReference sinkReference;
        WeakReference progressOperation = ProgressLifetime(out progressHandler, out sinkReference);
        WeakReference completedOperation = CompletedLifetime(out completedHandler);
        WeakReference completedAction = CompletedActionLifetime(out completedActionHandler);
        for (int i = 0; i < 3; ++i) { GC.Collect(); GC.WaitForPendingFinalizers(); }
        Check(!completedOperation.IsAlive, "completion delegate retained its operation after completion");
        Check(!completedAction.IsAlive, "action completion closure retained its operation");
        Check(!progressOperation.IsAlive && !sinkReference.IsAlive, "completion closure retained the progress sink and operation");
        GC.KeepAlive(completedHandler); GC.KeepAlive(completedActionHandler); GC.KeepAlive(progressHandler);
        Console.WriteLine("PASS: WinRT task adapters preserve completion, failure, cancellation, progress and ownership");
    }
    static int Main()
    {
        try { Run(); return 0; }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
