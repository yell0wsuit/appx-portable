#pragma once
#include <string>
namespace appx {
// Switches read once from the game's command line, each with an environment
// variable equivalent for scripts and tests. All are off by default. The two
// Store overrides can also be set in appx-portable.ini, [options] UnlockTrial=1
// and StubIap=1; a set environment variable wins over the file.
// [options] FrameRate=60 (the default) caps presents per second, 0 for no cap;
// APPX_PORTABLE_FRAME_RATE wins over it.
//   --log           write failures and notes to appx-portable.log  APPX_PORTABLE_LOG=1
//   --trace         also log every call traced (implies --log)     APPX_PORTABLE_TRACE=1
//   --capture       save the back buffer to appx-frame.bmp         APPX_PORTABLE_CAPTURE=1
//   --diagnostics   log crashes with stack details, check heaps    APPX_PORTABLE_DIAGNOSTICS=1
//   --passthrough   activate the OS classes and only log           APPX_PORTABLE_MODE=trace
//   --stub-iap      complete in-app purchases locally             APPX_PORTABLE_STUB_IAP=1
//   --unlock-trial  report an active, full base-app license        APPX_PORTABLE_UNLOCK_TRIAL=1
// [options] StubIapProducts=id1,id2 names products for the stub IAP listing of
// a game without a bundled catalog; APPX_PORTABLE_STUB_IAP_PRODUCTS wins over it.
// appx-native-host.exe sets APPX_PORTABLE_HOSTED=1 for a game it starts with
// the shim loaded; the shim clears it so the game's own children do not see it.
struct Options {
    bool log = false, trace = false, capture = false, diagnostics = false, passthrough = false;
    bool unlockTrial = false, stubIap = false, hosted = false;
    unsigned frameRate = 60;
    std::wstring stubIapProducts;
};
const Options& GetOptions() noexcept;
}
