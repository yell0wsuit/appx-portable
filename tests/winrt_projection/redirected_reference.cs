// The CLR assemblies games bind redirected WinRT structs to, as .NET defines them.
// Built twice: System.Runtime.WindowsRuntime, and with XAML its UI.Xaml sibling.
[assembly: System.Reflection.AssemblyVersion("4.0.0.0")]
#if XAML
namespace Windows.UI.Xaml
{
    public enum DurationType { Automatic, TimeSpan, Forever }
    public struct Thickness
    {
        public Thickness(double uniformLength) { throw new global::System.NotImplementedException(); }
        public Thickness(double left, double top, double right, double bottom) { throw new global::System.NotImplementedException(); }
        public double Left { get { throw new global::System.NotImplementedException(); } set { } }
        public double Top { get { throw new global::System.NotImplementedException(); } set { } }
        public double Right { get { throw new global::System.NotImplementedException(); } set { } }
        public double Bottom { get { throw new global::System.NotImplementedException(); } set { } }
    }
    public struct Duration
    {
        public Duration(global::System.TimeSpan timeSpan) { throw new global::System.NotImplementedException(); }
        public static implicit operator Duration(global::System.TimeSpan timeSpan) { throw new global::System.NotImplementedException(); }
        public static Duration Automatic { get { throw new global::System.NotImplementedException(); } }
        public static Duration Forever { get { throw new global::System.NotImplementedException(); } }
        public bool HasTimeSpan { get { throw new global::System.NotImplementedException(); } }
        public global::System.TimeSpan TimeSpan { get { throw new global::System.NotImplementedException(); } }
    }
}
#else
namespace Windows.Foundation
{
    public struct Point
    {
        public Point(double x, double y) { throw new global::System.NotImplementedException(); }
        public double X { get { throw new global::System.NotImplementedException(); } set { } }
        public double Y { get { throw new global::System.NotImplementedException(); } set { } }
    }
    public struct Size
    {
        public Size(double width, double height) { throw new global::System.NotImplementedException(); }
        public double Width { get { throw new global::System.NotImplementedException(); } set { } }
        public double Height { get { throw new global::System.NotImplementedException(); } set { } }
        public static Size Empty { get { throw new global::System.NotImplementedException(); } }
        public bool IsEmpty { get { throw new global::System.NotImplementedException(); } }
    }
    public struct Rect
    {
        public Rect(double x, double y, double width, double height) { throw new global::System.NotImplementedException(); }
        public Rect(Point point1, Point point2) { throw new global::System.NotImplementedException(); }
        public Rect(Point location, Size size) { throw new global::System.NotImplementedException(); }
        public double X { get { throw new global::System.NotImplementedException(); } set { } }
        public double Y { get { throw new global::System.NotImplementedException(); } set { } }
        public double Width { get { throw new global::System.NotImplementedException(); } set { } }
        public double Height { get { throw new global::System.NotImplementedException(); } set { } }
        public double Left { get { throw new global::System.NotImplementedException(); } }
        public double Top { get { throw new global::System.NotImplementedException(); } }
        public double Right { get { throw new global::System.NotImplementedException(); } }
        public double Bottom { get { throw new global::System.NotImplementedException(); } }
        public static Rect Empty { get { throw new global::System.NotImplementedException(); } }
        public bool IsEmpty { get { throw new global::System.NotImplementedException(); } }
        public bool Contains(Point point) { throw new global::System.NotImplementedException(); }
    }
}
namespace Windows.UI
{
    public struct Color
    {
        public byte A { get { throw new global::System.NotImplementedException(); } set { } }
        public byte R { get { throw new global::System.NotImplementedException(); } set { } }
        public byte G { get { throw new global::System.NotImplementedException(); } set { } }
        public byte B { get { throw new global::System.NotImplementedException(); } set { } }
        public static Color FromArgb(byte a, byte r, byte g, byte b) { throw new global::System.NotImplementedException(); }
    }
}
#endif
