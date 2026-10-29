using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Threading.Tasks;

using Windows.Storage;
using Windows.Storage.Streams;
class StorageIoProjectionTest
{
    [DllImport("combase.dll")] static extern int RoInitialize(uint apartment);
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    static async Task Run()
    {
        byte[] shared = new byte[] { 1, 2, 3 };
        IBuffer sharedBuffer = shared.AsBuffer(); shared[0] = 9;
        Check(sharedBuffer.ToArray()[0] == 9, "AsBuffer must share array storage");
        var capacity = new Windows.Storage.Streams.Buffer(3);
        Check(capacity.Capacity == 3 && capacity.Length == 0, "native buffer capacity/default length");
        try { capacity.Length = 4; throw new Exception("buffer length overflow accepted"); } catch (ArgumentException) { }
        try { new Windows.Storage.Streams.Buffer(uint.MaxValue); throw new Exception("oversized buffer accepted"); } catch (OutOfMemoryException) { }
        string path = Path.GetFullPath("payload-đá.bin");
        byte[] bytes = System.Text.Encoding.UTF8.GetBytes("native bytes 💎\0tail\n");
        File.WriteAllBytes(path, bytes);
        var lookup = StorageFile.GetFileFromPathAsync(path);
        Check(lookup.Id != 0, "native async ID");
        StorageFile file = await lookup;
        Check(file.Path == path && file.Name == Path.GetFileName(path), "StorageFile metadata");
        using (IRandomAccessStreamWithContentType native = await file.OpenReadAsync())
        {
            IBuffer written = await native.ReadAsync(sharedBuffer, 3, InputStreamOptions.None);
            Check(ReferenceEquals(written, sharedBuffer) && shared[0] == bytes[0] && shared[2] == bytes[2], "native read updates the original array");
        }
        using (IRandomAccessStreamWithContentType native = await file.OpenReadAsync())
        using (Stream stream = native.AsStream())
        {
            Check(stream.CanRead && stream.CanSeek && !stream.CanWrite, "read-only stream capabilities");
            Check(stream.Length == bytes.Length && stream.Position == 0, "stream size/position");
            byte[] actual = new byte[bytes.Length + 7];
            int first = stream.Read(actual, 2, 3);
            Check(first == 3 && actual[2] == bytes[0] && stream.Position == 3, "offset read and native position");
            stream.Seek(-2, SeekOrigin.End);
            Check(stream.ReadByte() == bytes[bytes.Length - 2] && stream.ReadByte() == bytes[bytes.Length - 1] && stream.ReadByte() == -1, "seek and EOF");
            stream.Seek((1L << 33) + 7, SeekOrigin.Begin);
            Check(stream.Position == (1L << 33) + 7 && stream.ReadByte() == -1, "64-bit stream position on x86");
            stream.Seek(0, SeekOrigin.Begin);
            int read = await stream.ReadAsync(actual, 1, bytes.Length);
            Check(read == bytes.Length, "async read length");
            for (int i = 0; i < read; ++i) Check(actual[i + 1] == bytes[i], "native file bytes");
            stream.Seek(0, SeekOrigin.Begin);
            using (var reader = new StreamReader(stream, System.Text.Encoding.UTF8, true, 128, true))
                Check(await reader.ReadToEndAsync() == System.Text.Encoding.UTF8.GetString(bytes), "StreamReader UTF8/NUL");
            try { stream.WriteByte(1); throw new Exception("write accepted"); } catch (NotSupportedException) { }
            try { stream.Seek(-1, SeekOrigin.Begin); throw new Exception("negative seek accepted"); } catch (IOException) { }
        }
        var local = Windows.Storage.ApplicationData.Current.LocalFolder;
        string localPath = Path.Combine(local.Path, "saved.bin"); File.WriteAllBytes(localPath, bytes);
        StorageFile saved = await local.GetFileAsync("saved.bin");
        Check(saved.Path == localPath, "LocalFolder native lookup");
        Directory.CreateDirectory(Path.Combine(local.Path, "sub"));
        File.WriteAllBytes(Path.Combine(local.Path, "sub", "nested.bin"), bytes);
        Check((await local.GetFileAsync(@"sub\nested.bin")).Name == "nested.bin", "relative subfolder lookup");
        try { await local.GetFileAsync("absent.bin"); throw new Exception("missing local file accepted"); } catch (FileNotFoundException) { }
        try { await local.GetFileAsync(@"..\escape.bin"); throw new Exception("parent traversal accepted"); } catch (ArgumentException) { }
        try { await StorageFile.GetFileFromPathAsync(path + "\0tail"); throw new Exception("NUL path accepted"); } catch (ArgumentException) { }
        var missing = StorageFile.GetFileFromPathAsync(path + ".absent");
        try { await missing; throw new Exception("missing file accepted"); }
        catch (FileNotFoundException) { }
        Check(missing.Status == Windows.Foundation.AsyncStatus.Error && missing.ErrorCode != null, "native async ErrorCode");
        byte[] copied = bytes.AsBuffer().ToArray();
        Check(copied.Length == bytes.Length && copied[0] == bytes[0], "buffer extensions");
        Console.WriteLine("PASS: managed storage awaits native files and reads seekable streams and buffers");
    }
    [MTAThread]
    static int Main()
    {
        try { Marshal.ThrowExceptionForHR(RoInitialize(1)); Run().GetAwaiter().GetResult(); return 0; }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
