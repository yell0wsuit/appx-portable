using AppxPortable.WinRT.Xaml;

namespace Windows.UI.Xaml
{
    public partial class VisualStateManager
    {
        // States live in the control's template root, as WPF and UWP both keep them.
        public static bool GoToState(Windows.UI.Xaml.Controls.Control control, string stateName, bool useTransitions)
        {
            if (control == null) throw new global::System.ArgumentNullException("control");
            return global::System.Windows.VisualStateManager.GoToState((global::System.Windows.FrameworkElement)Values.Dependency(control), stateName, useTransitions);
        }
    }
}
