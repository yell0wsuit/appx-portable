// Precompiled into every shim and test file (see CMakeLists.txt): the Windows
// and WinRT headers most files include, parsed once instead of once per file.
#pragma once
#include <windows.h>
#undef GetCurrentTime
#include <roapi.h>
#include <winstring.h>
#include <wrl.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.foundation.h>
#include <windows.ui.core.h>
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.controls.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>
