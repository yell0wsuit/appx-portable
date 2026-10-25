#pragma once
namespace appx {
// Switches read once from the game's command line, each with an environment
// variable equivalent for scripts and tests. All are off by default.
//   --log           write failures and notes to appx-portable.log  APPX_PORTABLE_LOG=1
//   --trace         also log every call traced (implies --log)     APPX_PORTABLE_TRACE=1
//   --capture       save the back buffer to appx-frame.bmp         APPX_PORTABLE_CAPTURE=1
//   --diagnostics   log crashes with stack details, check heaps    APPX_PORTABLE_DIAGNOSTICS=1
//   --passthrough   activate the OS classes and only log           APPX_PORTABLE_MODE=trace
//   --stub-iap      complete in-app purchases locally             APPX_PORTABLE_STUB_IAP=1
//   --unlock-trial  report an active, full base-app license        APPX_PORTABLE_UNLOCK_TRIAL=1
struct Options {
    bool log = false, trace = false, capture = false, diagnostics = false, passthrough = false;
    bool unlockTrial = false, stubIap = false;
};
const Options& GetOptions() noexcept;
}
