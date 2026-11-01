// A game-folder DLL loaded only as an in-package component's dependency, as
// vccorlib110 is: it reaches Wine's aborting RoCaptureErrorContext stub unless
// the shim patches modules loaded alongside the component too.
#include <windows.h>
#include <roerrorapi.h>
extern "C" __declspec(dllexport) HRESULT __cdecl ReportComponentFailure() { return RoCaptureErrorContext(E_FAIL); }
