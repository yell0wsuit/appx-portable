using System;
using AppxPortable.WinRT.Xaml;

namespace Windows.UI.Xaml.Media.Imaging
{
    public sealed partial class BitmapImage
    {
        public BitmapImage(Uri uriSource) : this(new global::System.Windows.Media.Imaging.BitmapImage())
        {
            if (uriSource == null) throw new ArgumentNullException("uriSource");
            Peers.Attach(this);
            var bitmap = (global::System.Windows.Media.Imaging.BitmapImage)peer;
            Uri uri = uriSource.IsAbsoluteUri ? uriSource : new Uri("ms-appx:///" + uriSource.OriginalString.TrimStart('/'));
            if (uri.Scheme == "ms-appx") uri = new Uri(XamlLoader.Resolve(uri, false), UriKind.Absolute);
            bitmap.BeginInit();
            bitmap.CreateOptions = global::System.Windows.Media.Imaging.BitmapCreateOptions.DelayCreation;
            bitmap.UriSource = uri;
            bitmap.EndInit();
        }
    }
}
