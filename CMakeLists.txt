cmake_minimum_required(VERSION 3.25)
project(appx_portable LANGUAGES CXX RC)
if(CMAKE_CROSSCOMPILING AND NOT EXISTS "${CMAKE_LINKER}")
  find_program(APPX_LLD NAMES lld-link HINTS "/opt/homebrew/opt/lld/bin" REQUIRED)
  set(CMAKE_LINKER "${APPX_LLD}")
endif()
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Requires Windows MSVC ABI (MSVC or clang-cl + xwin)")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  enable_language(ASM_MASM)
  set(APPX_EXPORTS shim/appxportable-x64.def)
  set(APPX_HOST_ARCH x64)
else()
  set(APPX_EXPORTS shim/appxportable.def)
  set(APPX_HOST_ARCH x86)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(APPX_TEST_WEBVIEW2 "Run WebView2 integration tests (requires the Evergreen runtime)" OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0603)
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/W4;/EHsc;/utf-8>")
# MSBuild compiles a target's files one at a time without /MP; Ninja already
# runs them in parallel.
if(CMAKE_GENERATOR MATCHES "Visual Studio")
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/MP>")
endif()
add_link_options(/DEBUG /INCREMENTAL:NO)
# Release builds pass their tag; local builds describe the git checkout. The
# header is refreshed on every build, so it names the commit actually built.
set(APPX_PORTABLE_VERSION "" CACHE STRING "Release tag (vYYYY.M.D or vYYYY.M.D-N); empty for local builds")
find_package(Git QUIET)
set(APPX_GENERATED "${CMAKE_BINARY_DIR}/generated")
add_custom_target(appxportable_version
  COMMAND "${CMAKE_COMMAND}" "-DVERSION=${APPX_PORTABLE_VERSION}" "-DGIT=${GIT_EXECUTABLE}" "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
          "-DOUTPUT=${APPX_GENERATED}/version.h" -P "${CMAKE_SOURCE_DIR}/cmake/version.cmake"
  BYPRODUCTS "${APPX_GENERATED}/version.h"
  VERBATIM)
add_executable(shimtest tests/shimtest.cpp)
target_link_libraries(shimtest PRIVATE runtimeobject ole32)
if(EXISTS "${CMAKE_SOURCE_DIR}/shim/registry.cpp")
  add_library(appxportable SHARED shim/registry.cpp shim/compat/win10/titlebar.cpp shim/compat/win10/input.cpp shim/compat/win10/xaml.cpp shim/compat/win10/memory.cpp shim/compat/win10/api_information.cpp shim/islands/islands.cpp shim/islands/panel.cpp shim/islands/scroll.cpp shim/islands/appearance.cpp shim/islands/mode.cpp shim/islands/webview.cpp shim/islands/webview_markup.cpp shim/islands/xbf_loader.cpp shim/xaml/xbf_webview.cpp shim/log.cpp shim/options.cpp shim/gpu/dxgi_redirect.cpp shim/entry/activation_hook.cpp shim/package/package.cpp shim/package/resources.cpp shim/xaml/xbf.cpp shim/platform/package_identity.cpp shim/xbox/xbox.cpp shim/com/failed_operation.cpp shim/com/pinterface.cpp shim/entry/diagnostics.cpp shim/platform/threading.cpp shim/platform/hardware.cpp shim/platform/input_capabilities.cpp shim/platform/sensors.cpp shim/platform/media_device.cpp shim/platform/device_information.cpp shim/platform/signal_notifier.cpp shim/platform/device_info.cpp shim/platform/data_reader.cpp shim/platform/application_view.cpp shim/platform/cursor.cpp shim/platform/visualization.cpp shim/platform/storage.cpp shim/platform/storage_file.cpp shim/platform/file_stream.cpp shim/platform/mf_stream.cpp shim/platform/settings.cpp shim/stubs/vungle.cpp shim/stubs/charms.cpp shim/stubs/search.cpp shim/stubs/http_client_csharp.cpp shim/stubs/store.cpp shim/stubs/notifications.cpp shim/stubs/xml.cpp shim/stubs/http.cpp shim/platform/display.cpp shim/platform/coreapp.cpp shim/platform/causality.cpp shim/platform/calendar.cpp shim/host/host.cpp shim/host/assets.cpp shim/host/placement.cpp shim/host/pointer.cpp shim/host/dispatcher.cpp shim/host/corewindow.cpp shim/xaml/application.cpp shim/xaml/controls.cpp shim/xaml/media.cpp shim/xaml/timer.cpp shim/xaml/window.cpp shim/xaml/data.cpp ${APPX_EXPORTS} shim/version.rc)
  target_include_directories(appxportable PRIVATE shim "${APPX_GENERATED}")
  add_dependencies(appxportable appxportable_version)
  target_link_libraries(appxportable PRIVATE runtimeobject ole32 oleaut32 dxgi d3d11 mfuuid shell32 shlwapi xmllite bcrypt psapi)
  include(cmake/webview2.cmake)
  add_executable(appx-native-host WIN32 host/native.cpp host/managed.manifest)
  target_link_libraries(appx-native-host PRIVATE appxportable)
  add_library(nativeappfixture SHARED tests/native_application.cpp tests/native_application.def)
  target_link_libraries(nativeappfixture PRIVATE runtimeobject ole32)
  add_test(NAME native_application_host COMMAND "${CMAKE_COMMAND}"
    "-DTEST_DLL=$<TARGET_FILE:nativeappfixture>" "-DHOST=$<TARGET_FILE:appx-native-host>"
    "-DSHIM=$<TARGET_FILE:appxportable>" "-DARCH=${APPX_HOST_ARCH}"
    "-DFIXTURE=${CMAKE_BINARY_DIR}/native-application-fixture"
    -P "${CMAKE_SOURCE_DIR}/tests/native_application.cmake")
  set_tests_properties(native_application_host PROPERTIES TIMEOUT 30)
  add_executable(nativelaunchfixture WIN32 tests/native_launch.cpp)
  target_link_libraries(nativelaunchfixture PRIVATE runtimeobject)
  add_test(NAME native_executable_launch COMMAND "${CMAKE_COMMAND}"
    "-DTEST_EXE=$<TARGET_FILE:nativelaunchfixture>" "-DPLUGIN=$<TARGET_FILE:nativeappfixture>"
    "-DHOST=$<TARGET_FILE:appx-native-host>" "-DSHIM=$<TARGET_FILE:appxportable>" "-DARCH=${APPX_HOST_ARCH}"
    "-DFIXTURE=${CMAKE_BINARY_DIR}/native-launch-fixture"
    -P "${CMAKE_SOURCE_DIR}/tests/native_launch.cmake")
  set_tests_properties(native_executable_launch PROPERTIES TIMEOUT 30)
  # One proxy per system DLL a game imports from its folder (see scripts/gen_proxy.py).
  set(APPX_PROXIES d3dcompiler_47 XAudio2_8 XAudio2_9 DWrite)
  foreach(proxy IN LISTS APPX_PROXIES)
    add_library(${proxy} SHARED proxy/proxy.cpp proxy/${proxy}/thunks.cpp proxy/${proxy}/proxy.def shim/version.rc)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      target_sources(${proxy} PRIVATE proxy/${proxy}/thunks-x64.asm)
    endif()
    target_include_directories(${proxy} PRIVATE "${APPX_GENERATED}" proxy/${proxy})
    target_compile_definitions(${proxy} PRIVATE APPX_PORTABLE_PROXY)
    add_dependencies(${proxy} appxportable_version)
    target_link_libraries(${proxy} PRIVATE appxportable)
  endforeach()
