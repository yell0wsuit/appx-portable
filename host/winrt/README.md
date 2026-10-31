# Explicit managed WinRT bridge

`AppxPortable.WinRT.dll` is an experimental C# 5/.NET Framework library. It calls
the shim's WinRT ABI directly, bypassing Mono's missing built-in WinRT projection.
The native Windows CMake build compiles it and registers `managed_winrt_bridge`.
It is not copied into games by the installer or included in release packages.

The first surface is `AppxPortable.WinRT.Package.Current.Id`: package name,
publisher, resource ID, and architecture come from the native shim and the
executable's adjacent AppxManifest.xml. GUIDs and vtable slots follow
`IPackageStatics`, `IPackage`, and `IPackageId` in the Windows SDK's
windows.applicationmodel.h. All methods preserve native HRESULT failures.

Callers initialize their COM apartment. `Package` and `PackageId` own native
references and implement IDisposable. A child remains valid after its parent
is disposed. SafeHandle protects each vtable call from concurrent disposal and
releases abandoned objects through finalization. HSTRING results are copied by
their UTF-16 length before being deleted. This initial surface uses agile shim
objects; apartment-bound XAML objects will need a UI-thread lifetime policy.

The integration regression calls real shim factories, checks Unicode and empty
strings, enum width, native errors, independent references, repeated activation,
and disposal/GC behavior. It passes as x86 under the patched Wine 11.18/Mono
11.3.0 runtime. The Windows test registration and x64 control require separate
validation.

## Generated Windows projection

`generator.cs` is a build-time tool. It reads SDK contract `.winmd` files with
Mono.Cecil and emits C# for the members selected by `bootstrap.xml`:
`<Property>` getters/setters and `<Method>` calls with pointer-valued results
or void and string, object, TypeName, runtime class, interface, or delegate
arguments, `<Event>` as consumers bind it (a
token-returning `add_X` and a token-taking `remove_X`), `<Constructor>` for
composable classes or metadata-marked parameterless activation, `<Overridable>` virtuals, `<Enum>`, `<Delegate>`, and
`<Interface>` with property getters. A selected class implements the selected
interfaces WinMD lists for it, reaching their members by querying the
interface per call; an interface wrapper first asks the native object's
runtime class name and returns the selected class wrapper when it has one. A
class with no members is a typed wrapper whose referenced members come from
completion stubs. A class may derive from another selected class: the root
holds the native object, and only the most derived constructor composes.
Static runtime classes use their metadata-selected static interfaces without
requiring a default instance interface.
Unsealed classes query the member's interface per call and wrap native
objects as the most derived selected class by runtime class name. Class and
interface arguments are queried for the parameter's interface. Property
setters take a runtime class or interface. IIDs, default, static, factory, and
overridable interfaces, vtable slots (six IInspectable slots plus WinMD
method order; required interfaces add no slots), return types, and enum
values all come from metadata. Unsupported surfaces, member kinds, and ABI
shapes fail generation by name. The output is a `Windows,
Version=255.255.255.255` assembly. Its classes use the internal runtime, so
`abi.cs` and `ccw.cs` are compiled into the same assembly. Neither the
metadata nor Cecil ships with games, and the generated assembly is not yet
installed into them.

`ccw.cs` gives managed objects a native identity. A managed delegate passed
to native code is an IUnknown-based callback; native references keep it
alive, and with none it is collectable. A composable constructor passes a
managed outer to the factory's `CreateInstance` and keeps the non-delegating
inner; the outer answers its overrides interfaces and delegates other
interfaces to the inner. Calls on a composed object query the inner per call,
so the managed object holds no reference that keeps itself alive. Wrapping a
native object returns the managed outer behind its IUnknown, or one cached
wrapper per native identity. Each native method invocation owns an exception scope. Passed delegates carry
that scope across callback threads, including reentrant dispatch; composed
objects fall back to their construction scope when no caller scope is active.
A managed callback exception is rethrown, with its stack, only by its originating
active invocation. Completed scopes discard failures. Delegate-only objects
reject IInspectable; wrapper-cache publication releases a losing native reference
when two threads wrap the same identity concurrently. Overridable virtuals outside the surface keep the native behavior;
a managed subclass that overrides one fails construction, naming it, rather
than never being called.

`dispatcher.cs` gives the shim's view thread a synchronization context when
native code first calls managed code there: a thread with a CoreWindow posts
continuations through `CoreDispatcher.RunAsync`, so `await` returns to the
view thread as XAML code expects. `Send` is unsupported, as in the CLR's WinRT
context. An exception escaping a posted continuation is rethrown off the
dispatcher and reaches `AppDomain.UnhandledException`, ending the app as on
Windows.

Under Wine, managed XAML games use the shim's own XAML instead of XAML Islands,
which needs system XAML; `APPX_PORTABLE_XAML=shim` or `=islands` overrides
either default. Windows behavior is unchanged without the variable.

