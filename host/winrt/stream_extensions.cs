// Read adapters over real WinRT streams and IBufferByteAccess. No shadow files.
using System;
using System.Threading;
using System.Threading.Tasks;

using Windows.Storage.Streams;

using AppxPortable.WinRT;
namespace System.IO
{
    public static class WindowsRuntimeStreamExtensions
    {
        public static Stream AsStream(this IRandomAccessStream source)
        {
            if (source == null) throw new ArgumentNullException("source");
            if (!source.CanRead || source.CanWrite) throw new NotSupportedException("Only read-only WinRT streams are projected");
            return new ReadStream(source, source);
        }
        public static Stream AsStreamForRead(this IInputStream source)
        {
            if (source == null) throw new ArgumentNullException("source");
            return new ReadStream(source, source as IRandomAccessStream);
        }
        private sealed class ReadStream : Stream
        {
            private IInputStream source;
            private readonly IRandomAccessStream random;
            private readonly SemaphoreSlim gate = new SemaphoreSlim(1);
            internal ReadStream(IInputStream source, IRandomAccessStream random) { this.source = source; this.random = random; }
            private void Open() { if (source == null) throw new ObjectDisposedException("WinRT stream"); }
            public override bool CanRead { get { return source != null; } }
            public override bool CanSeek { get { return source != null && random != null; } }
            public override bool CanWrite { get { return false; } }
            public override long Length { get { Open(); if (random == null) throw new NotSupportedException("Input stream cannot seek"); return checked((long)random.Size); } }
            public override long Position
            {
                get { Open(); if (random == null) throw new NotSupportedException("Input stream cannot seek"); return checked((long)random.Position); }
                set { Seek(value, SeekOrigin.Begin); }
            }
            public override void Flush() { Open(); }
            public override void SetLength(long value) { throw new NotSupportedException("Read-only WinRT stream"); }
            public override void Write(byte[] buffer, int offset, int count) { throw new NotSupportedException("Read-only WinRT stream"); }
            public override int Read(byte[] buffer, int offset, int count) { return ReadAsync(buffer, offset, count, CancellationToken.None).GetAwaiter().GetResult(); }
            public override async Task<int> ReadAsync(byte[] buffer, int offset, int count, CancellationToken cancellationToken)
            {
                if (buffer == null) throw new ArgumentNullException("buffer");
                if (offset < 0 || count < 0 || offset > buffer.Length - count) throw new ArgumentOutOfRangeException("offset");
                Open(); cancellationToken.ThrowIfCancellationRequested();
                await gate.WaitAsync(cancellationToken).ConfigureAwait(false);
                try
                {
                    Open();
                    if (count == 0) return 0;
                    var target = new global::Windows.Storage.Streams.Buffer((uint)count);
                    IBuffer result = await source.ReadAsync(target, (uint)count, InputStreamOptions.Partial).AsTask(cancellationToken).ConfigureAwait(false);
                    if (result == null || result.Length > (uint)count) throw new IOException("WinRT returned an invalid read buffer");
                    int length = checked((int)result.Length);
                    StorageBuffer.Copy(result, buffer, offset, length, false);
                    return length;
                }
                finally { gate.Release(); }
            }
            public override long Seek(long offset, SeekOrigin origin)
            {
                Open(); if (random == null) throw new NotSupportedException("Input stream cannot seek");
                gate.Wait();
                try
                {
                    Open(); long basis;
                    switch (origin) { case SeekOrigin.Begin: basis = 0; break; case SeekOrigin.Current: basis = Position; break; case SeekOrigin.End: basis = Length; break; default: throw new ArgumentException("Invalid seek origin", "origin"); }
                    long position;
                    try { position = checked(basis + offset); } catch (OverflowException error) { throw new IOException("Invalid stream position", error); }
                    if (position < 0) throw new IOException("Cannot seek before the start of the stream");
                    random.Seek((ulong)position); return position;
                }
                finally { gate.Release(); }
            }
            protected override void Dispose(bool disposing)
            {
                if (disposing)
                {
                    gate.Wait();
                    try { if (source != null) { IInputStream value = source; source = null; value.Dispose(); } }
                    finally { gate.Release(); }
                }
                base.Dispose(disposing);
            }
        }
    }
}
namespace System.Runtime.InteropServices.WindowsRuntime
{
    public static class WindowsRuntimeBufferExtensions
    {
        public static IBuffer AsBuffer(this byte[] source)
        {
            if (source == null) throw new ArgumentNullException("source");
            return ArrayBuffer.Create(source);
        }
        public static byte[] ToArray(this IBuffer source)
        {
            if (source == null) throw new ArgumentNullException("source");
            byte[] result = new byte[checked((int)source.Length)];
            StorageBuffer.Copy(source, result, 0, result.Length, false);
            return result;
        }
    }
}
