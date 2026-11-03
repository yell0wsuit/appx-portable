using System;
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

namespace AppxPortable.WinRT.Xaml
{
    // UWP converts a string key-frame value of ObjectAnimationUsingKeyFrames
    // ("Collapsed", "1,0,0,1,-256,-256", "/Assets/a.png") to the animated
    // property's type when the state's storyboard applies; WPF keeps the string,
    // which is no valid value for the property. The loader attaches this manager
    // wherever markup sets VisualStateGroups, so every state change, including
    // the ones WPF controls make themselves, converts first.
    public sealed class UwpVisualStateManager : System.Windows.VisualStateManager
    {
        internal static readonly UwpVisualStateManager Instance = new UwpVisualStateManager();
        private static readonly System.Runtime.CompilerServices.ConditionalWeakTable<System.Windows.Media.Animation.Storyboard, object> prepared =
            new System.Runtime.CompilerServices.ConditionalWeakTable<System.Windows.Media.Animation.Storyboard, object>();

        protected override bool GoToStateCore(System.Windows.FrameworkElement control, System.Windows.FrameworkElement stateGroupsRoot, string stateName,
            System.Windows.VisualStateGroup group, System.Windows.VisualState state, bool useTransitions)
        {
            // WPF calls a custom manager for names the template lacks too; its default returns false.
            if (state == null) return false;
            if (state.Storyboard != null) state.Storyboard = Prepare(state.Storyboard, control, stateGroupsRoot);
            if (group != null)
                foreach (System.Windows.VisualTransition transition in group.Transitions)
                    if (transition.Storyboard != null) transition.Storyboard = Prepare(transition.Storyboard, control, stateGroupsRoot);
            return base.GoToStateCore(control, stateGroupsRoot, stateName, group, state, useTransitions);
        }

        private static System.Windows.Media.Animation.Storyboard Prepare(System.Windows.Media.Animation.Storyboard storyboard,
            System.Windows.FrameworkElement control, System.Windows.FrameworkElement root)
        {
            object done;
            if (prepared.TryGetValue(storyboard, out done)) return storyboard;
            var result = storyboard.IsFrozen ? storyboard.Clone() : storyboard;
            Convert(result, control, root);
            prepared.Add(result, null);
            return result;
        }

        private static void Convert(System.Windows.Media.Animation.TimelineGroup timelines, System.Windows.FrameworkElement control, System.Windows.FrameworkElement root)
        {
            foreach (var timeline in timelines.Children)
            {
                var group = timeline as System.Windows.Media.Animation.TimelineGroup;
                if (group != null) { Convert(group, control, root); continue; }
                var animation = timeline as System.Windows.Media.Animation.ObjectAnimationUsingKeyFrames;
                if (animation == null) continue;
                var property = Target(animation, control, root);
                if (property == null) continue;
                foreach (System.Windows.Media.Animation.ObjectKeyFrame frame in animation.KeyFrames)
                {
                    var text = frame.Value as string;
                    if (text != null && property.PropertyType != typeof(string) && property.PropertyType != typeof(object))
                        frame.Value = Value(text, property.PropertyType);
                }
            }
        }

        // The animated property: one step, named by the path or given as its parameter.
        private static System.Windows.DependencyProperty Target(System.Windows.Media.Animation.Timeline animation,
            System.Windows.FrameworkElement control, System.Windows.FrameworkElement root)
        {
            var path = System.Windows.Media.Animation.Storyboard.GetTargetProperty(animation);
            if (path == null) return null;
            if (path.PathParameters.Count == 1 && path.Path == "(0)") return path.PathParameters[0] as System.Windows.DependencyProperty;
            if (path.Path.IndexOfAny(new[] { '.', '(', '[' }) >= 0) return null;
            object target = System.Windows.Media.Animation.Storyboard.GetTarget(animation);
            string name = System.Windows.Media.Animation.Storyboard.GetTargetName(animation);
            if (target == null && name != null)
            {
                target = root.FindName(name);
                var templated = control as System.Windows.Controls.Control;
                if (target == null && templated != null && templated.Template != null) target = templated.Template.FindName(name, templated);
            }
            if (target == null) target = control;
            var descriptor = System.ComponentModel.DependencyPropertyDescriptor.FromName(path.Path, target.GetType(), target.GetType());
            return descriptor == null ? null : descriptor.DependencyProperty;
        }

        private static object Value(string text, Type type)
        {
            if (type == typeof(System.Windows.FontWeight) && text == "SemiLight") return System.Windows.FontWeight.FromOpenTypeWeight(350);
            if (typeof(System.Windows.Media.ImageSource).IsAssignableFrom(type))
            {
                Uri uri;
                if (!Uri.TryCreate(text, UriKind.Absolute, out uri) || uri.Scheme == "ms-appx")
                    uri = new Uri(XamlLoader.Resolve(new Uri("ms-appx:///" + text.TrimStart('/').Replace("ms-appx:///", "")), false), UriKind.Absolute);
                var bitmap = new System.Windows.Media.Imaging.BitmapImage();
                bitmap.BeginInit();
                bitmap.UriSource = uri;
                bitmap.CreateOptions = System.Windows.Media.Imaging.BitmapCreateOptions.DelayCreation;
                bitmap.EndInit();
                return bitmap;
            }
            var converter = System.ComponentModel.TypeDescriptor.GetConverter(type);
            return converter != null && converter.CanConvertFrom(typeof(string)) ? converter.ConvertFromInvariantString(text) : text;
        }
    }
}
