// Compiled against the CLR redirection assemblies, as games are, and run against
// appx-portable's forwarding facades: each struct must bind to Windows.dll.
using System;
using System.Runtime.InteropServices;
using Windows.Foundation;
using Windows.UI;
using Windows.UI.Xaml;
class RedirectedTest
{
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    static void Throws<T>(Action action, string message) where T : Exception
    {
        try { action(); }
        catch (T) { return; }
        throw new Exception(message);
    }
    static int Main()
    {
        try
        {
            foreach (Type type in new[] { typeof(Point), typeof(Size), typeof(Rect), typeof(Color), typeof(Thickness), typeof(Duration), typeof(DurationType) })
                Check(type.Assembly.GetName().Name == "Windows", type.FullName + " is not defined by Windows.dll");
            // SDK ABI sizes: FLOAT pairs/quads, four BYTEs, four DOUBLEs, INT64 + enum.
            Check(Marshal.SizeOf(typeof(Point)) == 8 && Marshal.SizeOf(typeof(Size)) == 8 && Marshal.SizeOf(typeof(Rect)) == 16, "foundation struct sizes");
            Check(Marshal.SizeOf(typeof(Color)) == 4 && Marshal.SizeOf(typeof(Thickness)) == 32 && Marshal.SizeOf(typeof(Duration)) == 16, "UI struct sizes");
            var point = new Point(1.5, -2);
            Check(point.X == 1.5 && point.Y == -2, "Point");
            var size = new Size(3, 4);
            Check(size.Width == 3 && size.Height == 4 && !size.IsEmpty && Size.Empty.IsEmpty, "Size");
            Throws<ArgumentException>(() => new Size(-1, 0), "negative Size accepted");
            var rect = new Rect(10, 20, 30, 40);
            Check(rect.X == 10 && rect.Y == 20 && rect.Width == 30 && rect.Height == 40, "Rect fields");
            Check(rect.Left == 10 && rect.Top == 20 && rect.Right == 40 && rect.Bottom == 60, "Rect edges");
            Check(rect.Contains(new Point(40, 60)) && !rect.Contains(new Point(41, 60)), "Rect.Contains");
            Check(Rect.Empty.IsEmpty && !rect.IsEmpty && double.IsNegativeInfinity(Rect.Empty.Right), "Rect.Empty");
            var spanned = new Rect(new Point(5, 9), new Point(1, 2));
            Check(spanned.X == 1 && spanned.Y == 2 && spanned.Width == 4 && spanned.Height == 7, "Rect from points");
            var sized = new Rect(point, size);
            Check(sized.X == 1.5 && sized.Width == 3, "Rect from point and size");
            Throws<ArgumentException>(() => new Rect(0, 0, -1, 0), "negative Rect accepted");
            var color = Color.FromArgb(1, 2, 3, 4);
            Check(color.A == 1 && color.R == 2 && color.G == 3 && color.B == 4, "Color");
            var margin = new Thickness(1, 2, 3, 4);
            Check(margin.Left == 1 && margin.Top == 2 && margin.Right == 3 && margin.Bottom == 4 && new Thickness(5).Bottom == 5, "Thickness");
            Duration duration = TimeSpan.FromSeconds(2);
            Check(duration.HasTimeSpan && duration.TimeSpan == TimeSpan.FromSeconds(2), "implicit Duration");
            Check(!Duration.Automatic.HasTimeSpan && !Duration.Forever.HasTimeSpan, "special Durations");
            Throws<InvalidOperationException>(() => { var ignored = Duration.Forever.TimeSpan; }, "Forever had a TimeSpan");
            Console.WriteLine("PASS: CLR-redirected structs bind through the facades to Windows.dll");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