`scripts/probe-winrt-projection.py` checks this with an explicit x86 Wine
engine, prefix, metadata directory, Cecil, and shim, using bundled Mono `mcs`
with C# 5. It runs the bridge test above; then a consumer whose `Windows`
reference carries the WindowsRuntime flag must fail against a throwing
reference assembly and pass against the generated one. Generated IIDs and slots
are compared with values taken from the SDK's ABI headers
(`windows.applicationmodel.h`, `windows.applicationmodel.activation.h`,
`windows.ui.xaml.h`), and the real shim validates them through native calls.
Unknown or duplicate surface entries must fail, and metadata with a changed IID and
method order must change the output and be rejected by the shim.
`tests/winrt_projection/application.cs` starts a managed `Application`
subclass the way games do. Its `OnLaunched` must run on the shim's view
thread with `Application.Current` returning the same managed object, then
`Exit` ends the process as a Store app's does. When `OnLaunched` throws,
`Start` must rethrow that exception and the callback must then be
collectable. Its constructor subscribes to `Suspending` through
`WindowsRuntimeMarshal`, as compiled games do; `Exit` must raise it on the
managed App, and not on a handler already removed. `OnLaunched` sets a new
`Frame` as the content of `Window.Current`, which must then return that same
managed `Frame`. It then awaits work finished on the thread pool, and the
continuation must resume on the view thread. Both consumers also run against the completed assembly, so
completion cannot replace implemented members with stubs. Configure the
x86 cross build with `APPX_WINE`, `APPX_WINE_PREFIX`, `APPX_WINRT_METADATA`,
and `APPX_WINRT_CECIL` to register it as the `winrt_projection` CTest. The
tested metadata is `ref/netstandard2.0` from the Microsoft.Windows.SDK.Contracts
10.0.19041.1 NuGet package; Cecil is Wine Mono's own copy:

```sh
wine="$PWD/build/wine-clr/runtime/Wine Devel.app/Contents/Resources/wine"
cmake -B build/macos-x86 -DAPPX_WINE="$wine/bin/wine" \
  -DAPPX_WINE_PREFIX="$PWD/build/wine-clr/test-prefix" \
  -DAPPX_WINRT_METADATA=/path/to/Microsoft.Windows.SDK.Contracts/ref/netstandard2.0 \
  -DAPPX_WINRT_CECIL="$wine/share/wine/mono/wine-mono-11.3.0/lib/mono/gac/Mono.Cecil/0.11.1.0__0738eb9f132ed756/Mono.Cecil.dll"
ctest --test-dir build/macos-x86 -R '^winrt_projection$' --output-on-failure
```

## Completing the projection for a game

`complete.cs` is a second build-time Cecil pass over the compiled projection.
Given the game's managed assemblies, it defines every `Windows` type they
reference, with its selected WinMD shape: base chain, sealing, enum values, struct
fields, and delegate signatures. This pass does not yet copy every implemented
interface or virtual method shape; reference resolution measures binding coverage,
not complete CLR interface dispatch or implemented behavior. It then adds every referenced member with
the game's exact signature. Members the projection already implements are
kept. WinRT `put_X` calls forward to an implemented C# `set_X` only when
WinMD identifies the setter and the exact projected signature matches;
ordinary methods are not renamed. Every other body throws `NotImplementedException` naming the member,
for example `Windows.UI.Xaml.Window.get_Current`. A WinMD shape that would
need a CLR-redirected type such as `Uri` or `Thickness` fails by name.

`tests/winrt_projection/resolve_references.cs` checks the result by resolving
each `Windows` type and member reference against only the projection
directory. It also compares static/instance and generic arity, which Cecil's
own resolution ignores. The probe runs it before completion (it must fail)
and after (it must find no unresolved references). Pass `--consumer` once per
game assembly to include a real game. For Bejeweled LIVE, `Bejeweled3.exe` and
`DT.GoogleAnalytics.Metro.dll` have 240 type and 405 member references, none
unresolved after completion. In a copied game, `Program.Main` starts the
shim's Application; its callback constructs `Bejeweled3.App`, whose
constructor subscribes to `Suspending`, and the shim then calls the game's
`App.OnLaunched` on its view thread. `OnLaunched` sets a composed `Frame` as
`Window.Current.Content`. The local dependency-property implementation lets
`SuspensionManager` initialize its string and dictionary attached properties.
With the Xbox, settings, task, stream and connectivity projections below, the
game constructs its Xbox wrapper and returns to `App.OnApplicationStarted`.
It retrieves the native launch splash and composes the game's `ExtendedSplash`
Page. Its LoadComponent call now builds `ExtendedSplash.xaml`, including its
resources and Viewbox children, and decodes both package wordmark images. It
then completes `ExtendedSplash.InitializeComponent`, including typed Grid
namescope lookups. Its constructor next fails explicitly when subscribing to
`Window.SizeChanged` (`Windows.UI.Xaml.Window.add_SizeChanged`). Window event
projection is the next construction gap.
No menu or game round has been displayed by this path.

The game references `System.Runtime.WindowsRuntime`, now supplied by the probe,
and `System.Runtime.WindowsRuntime.UI.Xaml`, still missing from Wine Mono, plus
the `Microsoft.Xbox` and `Bej3Win8Component` WinRT components. The initial Xbox
surface, task adapters, and the read-only storage/stream path are projected;
other component assemblies and additional native async shapes remain pending.

This is a foundation for managed compatibility, not a replacement for the game's
`Windows` assembly. Layout, source loading, rendering integration,
CLR-redirected types such as `System.Uri`, and general managed implementations of WinRT interfaces
remain to be implemented before Bejeweled can use this path. The existing game
host is unchanged; its older "XAML Islands (STA)" log label identifies the STA
route even when the shim selects its Wine XAML backend. The ini override for
hosting mode remains to be added; this slice only supports the environment
override above.

Native PropertySet activation and settings persistence now pass on the patched
Wine 11.18 runtime. The managed settings projection is described below.

