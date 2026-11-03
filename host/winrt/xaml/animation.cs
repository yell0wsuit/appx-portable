using AppxPortable.WinRT.Xaml;

namespace Windows.UI.Xaml.Media.Animation
{
    public sealed partial class Storyboard
    {
        private global::System.Windows.Media.Animation.Storyboard Animation
        {
            get { return (global::System.Windows.Media.Animation.Storyboard)Values.Dependency(this); }
        }

        // Targets come from Storyboard.Target/TargetName on the children, as in UWP.
        public void Begin() { Animation.Begin(); }

        public void Stop() { Animation.Stop(); }
    }
}

namespace AppxPortable.WinRT.Xaml
{
    // Theme fades animate Opacity; their exact Windows timing is not reproduced.
    internal sealed class FadeInPeer : System.Windows.Media.Animation.DoubleAnimation
    {
        public FadeInPeer()
        {
            To = 1;
            Duration = new System.Windows.Duration(System.TimeSpan.FromMilliseconds(300));
            System.Windows.Media.Animation.Storyboard.SetTargetProperty(this, new System.Windows.PropertyPath(System.Windows.UIElement.OpacityProperty));
        }

        protected override System.Windows.Freezable CreateInstanceCore() { return new FadeInPeer(); }
    }

    internal sealed class FadeOutPeer : System.Windows.Media.Animation.DoubleAnimation
    {
        public FadeOutPeer()
        {
            To = 0;
            Duration = new System.Windows.Duration(System.TimeSpan.FromMilliseconds(300));
            System.Windows.Media.Animation.Storyboard.SetTargetProperty(this, new System.Windows.PropertyPath(System.Windows.UIElement.OpacityProperty));
        }

        protected override System.Windows.Freezable CreateInstanceCore() { return new FadeOutPeer(); }
    }
}
