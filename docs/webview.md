# WebView in managed XAML Islands apps

Code-created `Windows.UI.Xaml.Controls.WebView` instances use a WebView2-backed
stand-in. Its COM outer composes a system Grid, so CLR callers can use the normal
FrameworkElement/UIElement APIs and put it in the XAML tree. System XAML's own
activations and native games keep the system factory.

Implemented: default activation and the execution-mode constructor,
`NavigateToString`, `Navigate`/`Source`, synchronous `InvokeScript`, `ScriptNotify`
(including `Value` and `CallingUri`), `LoadCompleted`, and `NavigationFailed`.
Static interfaces are forwarded to Windows. Package URLs in navigation and HTML
(`ms-appx-web:///` and `ms-appx:///`) map to a WebView2 virtual host serving the
package folder. Returned local event URIs use `ms-appx-web:///`.

The HWND controller follows the element's layout, rasterization scale, tree
attachment, and ancestor visibility. Its browser profile lives in `Local/WebView2`
under the configured save folder. Browser notifications dispatch back through XAML
before invoking game handlers, allowing the legacy synchronous script API to pump
messages without reentering a WebView2 callback. Script calls time out after ten
seconds. ScriptNotify is delivered only for local package or NavigateToString
content; remote ApplicationContentUriRules support is not implemented.

The SDK is pinned and hash checked in `cmake/webview2.cmake`. Builds download it
from NuGet; an offline build can set `FETCHCONTENT_SOURCE_DIR_APPX_WEBVIEW2` to the
unpacked SDK. The static loader's redistribution notice is included in build and
release output and copied by the installer. No Microsoft binaries are checked in.
The Evergreen WebView2 runtime must already be installed to render content; a
missing runtime is logged and subsequent navigation/script calls return the error.
Other game controls do not need the runtime.

## Validation

`webviewtest` checks static forwarding, composed-element properties, COM identity,
and repeated creation/destruction, without needing the browser runtime. The opt-in
`managed_webview` integration test checks CLR projection, HTML supplied before
Loaded, package scripts and navigation, notification URIs, synchronous JavaScript
from a notification handler, and LoadCompleted:

```powershell
cmake -S . -B build/ci-fix-x86 -DAPPX_TEST_WEBVIEW2=ON
cmake --build build/ci-fix-x86 --config RelWithDebInfo
ctest --test-dir build/ci-fix-x86 -C RelWithDebInfo -R '^(webviewtest|managed_webview)$' --output-on-failure
```

OALD 2.6.21.9713's Help page was exercised in an isolated installation: the HTML
renders and sends its `loaded` notification. Its `BaseArticleComponent.WebViewBrush`
getter has no callers in the executable's IL; snapshot brushes are deferred.

## Remaining work

- Markup-created WebViews still bypass the activation shim. Practical English
  Usage therefore still needs an XBF/metadata solution before its page can load.
- `IWebView2` APIs (`InvokeScriptAsync`, NavigationCompleted and other modern
  events), WebViewBrush snapshots, and unsupported legacy members return
  E_NOINTERFACE/E_NOTIMPL. This is a compatibility subset, not a full replacement.
- HWND airspace places web content above overlapping XAML. Ancestor clip regions,
  transformed/rotated elements, popup layering, and XAML focus traversal need a
  composition-controller implementation for full fidelity.
- Remote URI allowlists, ms-local-stream and relative URLs inside HTML supplied
  to NavigateToString are not implemented.

Do not restore the EdgeHTML security/package hooks: EdgeHTML initialization
requires a CoreWindow and deliberately terminates the Islands process.
