# Practical English Usage 1.0.6.9953

The current runtime loads the original XBF files through its
[WebView memory adapter](../../docs/webview.md). `installappx.py` no longer applies
this asset patch. The tool remains available for older runtimes and for restoring
previously patched installations. Close PEU and run it with `--restore` when
upgrading such an installation to the current runtime.

The system WebView terminates an unpackaged XAML Islands process because EdgeHTML
requires a CoreWindow. The portable runtime supplies a WebView2 stand-in, but
XAML's built-in WebView activation bypasses the shim when loading compiled pages.

This patch changes the WebView type reference to `PortableWebView` in four XBF v1
assets. The runtime adds that custom type to the application's XAML metadata
provider and creates the stand-in before XAML sets properties or calls the app's
component connector. Existing names, connection IDs, binding data, node streams,
and other types are preserved. The app executable and resources.pri are unchanged.

Supported package: `OxfordUniversityPress.PracticalEnglishUsage` **1.0.6.9953 x86**.
The four input and output hashes are checked. Unknown assets or conflicting
backups abort before any file is changed. Originals are retained beside each XBF
with the `.islands-original` suffix; writes use temporary files and atomic rename.

The legacy patch requires a runtime supporting `PortableWebView` and an installed
Evergreen WebView2 runtime on Windows 10/11. These commands inspect, apply or undo
the old asset patch with the app closed:

```powershell
python patch/peu/patch.py "path/to/Practical English Usage"
python patch/peu/patch.py "path/to/Practical English Usage" --check
python patch/peu/patch.py "path/to/Practical English Usage" --restore
```

Verified through a fresh installer run: MainPage parses, both WebViews are created,
index/welcome HTML renders, and following an index link loads the article. The
HWND rendering limitations documented in
[`docs/webview.md`](../../docs/webview.md) still apply.
Wheel scrolling uses native browser input. PEU's own HTML deliberately disables
text selection; the patch preserves that behavior.
