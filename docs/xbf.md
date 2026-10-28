# Embedded compiled XAML

The native portable XAML backend can bind controls from a package's original XBF
2.0/2.1 resources. Rayman Fiesta Run Windows 10 Edition 1.0.23.0 can use this path
for its custom caption when no loose `CustomTitleBar.xaml` is present.

`OpenPackageResource` tries a loose file first, then asks Windows' native MRT
resource manager to resolve `Files/<relative path>` in the package's
`resources.pri`. The resolved candidate supplies a stream, with normal Windows
qualifier selection. Its bytes are copied while the MRT objects remain alive:
the native stream borrows the PRI mapping. Package files are never rewritten.

This uses native COM resource-manager interfaces, not the similarly named WinRT
interfaces. Their IIDs and method order were checked against Microsoft's matching
`MrmCoreR.pdb` public symbols. No instruction addresses or decompiled
implementation are used. These interfaces are undocumented. The embedded-caption
path has been tested locally on Windows 11 and successfully tested by the user
on a Windows 10 Pro 22H2 laptop (OS build 19045.7807) on 2026-10-28, with the
version and build confirmed by a Windows Specifications screenshot.
Interface activation or lookup failures are returned to the caller on
unsupported systems.

The installer therefore keeps Rayman's [loose-caption fallback](../patch/raymanwin10/README.md)
for version 1.0.23.0. `LoadComponent` loads that source XAML before attempting any
embedded-XBF read, so the default installation does not depend on these native
MRT interfaces. The fallback remains the default compatibility route; the
successful tests establish support for those tested configurations.

For the Windows 10 test, the user copied `local/rayman-win10-patchless`, which
omits `CustomTitleBar.xaml`, and reported that patchless operation works. The
provided screenshot (`Screenshot 2026-10-28 112637.png`) shows Rayman inside a
level with its pause menu open on the Windows 10 desktop.

The XBF reader validates section sizes, metadata tables, constants, scopes, names,
and connection IDs before returning a binding plan. `Application.LoadComponent`
then creates supported portable controls, registers their original names, applies
collapsed visibility, and invokes the game's component connector. Rayman's
original caption provides seven connection IDs, including the root UserControl.

This is a connection reader for the existing native backend, not a complete XAML
object writer. It supports Grid, Rectangle, Button and SymbolIcon connections
under a UserControl root. Decorative layout/property operations are consumed but
not rendered. The desktop title bar and F11/Alt+Enter supply the visible window
controls. Deferred streams, custom runtime data, unsupported connected types and
unrecognized instructions fail explicitly. Existing DirectX/media-page bindings
keep their current loading path.

PEU uses XBF v1 and real system XAML in Islands mode. A separate
[WebView memory adapter](webview.md) substitutes its type name while retaining
the original compiled node stream and leaving package files unchanged. System
XAML constructs that full visual tree; this native connection reader is not
used for PEU.

`xbftest` exercises valid bindings, all truncation points, duplicate IDs,
unsupported instructions, invalid metadata, and loose-resource lookup. When the
Windows SDK's `makepri.exe` is available, `xbf_pri` generates a synthetic embedded
XBF and reads it after the MRT mapping owner has been released. No game assets
are included in these fixtures. Rayman's original PRI was also checked manually,
and the game opened without the loose caption workaround.

Format references: Microsoft's [XBF reader](https://github.com/microsoft/microsoft-ui-xaml/tree/main/dxaml/xcp/tools/XbfParser/WidgetSpinner),
[stable XBF indices](https://github.com/microsoft/microsoft-ui-xaml/blob/main/dxaml/xcp/tools/XbfParser/WidgetSpinner/Metadata/StableXbfIndexMetadata.g.cs),
and [PRI-info indexer documentation](https://learn.microsoft.com/en-us/windows/uwp/app-resources/makepri-exe-format-specific-indexers#priinfo).
