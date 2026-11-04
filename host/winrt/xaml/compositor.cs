using System;
using System.Runtime.InteropServices;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace AppxPortable.WinRT.Xaml
{
    // Puts the window's WPF tree on screen: an off-screen layered HwndSource gives
    // it a presentation source (layout, templates, Loaded, input), and WPF's own
    // compositor renders it there, only where something changed. The shim takes
    // each layered present as the XAML frame (never showing the window) and blends
    // it over the game's swap chain when the game presents. The host window's
    // mouse messages are forwarded to that source.
    internal static partial class Host
    {
        [DllImport("appxportable.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        private static extern int ShimSubmitXamlFrame(byte[] bgra, uint width, uint height, uint stride);
        [DllImport("appxportable.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        private static extern int ShimCaptureXamlWindow(IntPtr window);
        [DllImport("appxportable.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        private static extern uint ShimXamlFrameCount();
        [DllImport("user32.dll", ExactSpelling = true)]
        private static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
        private const uint NoMoveZOrderActivate = 0x0002 | 0x0004 | 0x0010;  // SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
        // WPF lays out and raises Loaded only for a shown window, so the source is a
        // visible, non-activating tool popup kept off-screen.
        private const int PopupStyle = unchecked((int)(0x80000000 | 0x10000000));  // WS_POPUP | WS_VISIBLE
        private const int ToolNoActivate = 0x00000080 | 0x08000000;               // WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE
        private const int OffScreen = -16000;

        private static HwndSource presentation;
        private static RenderTargetBitmap target;
        private static byte[] pixels;
        private static int width, height;
        private static bool failed, layered;
        private static int rendered;

        // Frames handed to the shim, for diagnostics and probes.
        internal static int Frames { get { return layered ? (int)ShimXamlFrameCount() : rendered; } }

        internal static void EnsurePresentation()
        {
            if (presentation != null) return;
            var parameters = new HwndSourceParameters("AppxPortableXaml")
            {
                WindowStyle = PopupStyle,
                ExtendedWindowStyle = ToolNoActivate,
                PositionX = OffScreen,
                PositionY = OffScreen,
                Width = Math.Max(1, (int)Math.Round(global::Windows.UI.Xaml.Window.Current.Bounds.Width)),
                Height = Math.Max(1, (int)Math.Round(global::Windows.UI.Xaml.Window.Current.Bounds.Height)),
                UsesPerPixelOpacity = true,
            };
            presentation = new HwndSource(parameters);
            // Before the tree is attached, so the first, whole frame is taken too.
            // Without the shim's capture (x64) every tick renders the tree itself.
            layered = ShimCaptureXamlWindow(presentation.Handle) >= 0;
            if (!layered)
            {
                // Do not run WPF's layered renderer alongside the bitmap fallback.
                presentation.Dispose();
                parameters.UsesPerPixelOpacity = false;
                presentation = new HwndSource(parameters);
            }
            presentation.RootVisual = Layers;
            global::Windows.UI.Xaml.Media.CompositionTarget.add_Rendering((sender, args) => Render());
            ForwardInput();
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void InputHandler(uint message, IntPtr wparam, IntPtr lparam);
        [DllImport("appxportable.dll", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        private static extern int ShimSetXamlInputHandler(IntPtr source, InputHandler handler);
        [DllImport("user32.dll", ExactSpelling = true)]
        private static extern IntPtr SendMessageW(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
        [DllImport("user32.dll", ExactSpelling = true)]
        private static extern IntPtr GetCapture();
        [DllImport("user32.dll", ExactSpelling = true)]
        private static extern IntPtr SetCapture(IntPtr window);
        [DllImport("user32.dll", ExactSpelling = true)]
        private static extern bool ReleaseCapture();
        [DllImport("user32.dll", ExactSpelling = true)]
        private static extern bool ClientToScreen(IntPtr window, ref Position point);
        [StructLayout(LayoutKind.Sequential)]
        private struct Position { public int X, Y; }
        private const uint MouseQuery = 0x009B, MouseMove = 0x0200, MouseWheel = 0x020A, CaptureChanged = 0x0215, MouseLeave = 0x02A3;
        private const int AnyButton = 0x0001 | 0x0002 | 0x0010 | 0x0020 | 0x0040;  // MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2
        private static InputHandler input;  // the shim holds only the function pointer

        // WPF's mouse provider has a mode for input another party forwards (the
        // DWM's WM_MOUSEQUERY): it then leaves Win32 capture and leave tracking
        // alone, which here belong to the host window. A query naming no mouse
        // message only switches the mode.
        private static void ForwardInput()
        {
            var query = Marshal.AllocHGlobal(64);
            try
            {
                for (int offset = 0; offset < 64; offset += 4) Marshal.WriteInt32(query, offset, 0);
                SendMessageW(presentation.Handle, MouseQuery, IntPtr.Zero, query);
            }
            finally { Marshal.FreeHGlobal(query); }
            input = OnInput;
            Errors.Check(ShimSetXamlInputHandler(presentation.Handle, input));
        }

        // A host mouse message in client pixels, which are XAML's DIPs (the tree
        // renders at the client size); WPF reads the position in its own device pixels.
        private static void OnInput(uint message, IntPtr wparam, IntPtr lparam)
        {
            try
            {
                if (presentation == null || presentation.IsDisposed || presentation.CompositionTarget == null) return;
                if (message == MouseLeave)
                {
                    // The forwarded-input provider ignores leave: move its pointer off
                    // the tree, which ends every hover. A press dragged outside keeps going.
                    if (System.Windows.Input.Mouse.PrimaryDevice.ActiveSource == presentation && System.Windows.Input.Mouse.Captured == null &&
                        (unchecked((int)(long)Buttons(wparam)) & AnyButton) == 0)
                        SendMessageW(presentation.Handle, MouseMove, IntPtr.Zero, Pack(new Position { X = -1, Y = -1 }));
                    return;
                }
                var toDevice = presentation.CompositionTarget.TransformToDevice;
                int packed = unchecked((int)(long)lparam);
                var at = new Position
                {
                    X = (int)Math.Round((short)(packed & 0xFFFF) * toDevice.M11),
                    Y = (int)Math.Round((short)((packed >> 16) & 0xFFFF) * toDevice.M22)
                };
                var window = presentation.Handle;
                // The provider takes a source as the mouse's only while it holds
                // capture (or the real cursor is over it, which it never is):
                // activate it with a move at the same point, then give capture back.
                if (System.Windows.Input.Mouse.PrimaryDevice.ActiveSource != presentation)
                {
                    var owner = GetCapture();
                    SetCapture(window);
                    SendMessageW(window, MouseMove, Buttons(wparam), Pack(at));
                    if (owner != IntPtr.Zero) SetCapture(owner); else ReleaseCapture();
                    // Activation records the point without hit-testing it, and a
                    // message at the same point is no move: find what is under it.
                    System.Windows.Input.Mouse.Synchronize();
                }
                if (message == MouseWheel) ClientToScreen(window, ref at);
                SendMessageW(window, message, wparam, Pack(at));
                // In this mode WPF's capture release is a no-op that waits for a
                // WM_CAPTURECHANGED never sent. Its controls hold capture only
                // while a button is down, as the host window does: once none is,
                // end it as Win32 would.
                if ((unchecked((int)(long)Buttons(wparam)) & AnyButton) == 0 && System.Windows.Input.Mouse.Captured != null)
                    SendMessageW(window, CaptureChanged, IntPtr.Zero, IntPtr.Zero);
            }
            catch (Exception error)
            {
                // Native code called this; the host window's next message tries again.
                Errors.Log(error);
            }
        }

        private static IntPtr Pack(Position at) { return (IntPtr)((at.Y << 16) | (at.X & 0xFFFF)); }

        // A wheel message's wparam carries the delta above the button flags.
        private static IntPtr Buttons(IntPtr wparam) { return (IntPtr)(unchecked((int)(long)wparam) & 0xFFFF); }

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
                    if (!layered)
                    {
                        target = new RenderTargetBitmap(w, h, 96, 96, PixelFormats.Pbgra32);
                        pixels = new byte[w * h * 4];
                    }
                }
                if (layered) return;  // WPF presents the window's changes itself
                Layers.UpdateLayout();
                target.Clear();
                target.Render(Layers);
                target.CopyPixels(pixels, w * 4, 0);
                Errors.Check(ShimSubmitXamlFrame(pixels, (uint)w, (uint)h, (uint)(w * 4)));
                ++rendered;
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