The probe's `--game` option copies an installed game into a fresh output
subdirectory and launches its unchanged `appx-host.exe` with the completed
projection and current shim, with `--trace` to record named native gaps in
`appx-portable.log`. Supply `--consumer` for each managed game assembly
and `--expected-game-error` for the expected named gap. Source and output
directories must be disjoint. Links are copied as links and the game's own
`.wine-prefix` is skipped: its `z:` drive links to `/`, and the launch uses
`--prefix`. Replacement DLL links in the copied game are detached before
writing, so their original targets remain untouched. Source executable,
manifest, consumer, and replaced runtime DLL hashes
are checked after launch and recorded in `game-inputs.json`. For this checkpoint, build the test callback DLL beside the shim as well:

```sh
cmake --build build/macos-x86 --target appxportable network_projection_fixture page_projection_fixture
python3 scripts/probe-winrt-projection.py \
  --wine "$wine/bin/wine" --prefix /path/to/isolated-prefix \
  --metadata /path/to/contracts/ref/netstandard2.0 \
  --shim build/macos-x86/appxportable.dll --output build/game-projection-probe \
  --consumer /path/to/game/Bejeweled3.exe \
  --consumer /path/to/game/DT.GoogleAnalytics.Metro.dll \
  --game /path/to/game --xbox-metadata /path/to/game/Microsoft.Xbox.winmd \
  --expected-game-error 'Windows.UI.Xaml.Window.add_SizeChanged'
```

The focused runtime regressions cover delegate ABI, concurrent wrapper identity,
concurrent and nested exception scopes, stale failures, and reentrant callbacks.
The x86 cross build, registered Wine CTest, full game probe, and 154 Python tests
passed locally. Windows and x64 runtime controls remain unverified for this slice.

## Local dependency properties

