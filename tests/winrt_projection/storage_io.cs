using System;
using System.IO;
using System.Linq;
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
        // The game's tips path: package folder, file lookup, then FileIO text.
        StorageFolder installed = Windows.ApplicationModel.Package.Current.InstalledLocation;
        Check(string.Equals(Path.GetFullPath(installed.Path).TrimEnd('\\'), Path.GetFullPath(".").TrimEnd('\\'), StringComparison.OrdinalIgnoreCase),
            "InstalledLocation is the package folder");
        Check((await installed.GetFileAsync(Path.GetFileName(path))).Path == path, "InstalledLocation file lookup");
        string text = "tips 💎 é\r\nsecond line\r\n";
        var encodings = new[] {
            new { Name = "text-plain.txt", Bytes = System.Text.Encoding.UTF8.GetBytes(text) },
            new { Name = "text-utf8.txt", Bytes = new byte[] { 0xef, 0xbb, 0xbf }.Concat(System.Text.Encoding.UTF8.GetBytes(text)).ToArray() },
            new { Name = "text-utf16le.txt", Bytes = new byte[] { 0xff, 0xfe }.Concat(System.Text.Encoding.Unicode.GetBytes(text)).ToArray() },
            new { Name = "text-utf16be.txt", Bytes = new byte[] { 0xfe, 0xff }.Concat(System.Text.Encoding.BigEndianUnicode.GetBytes(text)).ToArray() } };
        foreach (var encoded in encodings)
        {
            File.WriteAllBytes(encoded.Name, encoded.Bytes);
            Check(await FileIO.ReadTextAsync(await installed.GetFileAsync(encoded.Name)) == text, "FileIO text decoding " + encoded.Name);
        }
        // Windows (windows.storage.dll CEncodingHelpers) checks for a byte-order mark only when
        // at least 3 bytes exist, and decodes UTF-16 as ReadString(bytes / 2).
        File.WriteAllBytes("text-short-bom.txt", new byte[] { 0xff, 0xfe });
        try { await FileIO.ReadTextAsync(await installed.GetFileAsync("text-short-bom.txt")); throw new Exception("2-byte BOM decoded as UTF-16"); }
        catch (Exception error) { if (error.HResult != unchecked((int)0x80070459)) throw; }
        File.WriteAllBytes("text-odd.txt", new byte[] { 0xff, 0xfe, 0x41, 0x00, 0x42 });
        Check(await FileIO.ReadTextAsync(await installed.GetFileAsync("text-odd.txt")) == "A", "odd UTF-16 trailing byte is dropped");
        File.WriteAllBytes("text-empty.txt", new byte[0]);
        Check(await FileIO.ReadTextAsync(await installed.GetFileAsync("text-empty.txt")) == "", "FileIO empty text");
        // Windows rejects bytes that are not UTF-8 when no BOM names another encoding.
        File.WriteAllBytes("text-invalid.txt", new byte[] { 0x41, 0xff, 0x42 });
        try { await FileIO.ReadTextAsync(await installed.GetFileAsync("text-invalid.txt")); throw new Exception("invalid UTF-8 accepted"); }
        catch (Exception error) { if (error.HResult != unchecked((int)0x80070459)) throw; }
        StorageFile vanished = await installed.GetFileAsync("text-empty.txt");
        File.Delete("text-empty.txt");
        try { await FileIO.ReadTextAsync(vanished); throw new Exception("deleted file read"); } catch (FileNotFoundException) { }
        // Windows fails a null file synchronously with E_POINTER.
        try { GC.KeepAlive(FileIO.ReadTextAsync(null)); throw new Exception("null file accepted"); } catch (NullReferenceException) { }
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
