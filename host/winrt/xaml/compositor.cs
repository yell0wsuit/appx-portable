using System;
using System.Runtime.InteropServices;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace AppxPortable.WinRT.Xaml
{
    // Puts the window's WPF tree on screen: a hidden HwndSource gives it a
    // presentation source (layout, templates, Loaded), and every tick of the
    // shim's CompositionTarget clock renders it offscreen for the shim to blend
    // over the game's swap chain when the game presents.
    internal static partial class Host
    {
        [DllImport("appxportable.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        private static extern int ShimSubmitXamlFrame(byte[] bgra, uint width, uint height, uint stride);
        [DllImport("user32.dll", ExactSpelling = true)]
        private static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
        private const uint NoMoveZOrderActivate = 0x0002 | 0x0004 | 0x0010;  // SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
        // WPF lays out and raises Loaded only for a shown window, so the source is a
        // visible, non-activating tool popup kept off-screen.
        private const int PopupStyle = unchecked((int)(0x80000000 | 0x10000000));  // WS_POPUP | WS_VISIBLE
        private const int ToolNoActivate = 0x00000080 | 0x08000000;               // WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE
        private const int OffScreen = -32000;

        private static HwndSource presentation;
        private static RenderTargetBitmap target;
        private static byte[] pixels;
        private static int width, height;
        private static bool failed;

        // Frames handed to the shim, for diagnostics and probes.
        internal static int Frames { get; private set; }

        internal static void EnsurePresentation()
        {
            if (presentation != null) return;
            var parameters = new HwndSourceParameters("AppxPortableXaml")
            {
                WindowStyle = PopupStyle, ExtendedWindowStyle = ToolNoActivate,
                PositionX = OffScreen, PositionY = OffScreen, Width = 1, Height = 1,
            };
            presentation = new HwndSource(parameters) { RootVisual = Layers };
            global::Windows.UI.Xaml.Media.CompositionTarget.add_Rendering((sender, args) => Render());
        }

        private static void Render()
        {
            if (failed) return;
            try
            {
                var bounds = global::Windows.UI.Xaml.Window.Current.Bounds;
                int w = (int)Math.Round(bounds.Width), h = (int)Math.Round(bounds.Height);
                if (w <= 0 || h <= 0) return;
                if (w != width || h != height)
                {
                    width = w; height = h;
                    SetWindowPos(presentation.Handle, IntPtr.Zero, 0, 0, w, h, NoMoveZOrderActivate);
                    target = new RenderTargetBitmap(w, h, 96, 96, PixelFormats.Pbgra32);
                    pixels = new byte[w * h * 4];
                }
                Layers.UpdateLayout();
                target.Clear();
                target.Render(Layers);
                target.CopyPixels(pixels, w * 4, 0);
                Errors.Check(ShimSubmitXamlFrame(pixels, (uint)w, (uint)h, (uint)(w * 4)));
                ++Frames;
            }
            catch (Exception error)
            {
                // The game's own frames keep going without the XAML layer.
                failed = true;
                Errors.Log(error);
                ShimSubmitXamlFrame(null, 0, 0, 0);
            }
        }
    }
}