endif()
add_executable(xamltest tests/xamltest.cpp)
target_link_libraries(xamltest PRIVATE runtimeobject ole32)
add_executable(controltest tests/controltest.cpp)
target_link_libraries(controltest PRIVATE runtimeobject ole32)
add_executable(platformtest tests/platformtest.cpp)
target_link_libraries(platformtest PRIVATE runtimeobject ole32)
add_executable(proxytest tests/proxytest.cpp)
enable_testing()
add_executable(xbftest tests/xbftest.cpp shim/xaml/xbf.cpp shim/package/resources.cpp)
target_include_directories(xbftest PRIVATE shim)
target_link_libraries(xbftest PRIVATE ole32 shlwapi)
add_test(NAME xbftest COMMAND xbftest)
add_executable(xbfwebviewtest tests/xbfwebviewtest.cpp shim/xaml/xbf_webview.cpp)
target_include_directories(xbfwebviewtest PRIVATE shim)
add_test(NAME xbfwebviewtest COMMAND xbfwebviewtest)
find_program(APPX_MAKEPRI makepri HINTS
  "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
if(APPX_MAKEPRI)
  add_test(NAME xbf_pri COMMAND "${CMAKE_COMMAND}" "-DTEST=$<TARGET_FILE:xbftest>"
    "-DMAKEPRI=${APPX_MAKEPRI}" "-DFIXTURE=${CMAKE_BINARY_DIR}/xbf-pri-fixture"
    -P "${CMAKE_SOURCE_DIR}/tests/xbf_pri.cmake")
endif()
add_executable(assettest tests/assettest.cpp shim/host/assets.cpp)
target_include_directories(assettest PRIVATE shim)
add_executable(packagetest tests/packagetest.cpp shim/package/package.cpp)
target_include_directories(packagetest PRIVATE shim)
target_link_libraries(packagetest PRIVATE xmllite bcrypt shlwapi)
add_test(NAME packagetest COMMAND packagetest)
add_executable(standintest tests/standintest.cpp shim/com/pinterface.cpp)
target_include_directories(standintest PRIVATE shim)
target_link_libraries(standintest PRIVATE runtimeobject ole32 bcrypt)
add_test(NAME standintest COMMAND standintest)
add_test(NAME assettest COMMAND assettest)
add_executable(videotest tests/videotest.cpp)
target_include_directories(videotest PRIVATE shim)
add_test(NAME videotest COMMAND videotest)
add_executable(pacetest tests/pacetest.cpp)
target_include_directories(pacetest PRIVATE shim)
add_test(NAME pacetest COMMAND pacetest)
add_test(NAME shimtest COMMAND shimtest)
add_test(NAME xamltest COMMAND xamltest)
add_test(NAME controltest COMMAND controltest)
add_test(NAME platformtest COMMAND platformtest)
add_test(NAME proxytest COMMAND proxytest)
add_executable(gputest tests/gputest.cpp)
target_link_libraries(gputest PRIVATE runtimeobject ole32 d3d11 dxgi)
add_test(NAME gputest COMMAND gputest)
add_executable(capture tests/capture.cpp)
target_link_libraries(capture PRIVATE gdi32 user32)
add_executable(storagetest tests/storagetest.cpp)
target_link_libraries(storagetest PRIVATE runtimeobject ole32)
add_test(NAME storagetest COMMAND storagetest)
add_test(NAME settings_write COMMAND storagetest write)
set_tests_properties(settings_write PROPERTIES FIXTURES_SETUP settings_roundtrip)
add_test(NAME settings_read COMMAND storagetest read)
set_tests_properties(settings_read PROPERTIES FIXTURES_REQUIRED settings_roundtrip)
add_test(NAME settings_atomic COMMAND storagetest atomic)
add_test(NAME settings_burst COMMAND storagetest burst)
add_test(NAME settings_torn COMMAND storagetest torn)
add_test(NAME settings_special_write COMMAND storagetest specialwrite)
set_tests_properties(settings_special_write PROPERTIES FIXTURES_SETUP settings_special_roundtrip)
add_test(NAME settings_special_read COMMAND storagetest specialread)
set_tests_properties(settings_special_read PROPERTIES FIXTURES_REQUIRED settings_special_roundtrip)
set_tests_properties(storagetest settings_write settings_read settings_atomic settings_burst settings_torn settings_special_write settings_special_read PROPERTIES RESOURCE_LOCK appx_settings_store)
add_executable(stubtest tests/stubtest.cpp)
target_include_directories(stubtest PRIVATE shim)
target_link_libraries(stubtest PRIVATE runtimeobject ole32)
add_test(NAME stubtest COMMAND stubtest)
add_executable(hardwaretest tests/hardwaretest.cpp)
target_link_libraries(hardwaretest PRIVATE runtimeobject ole32)
add_test(NAME hardwaretest COMMAND hardwaretest)
add_executable(viewtest tests/viewtest.cpp)
target_link_libraries(viewtest PRIVATE runtimeobject ole32)
add_test(NAME viewtest COMMAND viewtest)
add_executable(contexttest tests/contexttest.cpp)
target_link_libraries(contexttest PRIVATE runtimeobject ole32)
add_test(NAME contexttest COMMAND contexttest)
add_executable(win10test tests/win10test.cpp)
target_link_libraries(win10test PRIVATE runtimeobject ole32 user32)
add_test(NAME win10test COMMAND win10test)
add_executable(readertest tests/readertest.cpp)
target_link_libraries(readertest PRIVATE runtimeobject ole32)
add_test(NAME readertest COMMAND readertest)
add_executable(toasttest tests/toasttest.cpp)
target_link_libraries(toasttest PRIVATE runtimeobject ole32)
add_test(NAME toasttest COMMAND toasttest)
add_executable(xamlsettingstest tests/xamlsettingstest.cpp host/managed.manifest)
target_link_libraries(xamlsettingstest PRIVATE runtimeobject ole32 user32)
add_test(NAME xaml_settings_contract COMMAND xamlsettingstest)
add_test(NAME islands_settings_contract COMMAND "${CMAKE_COMMAND}"
  "-DTEST_EXE=$<TARGET_FILE:xamlsettingstest>"
  "-DSHIM=$<TARGET_FILE:appxportable>"
  "-DHOST=${CMAKE_CURRENT_BINARY_DIR}/appx-host.exe" "-DARCH=${APPX_HOST_ARCH}"
  "-DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/islands-settings-fixture"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/islands_runtime.cmake")
add_test(NAME islands_view_contract COMMAND "${CMAKE_COMMAND}"
  "-DTEST_EXE=$<TARGET_FILE:viewtest>"
  "-DSHIM=$<TARGET_FILE:appxportable>"
  "-DHOST=${CMAKE_CURRENT_BINARY_DIR}/appx-host.exe" "-DARCH=${APPX_HOST_ARCH}"
  "-DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/islands-view-fixture"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/islands_runtime.cmake")
set_tests_properties(islands_view_contract PROPERTIES TIMEOUT 60 LABELS managed-clr)
add_test(NAME islands_hardware_contract COMMAND "${CMAKE_COMMAND}"
  "-DTEST_EXE=$<TARGET_FILE:hardwaretest>"
  "-DSHIM=$<TARGET_FILE:appxportable>"
  "-DHOST=${CMAKE_CURRENT_BINARY_DIR}/appx-host.exe" "-DARCH=${APPX_HOST_ARCH}"
  "-DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/islands-hardware-fixture"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/islands_runtime.cmake")
set_tests_properties(islands_hardware_contract PROPERTIES TIMEOUT 60 LABELS managed-clr)
set_tests_properties(xaml_settings_contract islands_settings_contract PROPERTIES TIMEOUT 60)
add_executable(searchtest tests/searchtest.cpp host/managed.manifest)
target_link_libraries(searchtest PRIVATE runtimeobject ole32)
add_test(NAME searchtest COMMAND searchtest)
set_tests_properties(searchtest PROPERTIES TIMEOUT 60)
if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT CMAKE_CROSSCOMPILING)
  add_executable(webviewtest tests/webviewtest.cpp host/managed.manifest)
  target_link_libraries(webviewtest PRIVATE runtimeobject ole32)
  add_test(NAME webviewtest COMMAND "${CMAKE_COMMAND}"
    "-DTEST=$<TARGET_FILE:webviewtest>" "-DSHIM=$<TARGET_FILE:appxportable>"
    "-DHOST=${CMAKE_BINARY_DIR}/appx-host.exe" "-DFIXTURE=${CMAKE_BINARY_DIR}/webview-contract"
    -P "${CMAKE_SOURCE_DIR}/tests/webview_contract.cmake")
  set_tests_properties(webviewtest PROPERTIES TIMEOUT 60)
endif()

if(WIN32)
  add_test(NAME islands_toasttest COMMAND "${CMAKE_COMMAND}"
    "-DTEST_EXE=$<TARGET_FILE:toasttest>"
    "-DSHIM=$<TARGET_FILE:appxportable>"
    "-DHOST=${CMAKE_CURRENT_BINARY_DIR}/appx-host.exe" "-DARCH=${APPX_HOST_ARCH}"
    "-DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/islands-toast-fixture"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/islands_runtime.cmake")
endif()
add_executable(storetest tests/storetest.cpp)
target_link_libraries(storetest PRIVATE runtimeobject ole32)
add_test(NAME storetest COMMAND storetest)
add_test(NAME store_stub_iap_cli COMMAND storetest --expect-stub-iap --stub-iap)
add_test(NAME store_stub_iap_env COMMAND storetest --expect-stub-iap)
add_test(NAME store_stub_iap_disabled COMMAND storetest)
add_test(NAME store_stub_iap_dispatcher COMMAND storetest --expect-stub-iap --expect-host --stub-iap)
set_tests_properties(store_stub_iap_dispatcher PROPERTIES TIMEOUT 60
    ENVIRONMENT "APPX_PORTABLE_UNLOCK_TRIAL=0;APPX_PORTABLE_STUB_IAP=0;APPX_PORTABLE_DATA=${CMAKE_CURRENT_BINARY_DIR}/store-host-save")
add_test(NAME store_stub_iap_catalog COMMAND ${CMAKE_COMMAND}
    "-DTEST=$<TARGET_FILE:storetest>" "-DSHIM=$<TARGET_FILE:appxportable>"
    "-DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/store-catalog-fixture"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/store_catalog.cmake")
add_test(NAME store_stub_iap_discovered_catalog COMMAND ${CMAKE_COMMAND}
    "-DTEST=$<TARGET_FILE:storetest>" "-DSHIM=$<TARGET_FILE:appxportable>"
    "-DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/store-discovered-catalog-fixture"
    "-DCATALOG=Content/Arbitrary/Subdirectory/products.XML"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/store_catalog.cmake")
# Store options from appx-portable.ini beside the game; a set variable wins.
# ENV items are "|"-separated: add_test would split a ";" list.
function(add_store_settings_test name ini env args)
    add_test(NAME store_${name} COMMAND ${CMAKE_COMMAND}
        "-DTEST=$<TARGET_FILE:storetest>" "-DSHIM=$<TARGET_FILE:appxportable>"
        "-DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/store-settings-${name}"
        "-DINI=${ini}" "-DENV=${env}" "-DARGS=${args}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/store_settings.cmake")
endfunction()
add_store_settings_test(unlocked_ini "[options]\\nUnlockTrial=1"
    "--unset=APPX_PORTABLE_UNLOCK_TRIAL|APPX_PORTABLE_STUB_IAP=0" --expect-unlocked)
add_store_settings_test(stub_iap_ini "[options]\\nStubIap=1"
    "--unset=APPX_PORTABLE_STUB_IAP|APPX_PORTABLE_UNLOCK_TRIAL=0" --expect-stub-iap)
add_store_settings_test(ini_env_off "[options]\\nUnlockTrial=1\\nStubIap=1"
    "APPX_PORTABLE_UNLOCK_TRIAL=0|APPX_PORTABLE_STUB_IAP=0" "")
add_store_settings_test(ini_absent_section "[storage]\\nSaveLocation=game"
    "--unset=APPX_PORTABLE_UNLOCK_TRIAL|--unset=APPX_PORTABLE_STUB_IAP" "")
add_test(NAME store_unlocked_cli COMMAND storetest --expect-unlocked --unlock-trial)
add_test(NAME store_unlocked_env COMMAND storetest --expect-unlocked)
add_test(NAME store_override_disabled COMMAND storetest)
set_tests_properties(storetest store_unlocked_cli PROPERTIES ENVIRONMENT_MODIFICATION "APPX_PORTABLE_UNLOCK_TRIAL=unset:")
set_tests_properties(store_unlocked_env PROPERTIES ENVIRONMENT_MODIFICATION "APPX_PORTABLE_UNLOCK_TRIAL=set:1")
set_tests_properties(store_override_disabled PROPERTIES ENVIRONMENT_MODIFICATION "APPX_PORTABLE_UNLOCK_TRIAL=set:0")
set_tests_properties(storetest store_unlocked_cli store_unlocked_env store_override_disabled PROPERTIES
    ENVIRONMENT "APPX_PORTABLE_STUB_IAP=0")
set_tests_properties(store_stub_iap_cli store_stub_iap_env store_stub_iap_disabled PROPERTIES
    ENVIRONMENT "APPX_PORTABLE_UNLOCK_TRIAL=0")
set_tests_properties(store_stub_iap_cli PROPERTIES ENVIRONMENT_MODIFICATION "APPX_PORTABLE_STUB_IAP=unset:")
set_tests_properties(store_stub_iap_env PROPERTIES ENVIRONMENT_MODIFICATION "APPX_PORTABLE_STUB_IAP=set:1")
set_tests_properties(store_stub_iap_disabled PROPERTIES ENVIRONMENT_MODIFICATION "APPX_PORTABLE_STUB_IAP=set:0")
add_executable(devicetest tests/devicetest.cpp)
target_link_libraries(devicetest PRIVATE runtimeobject ole32)
add_test(NAME devicetest COMMAND devicetest)
add_executable(navtest tests/navtest.cpp)
target_link_libraries(navtest PRIVATE runtimeobject ole32)
add_test(NAME navtest COMMAND navtest)
add_executable(mediatest tests/mediatest.cpp)
target_link_libraries(mediatest PRIVATE runtimeobject ole32 shlwapi)
add_test(NAME mediatest COMMAND mediatest)
set_tests_properties(mediatest PROPERTIES TIMEOUT 60)
add_executable(mfstreamtest tests/mfstreamtest.cpp)
target_link_libraries(mfstreamtest PRIVATE runtimeobject ole32 mfplat mfuuid shlwapi)
add_test(NAME mfstreamtest COMMAND mfstreamtest)
set_tests_properties(mfstreamtest PROPERTIES TIMEOUT 30)
add_executable(cursortest tests/cursortest.cpp tests/cursortest.rc)
target_link_libraries(cursortest PRIVATE runtimeobject ole32 user32)
add_test(NAME cursortest COMMAND cursortest)
add_executable(focustest tests/focustest.cpp)
target_link_libraries(focustest PRIVATE runtimeobject ole32 user32)
add_test(NAME focustest COMMAND focustest)
add_test(NAME focus_maximized COMMAND focustest maximized)
add_executable(optionstest tests/optionstest.cpp)
add_test(NAME logging_off COMMAND optionstest)
add_test(NAME logging_on COMMAND optionstest --log)
add_test(NAME logging_trace COMMAND optionstest --trace)
set_tests_properties(logging_off logging_on logging_trace PROPERTIES RESOURCE_LOCK appx_log_file)
# Tests keep saves in the build tree, never in the player's save folder.
get_property(appx_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${appx_tests} PROPERTIES ENVIRONMENT "APPX_PORTABLE_DATA=${CMAKE_BINARY_DIR}/test-data;APPX_PORTABLE_WINDOW_STATE=off")
# Save-location tests resolve the folder themselves, as the game does.
add_test(NAME storage_location COMMAND storagetest location)
add_test(NAME storage_location_ini COMMAND storagetest location_ini)
set_tests_properties(storage_location storage_location_ini PROPERTIES
  ENVIRONMENT "LOCALAPPDATA=${CMAKE_BINARY_DIR}/test-localappdata"
  ENVIRONMENT_MODIFICATION "APPX_PORTABLE_DATA=unset:"
  RESOURCE_LOCK appx_save_location)
add_executable(windowtest tests/windowtest.cpp)
target_link_libraries(windowtest PRIVATE runtimeobject ole32 user32)
foreach(mode normal max full maxfull)
  add_test(NAME window_${mode}_write COMMAND windowtest write ${mode})
  add_test(NAME window_${mode}_read COMMAND windowtest read ${mode})
  set_tests_properties(window_${mode}_write PROPERTIES FIXTURES_SETUP window_${mode})
  set_tests_properties(window_${mode}_read PROPERTIES FIXTURES_REQUIRED window_${mode})
  set_tests_properties(window_${mode}_write window_${mode}_read PROPERTIES
    ENVIRONMENT "APPX_PORTABLE_DATA=${CMAKE_BINARY_DIR}/window-test-data/${mode}"
    ENVIRONMENT_MODIFICATION "APPX_PORTABLE_WINDOW_STATE=unset:"
    RESOURCE_LOCK appx_window_${mode} TIMEOUT 60)
endforeach()
foreach(mode corrupt offscreen above bottomright oversized negative overflow legacy invalidstate numericstate invalidsize)
  add_test(NAME window_${mode} COMMAND windowtest invalid ${mode})
  set_tests_properties(window_${mode} PROPERTIES
    ENVIRONMENT "APPX_PORTABLE_DATA=${CMAKE_BINARY_DIR}/window-test-data/${mode}"
    ENVIRONMENT_MODIFICATION "APPX_PORTABLE_WINDOW_STATE=unset:" TIMEOUT 60)
endforeach()
foreach(mode normal max full)
  add_test(NAME window_manual_${mode} COMMAND windowtest manual ${mode})
  set_tests_properties(window_manual_${mode} PROPERTIES
    ENVIRONMENT "APPX_PORTABLE_DATA=${CMAKE_BINARY_DIR}/window-test-data/manual-${mode}"
    ENVIRONMENT_MODIFICATION "APPX_PORTABLE_WINDOW_STATE=unset:" TIMEOUT 60)
endforeach()

# Desktop CLR host carries the XAML Islands compatibility manifest.
if(NOT CMAKE_CROSSCOMPILING)
  set(APPX_CSC "$ENV{WINDIR}/Microsoft.NET/Framework/v4.0.30319/csc.exe")
  file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/host/managed.cs" APPX_HOST_SOURCE)
  add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/appx-host.exe"
    COMMAND "${APPX_CSC}" /nologo /platform:${APPX_HOST_ARCH} /target:winexe
      /win32manifest:${CMAKE_SOURCE_DIR}/host/managed.manifest
      /r:System.Windows.Forms.dll /out:${CMAKE_BINARY_DIR}/appx-host.exe
      "${APPX_HOST_SOURCE}"
    DEPENDS host/managed.cs host/managed.manifest VERBATIM)
  add_custom_target(appx_managed_host ALL DEPENDS "${CMAKE_BINARY_DIR}/appx-host.exe")
  add_test(NAME xbox11_standins COMMAND "${CMAKE_COMMAND}"
    "-DCSC=${APPX_CSC}" "-DTEST_EXE=$<TARGET_FILE:standintest>"
    "-DSHIM=$<TARGET_FILE:appxportable>" "-DFIXTURE=${CMAKE_BINARY_DIR}/xbox11-fixture"
    -P "${CMAKE_SOURCE_DIR}/tests/xbox11_runtime.cmake")
  set_tests_properties(xbox11_standins PROPERTIES TIMEOUT 60)
  add_test(NAME xbox20_standins COMMAND "${CMAKE_COMMAND}"
    "-DCSC=${APPX_CSC}" "-DTEST_EXE=$<TARGET_FILE:standintest>"
    "-DSHIM=$<TARGET_FILE:appxportable>" "-DFIXTURE=${CMAKE_BINARY_DIR}/xbox20-fixture"
    "-DSDK_VERSION=2.0.0.0" "-DTEST_ARGS=--modern-user"
    -P "${CMAKE_SOURCE_DIR}/tests/xbox11_runtime.cmake")
  set_tests_properties(xbox20_standins PROPERTIES TIMEOUT 60)
  foreach(mode COREWINDOW XAML)
    add_test(NAME managed_host_${mode} COMMAND "${CMAKE_COMMAND}"
      "-DCSC=${APPX_CSC}" "-DDEFINE=/define:${mode}"
      "-DSOURCE=${CMAKE_SOURCE_DIR}/tests/managed_host.cs"
      "-DSHIM=$<TARGET_FILE:appxportable>" "-DHOST=${CMAKE_BINARY_DIR}/appx-host.exe" "-DARCH=${APPX_HOST_ARCH}"
      "-DFIXTURE=${CMAKE_BINARY_DIR}/managed-host-${mode}"
      -P "${CMAKE_SOURCE_DIR}/tests/managed_host.cmake")
    set_tests_properties(managed_host_${mode} PROPERTIES TIMEOUT 60 LABELS managed-clr)
  endforeach()
  find_file(APPX_WINDOWS_METADATA Windows.winmd
    PATHS "$ENV{WindowsSdkDir}/UnionMetadata/$ENV{WindowsSDKVersion}" NO_DEFAULT_PATH)
  set(APPX_FRAMEWORK_FACADES "$ENV{ProgramFiles\(x86\)}/Reference Assemblies/Microsoft/Framework/.NETFramework/v4.8/Facades")
  if(APPX_WINDOWS_METADATA AND EXISTS "${APPX_FRAMEWORK_FACADES}/System.Runtime.dll")
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
      add_test(NAME managed_webview_xbf_contract COMMAND "${CMAKE_COMMAND}"
        "-DCSC=${APPX_CSC}" "-DDEFINE=/define:XBF"
        "-DMETADATA=${APPX_WINDOWS_METADATA}" "-DFACADES=${APPX_FRAMEWORK_FACADES}"
        "-DSOURCE=${CMAKE_SOURCE_DIR}/tests/managed_webview_xbf.cs"
        "-DXBF_BUILDER=$<TARGET_FILE:xbfwebviewtest>"
        "-DSHIM=$<TARGET_FILE:appxportable>" "-DHOST=${CMAKE_BINARY_DIR}/appx-host.exe" "-DARCH=${APPX_HOST_ARCH}"
        "-DFIXTURE=${CMAKE_BINARY_DIR}/managed-webview-xbf-contract"
        -P "${CMAKE_SOURCE_DIR}/tests/managed_xaml_startup.cmake")
      set_tests_properties(managed_webview_xbf_contract PROPERTIES TIMEOUT 60 LABELS managed-clr)
      add_test(NAME managed_webview_markup_contract COMMAND "${CMAKE_COMMAND}"
        "-DCSC=${APPX_CSC}" "-DDEFINE=/define:MARKUP"
        "-DMETADATA=${APPX_WINDOWS_METADATA}" "-DFACADES=${APPX_FRAMEWORK_FACADES}"
        "-DSOURCE=${CMAKE_SOURCE_DIR}/tests/managed_webview_markup.cs"
        "-DSHIM=$<TARGET_FILE:appxportable>" "-DHOST=${CMAKE_BINARY_DIR}/appx-host.exe" "-DARCH=${APPX_HOST_ARCH}"
        "-DFIXTURE=${CMAKE_BINARY_DIR}/managed-webview-markup-contract"
        -P "${CMAKE_SOURCE_DIR}/tests/managed_xaml_startup.cmake")
      set_tests_properties(managed_webview_markup_contract PROPERTIES TIMEOUT 60 LABELS managed-clr)
    endif()
    if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND APPX_TEST_WEBVIEW2)
      add_test(NAME managed_webview_xbf COMMAND "${CMAKE_COMMAND}"
        "-DCSC=${APPX_CSC}" "-DDEFINE=/define:COMPILED,SCROLL"
        "-DMETADATA=${APPX_WINDOWS_METADATA}" "-DFACADES=${APPX_FRAMEWORK_FACADES}"
        "-DSOURCE=${CMAKE_SOURCE_DIR}/tests/managed_webview.cs"
        "-DXBF_BUILDER=$<TARGET_FILE:xbfwebviewtest>"
        "-DSHIM=$<TARGET_FILE:appxportable>" "-DHOST=${CMAKE_BINARY_DIR}/appx-host.exe" "-DARCH=${APPX_HOST_ARCH}"
        "-DFIXTURE=${CMAKE_BINARY_DIR}/managed-webview-xbf"
        -P "${CMAKE_SOURCE_DIR}/tests/managed_xaml_startup.cmake")
      set_tests_properties(managed_webview_xbf PROPERTIES TIMEOUT 60 LABELS "managed-clr;webview2" RUN_SERIAL TRUE)
      add_test(NAME managed_webview COMMAND "${CMAKE_COMMAND}"
        "-DCSC=${APPX_CSC}" "-DDEFINE=/define:WEBVIEW"
        "-DMETADATA=${APPX_WINDOWS_METADATA}" "-DFACADES=${APPX_FRAMEWORK_FACADES}"
        "-DSOURCE=${CMAKE_SOURCE_DIR}/tests/managed_webview.cs"
        "-DSHIM=$<TARGET_FILE:appxportable>" "-DHOST=${CMAKE_BINARY_DIR}/appx-host.exe" "-DARCH=${APPX_HOST_ARCH}"
        "-DFIXTURE=${CMAKE_BINARY_DIR}/managed-webview"
        -P "${CMAKE_SOURCE_DIR}/tests/managed_xaml_startup.cmake")
      set_tests_properties(managed_webview PROPERTIES TIMEOUT 60 LABELS "managed-clr;webview2")
      add_test(NAME managed_webview_markup COMMAND "${CMAKE_COMMAND}"
        "-DCSC=${APPX_CSC}" "-DDEFINE=/define:MARKUP"
        "-DMETADATA=${APPX_WINDOWS_METADATA}" "-DFACADES=${APPX_FRAMEWORK_FACADES}"
        "-DSOURCE=${CMAKE_SOURCE_DIR}/tests/managed_webview.cs"
        "-DSHIM=$<TARGET_FILE:appxportable>" "-DHOST=${CMAKE_BINARY_DIR}/appx-host.exe" "-DARCH=${APPX_HOST_ARCH}"
        "-DFIXTURE=${CMAKE_BINARY_DIR}/managed-webview-markup"
        -P "${CMAKE_SOURCE_DIR}/tests/managed_xaml_startup.cmake")
      set_tests_properties(managed_webview_markup PROPERTIES TIMEOUT 60 LABELS "managed-clr;webview2")
      add_test(NAME managed_webview_scroll COMMAND "${CMAKE_COMMAND}"
        "-DCSC=${APPX_CSC}" "-DDEFINE=/define:MARKUP,SCROLL"
        "-DMETADATA=${APPX_WINDOWS_METADATA}" "-DFACADES=${APPX_FRAMEWORK_FACADES}"
        "-DSOURCE=${CMAKE_SOURCE_DIR}/tests/managed_webview.cs"
        "-DSHIM=$<TARGET_FILE:appxportable>" "-DHOST=${CMAKE_BINARY_DIR}/appx-host.exe" "-DARCH=${APPX_HOST_ARCH}"
        "-DFIXTURE=${CMAKE_BINARY_DIR}/managed-webview-scroll"
        -P "${CMAKE_SOURCE_DIR}/tests/managed_xaml_startup.cmake")
      set_tests_properties(managed_webview_scroll PROPERTIES TIMEOUT 60 LABELS "managed-clr;webview2" RUN_SERIAL TRUE)
      add_test(NAME managed_webview_clip COMMAND "${CMAKE_COMMAND}"
        "-DCSC=${APPX_CSC}" "-DDEFINE=/define:CLIP"
        "-DMETADATA=${APPX_WINDOWS_METADATA}" "-DFACADES=${APPX_FRAMEWORK_FACADES}"
        "-DSOURCE=${CMAKE_SOURCE_DIR}/tests/managed_webview.cs"
        "-DSHIM=$<TARGET_FILE:appxportable>" "-DHOST=${CMAKE_BINARY_DIR}/appx-host.exe" "-DARCH=${APPX_HOST_ARCH}"
        "-DFIXTURE=${CMAKE_BINARY_DIR}/managed-webview-clip"
        -P "${CMAKE_SOURCE_DIR}/tests/managed_xaml_startup.cmake")
      set_tests_properties(managed_webview_clip PROPERTIES TIMEOUT 60 LABELS "managed-clr;webview2")
    endif()
    add_test(NAME managed_settings COMMAND "${CMAKE_COMMAND}"
      "-DCSC=${APPX_CSC}" "-DDEFINE=/define:SETTINGS"
      "-DMETADATA=${APPX_WINDOWS_METADATA}" "-DFACADES=${APPX_FRAMEWORK_FACADES}"
      "-DSOURCE=${CMAKE_SOURCE_DIR}/tests/managed_settings.cs"
      "-DSHIM=$<TARGET_FILE:appxportable>" "-DHOST=${CMAKE_BINARY_DIR}/appx-host.exe" "-DARCH=${APPX_HOST_ARCH}"
      "-DFIXTURE=${CMAKE_BINARY_DIR}/managed-settings"
      -P "${CMAKE_SOURCE_DIR}/tests/managed_xaml_startup.cmake")
    set_tests_properties(managed_settings PROPERTIES TIMEOUT 60 LABELS managed-clr)
    add_test(NAME managed_scroll COMMAND "${CMAKE_COMMAND}"
      "-DCSC=${APPX_CSC}" "-DDEFINE=/define:SCROLL"
      "-DMETADATA=${APPX_WINDOWS_METADATA}" "-DFACADES=${APPX_FRAMEWORK_FACADES}"
      "-DSOURCE=${CMAKE_SOURCE_DIR}/tests/managed_scroll.cs"
      "-DSHIM=$<TARGET_FILE:appxportable>" "-DHOST=${CMAKE_BINARY_DIR}/appx-host.exe" "-DARCH=${APPX_HOST_ARCH}"
      "-DFIXTURE=${CMAKE_BINARY_DIR}/managed-scroll"
      -P "${CMAKE_SOURCE_DIR}/tests/managed_xaml_startup.cmake")
    set_tests_properties(managed_scroll PROPERTIES TIMEOUT 60 LABELS managed-clr)
    foreach(mode CONSTRUCTOR_CONTROLS LAUNCH_CONTROLS LIGHT_THEME ROOT_LIGHT_THEME XAML_LIGHT_THEME APPLICATION_EXIT)
      add_test(NAME managed_xaml_startup_${mode} COMMAND "${CMAKE_COMMAND}"
        "-DCSC=${APPX_CSC}" "-DDEFINE=/define:${mode}"
        "-DMETADATA=${APPX_WINDOWS_METADATA}" "-DFACADES=${APPX_FRAMEWORK_FACADES}"
        "-DSOURCE=${CMAKE_SOURCE_DIR}/tests/managed_xaml_startup.cs"
        "-DSHIM=$<TARGET_FILE:appxportable>" "-DHOST=${CMAKE_BINARY_DIR}/appx-host.exe" "-DARCH=${APPX_HOST_ARCH}"
        "-DFIXTURE=${CMAKE_BINARY_DIR}/managed-xaml-startup-${mode}"
        -P "${CMAKE_SOURCE_DIR}/tests/managed_xaml_startup.cmake")
      set_tests_properties(managed_xaml_startup_${mode} PROPERTIES TIMEOUT 60 LABELS managed-clr)
    endforeach()
  else()
    message(STATUS "Managed XAML startup tests need Windows SDK metadata and .NET Framework 4.8 reference assemblies")
  endif()
endif()

add_test(NAME containers_write COMMAND storagetest containers_write)
add_test(NAME containers_read COMMAND storagetest containers_read)
set_tests_properties(containers_write PROPERTIES FIXTURES_SETUP container_roundtrip)
set_tests_properties(containers_read PROPERTIES FIXTURES_REQUIRED container_roundtrip)
set_tests_properties(containers_write containers_read PROPERTIES
  ENVIRONMENT "APPX_PORTABLE_DATA=${CMAKE_BINARY_DIR}/container-test-data;APPX_PORTABLE_WINDOW_STATE=off"
  RESOURCE_LOCK appx_container_store)

# Most files include the same Windows and WinRT headers; the shim and tests
# share one precompiled copy of shim/pch.h. The proxies define
# APPX_PORTABLE_PROXY, and the files that raise _WIN32_WINNT above
# <windows.h> must parse it themselves.
add_library(appx_pch OBJECT shim/pch.cpp)
target_precompile_headers(appx_pch PRIVATE shim/pch.h)
get_property(appx_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
list(REMOVE_ITEM appx_targets ${APPX_PROXIES} xamlsettingstest searchtest webviewtest)
foreach(target IN LISTS appx_targets)
  get_target_property(type ${target} TYPE)
  if(type STREQUAL "EXECUTABLE" OR type STREQUAL "SHARED_LIBRARY")
    target_precompile_headers(${target} REUSE_FROM appx_pch)
  endif()
endforeach()
set_source_files_properties(shim/islands/islands.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
