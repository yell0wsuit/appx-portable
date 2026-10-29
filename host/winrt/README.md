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
With the Xbox bootstrap below, the game passes that assembly binding,
`CoreWindow.GetForCurrentThread`, and native Xbox User activation. With the settings projection below, it passes ApplicationData startup and next
fails loading `System.Runtime.WindowsRuntime` in `XboxWrapper.GetLastUserXuid`,
called by its constructor and `App.OnApplicationStarted`. No menu or game round has
been displayed by this path.

The game also references `System.Runtime.WindowsRuntime` and
`System.Runtime.WindowsRuntime.UI.Xaml`, which Wine Mono does not provide, plus
the `Microsoft.Xbox` and `Bej3Win8Component` WinRT components. The initial Xbox
surface is projected; the other component assemblies and general async adapters
remain pending.

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
projection and current shim. Supply `--consumer` for each managed game assembly
and `--expected-game-error` for the expected named gap. Source and output
directories must be disjoint. Links are copied as links and the game's own
`.wine-prefix` is skipped: its `z:` drive links to `/`, and the launch uses
`--prefix`. Replacement DLL links in the copied game are detached before
writing, so their original targets remain untouched. Source executable,
manifest, consumer, and replaced runtime DLL hashes
are checked after launch and recorded in `game-inputs.json`. For this checkpoint:

```sh
python3 scripts/probe-winrt-projection.py \
  --wine "$wine/bin/wine" --prefix /path/to/isolated-prefix \
  --metadata /path/to/contracts/ref/netstandard2.0 \
  --shim build/macos-x86/appxportable.dll --output build/game-projection-probe \
  --consumer /path/to/game/Bejeweled3.exe \
  --consumer /path/to/game/DT.GoogleAnalytics.Metro.dll \
  --game /path/to/game --xbox-metadata /path/to/game/Microsoft.Xbox.winmd \
  --expected-game-error "Could not load file or assembly 'System.Runtime.WindowsRuntime,"
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
read-only map, storage folders/files, and general async adapters are pending.
This checkpoint does not establish Windows/x64 runtime or rendered gameplay
acceptance.
