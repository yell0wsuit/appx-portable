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
`<Property>` getters, `<Method>` (void, with no parameter or one runtime
class, interface, or delegate), `<Event>` as consumers bind it (a
token-returning `add_X` and a token-taking `remove_X`), `<Constructor>` for
composable classes, `<Overridable>` virtuals, `<Enum>`, `<Delegate>`, and
`<Interface>` with property getters. A selected class implements the selected
interfaces WinMD lists for it, reaching their members by querying the
interface per call; an interface wrapper first asks the native object's
runtime class name and returns the selected class wrapper when it has one. A
class with no members is a typed wrapper whose referenced members come from
completion stubs. IIDs, default, static, factory, and
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
managed App, and not on a handler already removed. Both consumers also run against the completed assembly, so
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
kept; every other body throws `NotImplementedException` naming the member,
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
`App.OnLaunched` on its view thread. `OnLaunched` fails at its first
statement with `NotImplementedException: Windows.UI.Xaml.Window.get_Current`;
`Window` and `Frame` come next.

The game also references `System.Runtime.WindowsRuntime` and
`System.Runtime.WindowsRuntime.UI.Xaml`, which Wine Mono does not provide, plus
the `Microsoft.Xbox` and `Bej3Win8Component` WinRT components. None of these
is projected yet.

This is a foundation for managed compatibility, not a replacement for the
game's `Windows` assembly. `Window`, `Frame`, CLR-redirected types such as
`System.Uri`, managed implementations of WinRT interfaces, and dispatcher
synchronization remain to be implemented before Bejeweled can use this path. The existing game host is unchanged; its older "XAML Islands (STA)" log label
identifies the STA route even when the shim selects its Wine XAML backend.
The ini override for hosting mode remains to be added; this slice only supports
the environment override above.

ApplicationData was excluded from this first slice because Wine 9.0's native
PropertySet activation returned E_NOTIMPL, blocking the shim's settings backing
store independently of managed projection. This has not been rechecked on
Wine 11.18.

The probe's `--game` option copies an installed game into a fresh output
subdirectory and launches its unchanged `appx-host.exe` with the completed
projection and current shim. Supply `--consumer` for each managed game assembly
and `--expected-game-error` for the expected named gap. Source and output
directories must be disjoint. Links are copied as links and the game's own
`.wine-prefix` is skipped: its `z:` drive links to `/`, and the launch uses
`--prefix`. Source executable, manifest, and consumer hashes
are checked after launch and recorded in `game-inputs.json`. For this checkpoint:

```sh
python3 scripts/probe-winrt-projection.py \
  --wine "$wine/bin/wine" --prefix /path/to/isolated-prefix \
  --metadata /path/to/contracts/ref/netstandard2.0 \
  --shim build/macos-x86/appxportable.dll --output build/game-projection-probe \
  --consumer /path/to/game/Bejeweled3.exe \
  --consumer /path/to/game/DT.GoogleAnalytics.Metro.dll \
  --game /path/to/game \
  --expected-game-error 'NotImplementedException: Windows.UI.Xaml.Application.OnActivated override'
```

The focused runtime regressions cover delegate ABI, concurrent wrapper identity,
concurrent and nested exception scopes, stale failures, and reentrant callbacks.
The x86 cross build, registered Wine CTest, full game probe, and 153 Python tests
passed locally. Windows and x64 runtime controls remain unverified for this slice.
