#pragma once
#include <windows.h>
#include <activation.h>
extern "C" HRESULT WINAPI ShimGetFactory(HSTRING name, IActivationFactory** factory) noexcept;
namespace appx {
HRESULT MakeThreadPoolFactory(bool timers, IActivationFactory** factory);
HRESULT MakeApplicationFactory(IActivationFactory** value);
HRESULT MakeControlFactory(const wchar_t* name, IActivationFactory** value);
HRESULT MakeDisplayFactory(IActivationFactory** v);
HRESULT MakeCoreApplicationFactory(IActivationFactory** value);
HRESULT MakeWindowFactory(IActivationFactory** value);
HRESULT MakeRectangleGeometryFactory(IActivationFactory** value);
HRESULT MakeDispatcherTimerFactory(IActivationFactory** value);
HRESULT MakeCompositionTargetFactory(IActivationFactory** value);
HRESULT MakeCursorFactory(IActivationFactory** value);
HRESULT MakeCoreWindowFactory(IActivationFactory** value);
HRESULT MakePackageFactory(IActivationFactory** value);
HRESULT MakeShareFactory(IActivationFactory** value);
HRESULT MakeMessageDialogFactory(IActivationFactory** value);
HRESULT MakeSettingsPaneFactory(IActivationFactory** value);
HRESULT MakeTileFactory(IActivationFactory** value);
// The offline Xbox LIVE stand-in; REGDB_E_CLASSNOTREG for Xbox classes it lacks.
HRESULT MakeXboxFactory(const wchar_t* name, IActivationFactory** value);
// Halfbrick's C# HTTP component, stood in offline.
HRESULT MakeHttpClientCSharpFactory(const wchar_t* name, IActivationFactory** value);
HRESULT MakeVisualizationFactory(IActivationFactory** value);
HRESULT MakeStorageFileFactory(IActivationFactory** value);
HRESULT MakeStorageFactory(IActivationFactory** value);
HRESULT MakeAdFactory(IActivationFactory** value);
HRESULT MakeHttpFactory(bool filter, IActivationFactory** value);
HRESULT MakeNetworkFactory(IActivationFactory** value);
HRESULT MakeApplicationViewFactory(IActivationFactory** v);
HRESULT MakeHardwareFactory(IActivationFactory** value);
HRESULT MakeDataReaderFactory(IActivationFactory** value);
HRESULT MakeStoreFactory(IActivationFactory** value);
HRESULT MakeToastFactory(IActivationFactory** value);
HRESULT MakeScheduledToastFactory(IActivationFactory** value);
HRESULT MakeDeviceFactory(bool display, IActivationFactory** value);
HRESULT MakeCausalityFactory(IActivationFactory** value);
HRESULT MakeCalendarFactory(IActivationFactory** value);
void InstallActivationHook() noexcept;
// The OS activation functions, bypassing the hook: for shim code that wraps an OS class.
HRESULT OsGetActivationFactory(HSTRING name, REFIID iid, void** out) noexcept;
HRESULT OsActivateInstance(HSTRING name, IInspectable** out) noexcept;
}
