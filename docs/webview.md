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

Practical English Usage 1.0.6.9953's markup-created WebViews are supported through
the [installer patch](../patch/peu/README.md). It retargets only native WebView type
references in four XBF v1 files to `Windows.UI.Xaml.Controls.PortableWebView`.
The shim extends the application's metadata provider for that type, describes its
Grid base to XAML, and activates the existing COM stand-in. The parser therefore
sets inherited properties, registers names, and connects events normally. Its
`DefaultBackgroundColor` markup property maps to WebView2's controller color;
other metadata requests pass through to the application's original provider.

Each controller has a dedicated native host beside the XAML island. Its position
and z-order are updated together so XAML cannot paint over browser content or
intercept its input. Nesting the browser HWND under the island bridge made its
pixels visible but prevented native mouse hit testing and wheel scrolling.
Resizing preserves the sibling order. Legacy documents use a light browser color
scheme, matching their authored light backgrounds even when Windows is dark.
Transparent backgrounds use the nearest opaque panel/border brush, or the system
window color when none is present; gradients and images are not composited through
these native hosts.
The controller follows the element's layout, rasterization scale, tree
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
from a notification handler, and LoadCompleted.

`managed_webview_markup_contract` checks parsed namescopes, CLR casts, inherited
properties, and color conversion without loading the browser runtime. With
`APPX_TEST_WEBVIEW2=ON`, `managed_webview_markup` repeats the browser integration
test using a parser-created element. Python tests exercise the XBF rewrite,
input/output validation, idempotence, and restore using synthetic assets.
`managed_webview_scroll` verifies browser hit testing after host resizing and
checks displayed marker/background pixels before posting a wheel message
through the host pump and checking that the document actually scrolled. XAML's
input site receives real pointer events over browser siblings, so the host routes
wheel events and mouse buttons/motion to the browser under the pointer, retaining
the drag target until release. Real mouse-wheel and scrollbar-drag input were
also checked in PEU. This opt-in test needs an
interactive desktop and runs separately from other tests that open windows.

```powershell
cmake -S . -B build/ci-fix-x86 -DAPPX_TEST_WEBVIEW2=ON
cmake --build build/ci-fix-x86 --config RelWithDebInfo
ctest --test-dir build/ci-fix-x86 -C RelWithDebInfo -R webview --output-on-failure
```

OALD 2.6.21.9713's Help page was exercised in an isolated installation: the HTML
renders and sends its `loaded` notification. Its `BaseArticleComponent.WebViewBrush`
getter has no callers in the executable's IL; snapshot brushes are deferred.
PEU's MainPage, index/welcome HTML, and following an index link to an article were
also verified in a fresh isolated installation.
PEU's own HTML disables text selection; that restriction is preserved.

## Remaining work

- Other packages' markup-created WebViews need an XBF patch or equivalent markup
  rewrite. The PEU patch is restricted to its verified version and file hashes;
  XBF v2 and WebView-specific markup properties beyond background color are not
  implemented.
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