`shim/xaml/properties.cpp` serves real `DependencyProperty` and
`PropertyMetadata` factories through `ShimGetFactory`. Each shim element owns
its local values; absent and explicit-null entries differ, `ReadLocalValue`
returns the stable UnsetValue sentinel for absence, and `ClearValue` restores
the registered default. Change callbacks receive retained old/new values after
the update, including reentrant changes, and failures propagate as HRESULTs.
Access to an element's store is restricted to its creating thread. The model
follows the documented [default and local value rules](https://learn.microsoft.com/en-us/windows/apps/develop/platform/xaml/dependency-properties-overview).

Native registrations support string, object, Boolean, Int32/UInt32, Int64/UInt64,
Single and Double, with scalar type validation and implicit zero defaults for
numeric/value types. Custom managed reference properties use a private
projection/shim interface to let the CLR check assignability; class-name
comparison cannot validate generic arguments, interfaces, or base classes.
Custom type names carry assembly identity. The projection maps native
[TypeName to System.Type](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.interop.typename),
boxes/unboxes string, Int32, Int64, Boolean, Double, and Guid via Wine's IPropertyValue, and preserves the
identity of other managed references through a real IInspectable CCW. The
CCW stays rooted until native AddRef establishes its lifetime.

`dependencytest` checks registration, duplicate rejection, default/local/null/
unset transitions, scalar equality, invalid types, callbacks, reentrancy,
callback failure, ownership, and wrong-thread rejection. The managed Application
fixture checks TypeName ABI, scalar and dictionary round trips, derived values,
type rejection, and temporary objects while another thread forces GC.
Its second variant uses WinRT `put_Content` metadata to verify completion
forwards to the implemented setter; the original C# variant still runs.

This is the local-value foundation of milestone 2b. Style/template/animation
precedence, inheritance, binding invalidation, built-in control property
identifiers, and general layout are still pending. Metadata factory callbacks
and foreign PropertyMetadata implementations fail explicitly; parameterized
PropertyMetadata constructors are not projected yet. Other managed scalar boxing remains explicit unsupported behavior. This checkpoint
does not establish Windows or x64 runtime parity.

## Xbox bootstrap and CoreWindow

`xbox.xml` selects native-backed `Microsoft.Xbox.User` default activation and
its Identity getter, plus XboxLIVEService.ServiceClient/SignedInUserIdentity
and ServiceClient.Token/SignedInUserIdentity. IIDs and slots come from the
package's Microsoft.Xbox.winmd, with independent checks against
`shim/xbox/xbox_abi.h`. The existing native stand-in supplies an unsigned-in
user, no signed-in identity, an empty token, and a shared client. The Microsoft
implementation DLL is neither inspected nor loaded for this projection.

The generator supports static runtime classes and parameterless activation
when WinMD explicitly advertises it. Completion scopes definitions to the input
projection assembly, retaining external Windows and System scopes. Xbox members
outside the selected surface have named NotImplementedException bodies;
async service calls, profile retrieval, achievements and title storage are
not yet forwarded to their native offline operations. Referenced type/member
binding is not equivalent to implementing those APIs.

`xbox_metadata.cs` verifies IUser's IID and full ordered ABI signatures against
the supported native layouts, plus the selected service interfaces' IIDs, slots
and signatures. It derives the ABI layout version from that metadata snapshot
and emits AssemblyFileVersion for the generated DLL. The native stand-in uses
this resource to choose its IUser10 or IUser12 vtable. AssemblyVersion remains
255.255.255.255; the file version describes the ABI, not an inferred Microsoft
SDK product build. Unknown layouts are rejected. The tested package matches
ABI 1.2; real SDK 1.0/1.1 and Windows/x64 projection controls remain unverified.

The optional `--xbox-metadata` probe builds and completes Microsoft.Xbox.dll,
checks native activation, signed-out state, wrapper identity, the version
resource after completion, named unimplemented behavior, and reference
resolution. Mutated IIDs, method order, argument widths, and service slots must
fail qualification. `--xbox-only` runs this focused fixture. Set
`APPX_XBOX_METADATA=/path/to/game/Microsoft.Xbox.winmd` in the existing CMake
configuration to register `winrt_xbox_projection`; it uses only the metadata,
not the Microsoft implementation DLL. SDK metadata and generated artifacts
remain outside release packages and normal installer behavior.

The Windows projection also exposes CoreWindow.GetForCurrentThread and its
Dispatcher through the existing native factories. The managed Application
fixture verifies a current view-thread window, a dispatcher, and stable wrapper
identity; the existing dispatcher continuation checks still run. General
CoreWindow events and further storage APIs remain named completion stubs.

## Persistent application settings

`bootstrap.xml` selects ApplicationData.Current, LocalSettings/RoamingSettings,
and container Name/Locality/Values/CreateContainer/DeleteContainer. Values use
an IPropertySet adapter implementing `IDictionary<string, object>` over the
native IMap and its iterable interfaces. Each operation reaches the existing
native settings store; the projection keeps no shadow dictionary. Keys and
Values expose read-only live views, and enumeration owns and releases its
native iterator and pairs. Compound Add and pair-removal operations require
caller synchronization when other actors mutate the map.

The generator derives closed generic IIDs and slots from WinMD, validates
consumed signatures and method order, and checks generated values against SDK
header constants. Negative metadata fixtures reject changed Insert/iterator
signatures and reordered map slots. The focused `--storage-only` probe and
`winrt_storage_projection` CTest verify dictionary operations, Unicode keys,
scalar round trips, local/roaming separation, child containers, and durable
reads in a second process. Unsupported settings values fail without insertion.

The managed bridge supports string, Int32, Int64, Boolean, Double, and Guid
settings values. UInt64, arrays/composites, MapChanged events, the Containers
read-only map and additional storage/async surfaces are pending. The read-only
file path and its native async wrappers are described below.
This checkpoint does not establish Windows/x64 runtime or rendered gameplay
acceptance.

## Managed WinRT task adapters

The generator's `Adapter="Async"` surface emits IAsyncInfo, the four IAsync
contracts, completion/progress delegates and AsyncStatus from SDK WinMD. It
projects HResult to System.Exception. These are CLR-facing declarations; native
closed generic wrappers remain separate work.

`task_extensions.cs` builds an owned `System.Runtime.WindowsRuntime.dll` with
assembly version 4.0.0.0. Its GetAwaiter/AsTask overloads consume these contracts
using TaskCompletionSource, preserving results and errors, requesting native
cancellation through Cancel, and forwarding progress to IProgress. Cancellation
requests await terminal status; the adapter never closes a caller's operation.
The signatures follow the public
[WindowsRuntimeSystemExtensions API](https://learn.microsoft.com/en-us/dotnet/api/system.windowsruntimesystemextensions.astask?view=dotnet-uwp-10.0).

Completion releases the adapter's source and result delegate references. This
prevents a native completion CCW from retaining its own native source through
the adapter. Cancellation-registration disposal runs off the completion callback
to avoid deadlock when Cancel waits for completion on another thread. Task
continuations run asynchronously and retain standard TaskAwaiter context capture.

`--async-only` and `winrt_async_projection` verify completed, delayed, reentrant
and duplicate callbacks; result/error identity; cancellation races; all four
async shapes; progress; caller ownership; and collection after completion with
the completion delegate still rooted. The fixture's Windows reference carries
the WindowsRuntime flag and its System.Runtime.WindowsRuntime reference uses
version 4.0.0.0/token b77a5c561934e089. It first fails without the bridge, then
runs with the owned replacement. This establishes Wine Mono binding; Windows
strong-name binding remains unverified. The bridge is only copied into isolated
probe games. The selected native async and stream extensions are described below;
other async result shapes and writable streams remain pending.

## Native file reads and stream adapters

`storage_generation.cs` qualifies the IIDs, consumed method signatures, and slots
for `storage_abi.cs`, including closed `IAsyncOperation<StorageFile>`,
`IAsyncOperation<IRandomAccessStreamWithContentType>` and
`IAsyncOperationWithProgress<IBuffer, uint>`. Generated values are independently
checked against SDK ABI headers. Negative metadata fixtures reject changed
64-bit sizes/read counts and reordered buffer/async slots.

ApplicationData.LocalFolder, StorageFolder.Path/Name/GetFileAsync, and
StorageFile.GetFileFromPathAsync/Path/Name/OpenReadAsync reach the native shim.
The native folder fallback now looks up relative file paths, including subfolders,
and returns the existing native async error for missing files. This follows the
[documented GetFileAsync path contract](https://learn.microsoft.com/en-us/uwp/api/windows.storage.storagefolder.getfileasync).
Completion and progress callbacks are real CCWs; they reconstruct operation
wrappers from the callback argument and avoid retaining their own source.

The shim fills Wine's missing Buffer factory with owned byte storage, capacity/
length validation and IBufferByteAccess. The managed AsBuffer(byte[]) adapter
instead shares the original array through an IBuffer CCW and a separately owned
pin, as required by the
[buffer extension API](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.windowsruntime.windowsruntimebufferextensions).
Native references keep the bytes alive after the projected wrapper is collected.
Object-valued returns preserve or recreate the public buffer wrapper; they do
not expose its backing state. ToArray copies bytes. Compatible delegate rebinding
handles Mono returning a CCW's original delegate type for a function pointer.

AsStream(IRandomAccessStream) and AsStreamForRead(IInputStream) expose actual
native reads, EOF, seeking and position through System.IO.Stream. ReadAsync
serializes position changes and avoids capturing the caller's synchronization
context. The seekable view is limited to read-only streams; writable streams,
resizing, output streams and additional extension overloads fail explicitly or
remain unprojected. Disposal closes the underlying WinRT input stream.

`--io-only` and `winrt_storage_io_projection` cover real file reads, 64-bit
positions on x86, UTF-8/NUL StreamReader content, missing-file errors, relative
subfolder lookup, array mutation in both directions, object identity and pin
lifetime after collection, invalid buffer lengths and oversized allocations.
The consumer carries the game's WindowsRuntime reference flag and runtime
assembly version/token. All fixtures use owned output/save directories. This
checkpoint does not establish Windows/x64 runtime or rendered gameplay parity.

## Native connectivity projection

`bootstrap.xml` selects NetworkInformation.GetInternetConnectionProfile and its
NetworkStatusChanged event, ConnectionProfile.ProfileName and
GetNetworkConnectivityLevel, plus the metadata enum and delegate. Static events
use the existing token/CCW implementation; generated methods now support selected
32-bit enum returns with HRESULT checks and the correct signed/unsigned ABI.
A changed 64-bit connectivity enum fails generation by name. IIDs and slots are
independently checked against windows.networking.connectivity.h.

This projects the existing native connectivity contract. By default there is no
internet profile. The explicit stub-IAP option supplies the existing synthetic
profile named `Offline stub IAP` reporting InternetAccess, so connectivity
preflight can reach the local Store stub. HTTP remains offline. Options are read
once and the model is constant for the process; registration retains handlers
and removal releases them, but it does not monitor physical network changes.

`--network-only` and `winrt_network_projection` check both modes in separate
processes, native subscriber lifetime/removal and WindowsRuntimeMarshal event
registration. `network_projection_fixture.dll` is a test-only SDK ABI caller: it
queries the generated callback IID and invokes its native vtable with null and
managed-object senders, including an exception that must return through the
invocation scope. It is copied only into that fixture and is not release content.
By default the probe finds it beside the shim; `--network-fixture` selects an
explicit path. Windows/x64 controls and gameplay acceptance remain pending.

## Launch splash and typed events

The IActivatedEventArgs.SplashScreen projection returns the shim's existing
shared splash object. SplashScreen.Dismissed reaches the native event source,
which queues dismissal behind activation and fires once. The generator supports
`TypedEventHandler<TSender, TArgs>` through `Adapter="TypedEvent"`, deriving the
closed delegate IID from WinMD and the selected argument signatures. The native
callback uses two pointer arguments and preserves existing wrapper identity.
Supported native arguments are selected runtime classes/interfaces and object;
unsupported shapes fail generation by name.

`--splash-only` and `winrt_splash_projection` launch a real composed Application
with a WindowsRuntime-marked consumer. They verify splash identity, dismissal
after OnLaunched returns, sender/argument identity, view-thread execution and
continuation, one dismissal across repeated activation, and removal of the
unwanted handler. The closed IID is independently checked against the SDK; a
mutated typed-delegate signature must be rejected without producing output.

The copied game now passes ExtendedSplash's Page base constructor. XAML component
loading, splash ImageLocation/geometry projection,
and a rendered game page remain pending. The fixture proves native splash
object/event behavior; it does not establish a displayed game splash. Windows
and x64 controls remain unverified.

## Managed Page composition

The selected Page constructor now composes over the existing native Page factory.
UserControl supplies the selected base type and Content property; Page.Frame
reads the native page's host frame. The most-derived projected constructor
creates the native object once, sharing its root's native/outer fields through
the marker constructor chain. Derived composable classes can expose their own
selected override interfaces.

OnNavigatedTo, OnNavigatedFrom and OnNavigatingFrom dispatch through the managed
controlling outer. Their managed base implementations call the nondelegating
inner's native interface. This preserves the native base behavior and avoids
recursive override dispatch. NavigationEventArgs and NavigatingCancelEventArgs
are typed wrappers; NavigationEventArgs.Uri is selected, while their other
properties remain unselected.

`--page-only` and `winrt_page_projection` check a WindowsRuntime-marked consumer
with two levels of managed Page inheritance, real inherited Content and local
dependency properties, and Page identity through Window.Content. A second
fixture compiles the same consumer with the production runtime and uses the
test-only `page_projection_fixture.dll` to call the SDK IPageOverrides ABI via an
ordinary UI interface. It checks all three overrides, shared navigation-argument
identity, inherited base methods, a partial override, and callback exception
identity. The DLL is not copied into probe games or release content. Its default
location is beside the shim; `--page-fixture` selects another path.

The fixture does not perform actual Frame navigation or test populated
cancellation arguments. Broader XAML source loading, layout, rendering,
Windows/x64 controls and gameplay acceptance remain pending.

## Managed component ABI prerequisites

Composed managed subclasses that implement
`Windows.UI.Xaml.Markup.IComponentConnector` now expose that SDK interface on
their controlling outer. Native `Connect(int, object)` calls dispatch to the
managed implementation, preserving the signed ID, target identity and callback
exceptions. Native targets use selected runtime-class wrappers; boxed property
values are unboxed, and unnamed native objects retain a generic wrapper when
GetRuntimeClassName returns E_NOTIMPL. Other native failures propagate.

Public arguments of a composed object use its controlling identity, including
IInspectable arguments. The private inner remains available for native base
methods, preventing recursive override calls.

The Uri adapter follows the documented
[System.Uri projection](https://learn.microsoft.com/en-us/uwp/api/windows.foundation.uri):
it creates a native IUriRuntimeClass through the platform factory and converts
native values through AbsoluteUri. Null is supported; relative URIs are rejected
before calling native code. URI targets are recognized by IID because the tested
Wine implementation returns E_NOTIMPL for GetRuntimeClassName. The SDK metadata
qualifies each signature, IID and slot; no Windows.Foundation.Uri CLR class is
emitted.

The Page probe checks a connector implemented by an external WindowsRuntime-marked
consumer, native-created typed and unnamed targets retained beyond the native
call and GC, boxed strings, callback exceptions and recovery, and URI round trips
with package paths, escapes, Unicode, query and fragment components. Mutated
connector ID, URI result and URI factory parameter metadata must be rejected.

These prerequisites are now used by the first source-XAML loader below. The
existing native Windows 8 loader remains separate from the managed shim path.

## Source-XAML loading

Both [Application.LoadComponent overloads](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.application.loadcomponent)
now call a real source loader in managed shim-XAML mode. The generator qualifies
the overloaded signatures from metadata and emits separate ABI delegates for
the two SDK slots. Native games keep the existing loader path; Windows Islands
continues to use system XAML.

The source loader supports Page, Frame, Grid, Image, ProgressRing, TextBlock and Viewbox elements,
implicit content and explicit Page.Content/Frame.Content, numeric Width/Height, Visibility,
ASCII names, a matching root x:Class, and x:ConnectionId callbacks. Design
attributes in a declared ignorable Blend namespace are skipped. Inline resource
dictionaries, Grid child trees and storyboard data are supported as described below.
Animation clocks, other brush types, other controls and other properties fail explicitly,
with their member names in appx-portable.log.

Package-local ms-appx:/// source paths are UTF-8 unescaped and resolved through
the existing package resource reader. Query/fragment components, remote schemes
and other package authorities are unsupported; only the Application resource
location is implemented. XML DTDs are prohibited and element depth is bounded.
The full document is validated and children are built detached before applying
the root's content and properties. Names are registered before connection
callbacks, and callback failures propagate to the managed caller.

Each loaded component owns a namescope shared by its descendants. The scope
retains named descendants and holds a WinRT weak reference to the root;
descendants borrow the scope. Managed inspectable identities expose the SDK
IWeakReferenceSource/IWeakReference ABI so resolution acquires a managed root
before publishing a counted COM reference. This avoids a root/scope reference
cycle and permits a retained child to outlive its root safely. Source loading
and element FindName check the owning thread. Namescope lookups do not fabricate
legacy panel names after a component has been loaded.

The source fixture runs as a WindowsRuntime-marked external consumer through
`--page-only` and `winrt_page_projection`. An SDK-typed native inspector checks
the actual content tree, dimensions, visibility and descendant namescope.
Regressions cover escaped Unicode paths, root/child identity, connection timing,
malformed and unsupported source, invalid values, replacement namescopes,
wrong-thread calls, callback exceptions, and retained-child lookups during root
collection. Rendering, layout and the game's splash/menu remain separate gates.

## Inline resource dictionaries

FrameworkElement.Resources now returns a real native ResourceDictionary, created
lazily per element. The managed constructor, property getter/setter and inherited
local dependency values use the native ABI. Native games retain their existing
hosting route, and system XAML remains in charge in Islands mode.

The native IResourceDictionary also implements IMap<object,object> and its
iterable contract. This slice accepts boxed string keys using ordinal string
comparison. Insert reports replacement, missing keys return E_BOUNDS, null values
remain distinguishable from missing entries, and Remove/Clear release owned
references. Read-only views and enumeration use snapshots; pairs and iterators
retain their own keys and values. Views support Split and iterators support
GetMany. Dictionary and snapshot operations enforce their creating thread.
Non-string keys, non-null Source, merged dictionaries and theme dictionaries
fail explicitly. Managed dictionary indexer/enumeration projection is still
unselected; native map behavior is verified through the SDK ABI.

The source loader accepts `<Page.Resources>`, `<Frame.Resources>` and
`<Grid.Resources>` with implicit entries or a sole explicit ResourceDictionary.
Resource values use the supported visual, animation and brush source types.
Literal x:Key selects a key; x:Name supplies a fallback key under the documented
[XAML naming convention](https://learn.microsoft.com/en-us/windows/apps/develop/platform/xaml/x-name-attribute).
Keys do not register names for FindName. Actual names enter the component's
namescope and can participate in connection callbacks. Keys are local to each
dictionary, so nested dictionaries can reuse a key. Duplicate keys and missing
keys are rejected during preparation. Source dictionaries are built detached
before replacement, and retained old dictionaries remain usable.

The SDK-typed resource fixture checks identity, Unicode string keys, null values,
replacement/removal, snapshots retaining a native value after its original
references are released, split views, iterator boundaries/GetMany, wrong-thread
calls and unsupported keys. Source regressions cover implicit, explicit, empty
and nested dictionaries; names versus keys; replacement lifetime; callbacks; and
unsupported entries preserving the existing tree/resources. Animation execution
remains a separate implementation checkpoint.


## Storyboard and timeline data

The native engine activates Storyboard, FadeInThemeAnimation and
FadeOutThemeAnimation through their SDK interfaces. Timeline properties store
validated duration, begin time, speed, repeat, fill and reverse values. Source
XAML accepts implicit Storyboard children or a sole Storyboard.Children container,
Duration values with exact 100 ns precision, and literal animation target names
and properties. Theme TargetName and Storyboard.TargetName remain distinct.
Named timelines participate in source namescopes and component callbacks;
FindName returns the selected managed animation type even without a prior callback.
Retaining a timeline does not retain the component root.

Storyboard.Children implements the SDK vector, iterable, snapshot view and
iterator contracts. Children retain their timeline objects; collections remain
usable after their original Storyboard is released. Invalid replacements preserve
old contents, cycles are rejected, and operations enforce the creating thread.
Completed subscriptions retain and release handlers, but no completion is fired.

Clock operations, including Begin, return named E_NOTIMPL errors. No animation
runs in this checkpoint. Explicit object targets, animation dependency-property
identifiers and target markup extensions also fail explicitly. Managed animation
constructors and collection/property projection remain unselected; SDK callers
verify the native data contract. The source fixture covers typed names, exact
and overflowing durations, thread checks, lifetime, collection boundaries,
oversized ReplaceAll counts and rejected Page content preserving root properties.
Frame retains its object-typed content contract.


## Source-loaded Grid children

A Grid can contain ordered visual children directly or in a sole Grid.Children
container. Children currently use the supported Page/Frame/Grid source types;
nonvisual timelines are rejected. Duplicate or mixed child containers, duplicate
names and unsupported property elements fail during preparation before Page
properties or content change. Property-element failures retain the actual member
name, including Grid.Background.

IPanel.Children returns a stable native UIElementCollection with SDK vector read,
view and iterable interfaces. Source loading installs a prepared immutable child
snapshot. Views and iterators retain their snapshot, and a retained collection
owns its children independently of the Grid. Reads enforce the creating thread.
Programmatic vector mutations and UIElementCollection.Move return named E_NOTIMPL
errors until visual parenting and reparenting are implemented. Managed collection
projection remains unselected.

Grid descendants share the component namescope and participate in connection
callbacks in source order. Names are available before callbacks run, and retained
children do not keep the component Page alive. The SDK fixture checks child order,
nested/empty children, identity, iteration/GetMany, bounds, thread rejection,
unsupported mutations and lifetime across Page replacement. This checkpoint adds
tree data; layout, drawing and Loaded traversal through panels remain separate
work. Background brush data is described below.


## Linear gradient background data

The source loader accepts Grid.Background containing LinearGradientBrush or
SolidColorBrush. LinearGradientBrush supports StartPoint, EndPoint and Opacity
literals. Stops can appear directly or in a sole
LinearGradientBrush.GradientStops container. GradientStop supports finite Offset
values and named or #RGB, #ARGB, #RRGGBB and #AARRGGBB colors. Points use two finite,
float-representable coordinates separated by a comma. scRGB literals and other
brush types remain unsupported. Resources may contain these brush data objects;
actual names participate in FindName and component callbacks.

The native activation factories expose ILinearGradientBrush, IGradientBrush,
IBrush and IGradientStop data plus inherited local dependency values. Defaults
are start (0,0), end (1,1), opacity 1, Pad spread, relative mapping, sRGB
interpolation, and a zero-color/zero-offset gradient stop. The
[public gradient documentation](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.media.lineargradientbrush)
and the user-supplied local Windows.UI.Xaml.dll.c constructor exports informed
the defaults. Static reference inspection does not establish Windows runtime parity.

Grid owns its assigned background. Stop collections, views and iterators retain
stop objects independently of the brush, and all data operations enforce their
creating thread. Source-created stop collections expose the SDK read, view and
iteration contracts; public collection mutations fail by name. Brush property
round trips and replacing a GradientStops vector retain the supplied references.
Non-null transforms, alternate constructors, dependency-property identifiers,
managed brush construction and additional source properties remain unsupported.

Preparation rejects malformed literals, duplicate backgrounds, mixed stop
containers, markup expressions and nonvisual Page/Grid children before changing
the root tree. The SDK fixture verifies exact color bytes, default and explicit
values, names, resources, ownership, bounds, property setters and thread errors.
These are data contracts; gradient drawing and visual acceptance remain pending.


## Package image data

Source Image elements support absolute package-local ms-appx:/// PNG sources,
Stretch, dimensions, alignment and margin data. Image names and callbacks use a
selected managed Image wrapper; managed construction and Source/Stretch property
projection remain unselected. FrameworkElement alignment and finite margins use
native storage, including the one-, two- and four-value XAML margin forms. These
properties prepare layout inputs; measure/arrange and drawing remain pending.

The package reader resolves loose files and existing PRI candidates. URI handling
shares the source loader's authority, query, fragment and escaped-path checks.
WIC detects the container and frame count, accepts a single decoded PNG frame,
converts it to 32-bit premultiplied BGRA and copies pixels into an owned bitmap.
Decoded pixel dimensions stay separate from the Image's declared dimensions.
Failed loads leave the existing component tree and root properties intact.

The native Image exposes source ownership and
[Stretch data](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.controls.image.stretch),
with Uniform as its default. Its package source is an internal
AppxPortable.Xaml.PackageBitmapSource implementing IImageSource and the SDK
IBitmapSource dimension getters. It is not a substitute BitmapImage class.
The internal IImagePixels bridge provides counted WIC bitmap access for a future
renderer. Objects enforce their creating thread. Image events, NineGrid, casting,
stream replacement and general BitmapImage construction remain explicit gaps.

The SDK fixture decodes an independent two-pixel RGBA PNG and checks exact BGRA
premultiplication, dimensions, source replacement, all margin forms, alignment,
thread rejection and explicit event failure. It retains only the decoded source,
clears Image references, deletes the PNG and replaces the tree before reading
pixels again. Regressions cover missing/corrupt files, unsupported URIs and markup,
malformed layout values and content on leaf Image elements. This proves pixel
preparation and ownership, not rendering or the game's complete splash.


## ProgressRing state and solid foreground data

ProgressRing activation exposes its SDK
[IsActive property](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.controls.progressring.isactive)
and inherited Control
and FrameworkElement interfaces. Source IsActive literals and a selected managed
property call the same native state; the default is false. Explicit Foreground
values retain their brush, including explicit null from native callers. An unset
foreground fails by name because theme/style inheritance is unimplemented.
TemplateSettings also fails explicitly. Drawing and spinning remain open gates.

Source Foreground accepts a color literal or a sole ProgressRing.Foreground brush
element. SolidColorBrush supports default and color-specific native activation,
Color and inherited opacity/local dependency data. Its omitted Color preserves
its Transparent default. Both solid and linear brushes share opacity and
unsupported-transform handling. Source resources, Grid backgrounds and gradient
stops can use the same color parser, which recognizes all 141 names in the public
Windows.UI.IColorsStatics contract. RGB values reference public D2D SDK constants;
[Transparent](https://learn.microsoft.com/en-us/uwp/api/windows.ui.colors.transparent)
is #00FFFFFF. Names accept ASCII case differences and surrounding
whitespace; existing hex forms remain supported. scRGB, resource/theme markup,
brush constructors in managed code and additional source properties remain gaps.

Selected Boolean properties now use shared native helpers with explicit one-byte
WinRT boolean delegates, rather than Win32 BOOL marshalling or object arguments.
Metadata supplies accessor slots and IIDs; SDK constants independently check the
ProgressRing getter/setter. Property errors and wrong-thread failures propagate.

The fixture verifies typed callbacks, managed/native true/false round trips,
foreground brush identity and ownership, named/default/hex colors, native solid
brush constructors and property setters, invalid source preserving root state,
and explicit theme/template failures. These checks establish control data and
projection behavior; they do not demonstrate a rendered progress indicator.


## TextBlock and font descriptor data

Source TextBlock accepts Text, explicit FontSize/FontFamily/Foreground,
TextWrapping and TextAlignment plus existing visual properties. Text can be
updated through a selected managed Text property. Generic string-property setters
now hold a NativeString through the ABI call; Unicode, embedded NULs and null
values preserve the HSTRING contract. TextBlock construction and further managed
property projection remain unselected.

Native text, wrapping and alignment values use the SDK interface. Font sizes are
finite and nonnegative; explicit font family and foreground assignments retain
their objects, including native null values. Unset font/style values fail by name.
The native FontFamily name factory retains an immutable source descriptor with
its hashed family suffix. The
[SDK FontFamily constructor](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.media.fontfamily.-ctor)
defines this descriptor form. It follows the SDK's composable factory shape and is
readable across threads. String allocation completes before composition, and
cleanup keeps the ordinary interface alive only while its inner is retained.
FontFamily is a descriptor, not a loaded font or a DependencyObject.

The SDK-generated TextBlock defaults keep inlines, selection, baseline metrics
and other unsupported members explicit. Font resolution, package font loading,
text measurement, wrapping execution and drawing remain open work. Source body
text/Run content and markup expressions also remain unsupported; the implemented
source text form is the Text attribute. No font substitution or measurement is
reported as successful.

The fixture checks XML/Unicode text, managed embedded-NUL/null round trips,
thread rejection, explicit dimensions/style, font descriptor retention after
TextBlock replacement, cross-thread descriptor reads, aggregated factory
ownership, invalid arguments and Start/End alignment aliases. SDK constants
check ITextBlock and its Text accessor slots independently of metadata.

## Source-loaded Viewbox data

Viewbox accepts one implicit visual child or a sole Viewbox.Child property
element, including namespace declarations on that wrapper. Empty Viewboxes are
valid; multiple children, empty/duplicate child property elements, nonvisual
values and unsupported wrapper attributes fail before root mutation. Children
participate in the component namescope and connection traversal.

The native SDK interface returns counted child references and stores validated
Stretch/StretchDirection values. Defaults are
[Uniform](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.controls.viewbox.stretch)
and [Both](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.controls.viewbox.stretchdirection).
Source construction uses an internal child installer. Public Child mutations
return named E_NOTIMPL errors until visual parenting/reparenting is implemented.
Reads and data setters enforce the creating thread. A selected managed wrapper
preserves Viewbox identity; its constructor and properties remain unselected.

The SDK fixture checks implicit/explicit/empty children, names and callbacks,
default and explicit stretch data, invalid setter preservation, dimensions and
visibility, wrong-thread reads and child ownership after tree replacement.
These are tree and scaling-input contracts. Measure/arrange, scale calculation,
transforms and drawing remain separate work.

## Managed Grid and Panel identity

Grid and its Panel base are selected typed wrappers over the existing native
objects. Grid inherits Panel, FrameworkElement, UIElement and DependencyObject.
Content, FindName and component callbacks reuse the same native identity, so
source-loaded Grid results support typed casts and inherited implemented members.
The fixture checks nested namescope lookup, shared dependency values and retained
typed children after the component root is collected. SDK header constants verify
IGrid and IPanel independently of the generator's WinMD inputs.

Constructors, Panel.Children, Grid row/column definitions and attached layout
properties remain unselected. This adds managed type identity; it does not add
layout or rendering behavior.
