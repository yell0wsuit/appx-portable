file(MAKE_DIRECTORY "${FIXTURE}")
file(COPY "${SHIM}" "${HOST}" DESTINATION "${FIXTURE}")
file(REMOVE "${FIXTURE}/startup-passed.txt")
file(REMOVE "${FIXTURE}/App.xaml")
if(DEFINE MATCHES "XAML_LIGHT_THEME")
  file(WRITE "${FIXTURE}/App.xaml"
    "<Application xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" RequestedTheme=\"Light\"/>")
endif()
get_filename_component(framework "${CSC}" DIRECTORY)
file(TO_NATIVE_PATH "${SOURCE}" source_path)
file(TO_NATIVE_PATH "${FIXTURE}/game.exe" output_path)
execute_process(COMMAND "${CSC}" /nologo /platform:x86 /target:exe /nowarn:1701 "${DEFINE}"
  "/out:${output_path}" "/r:${METADATA}"
  "/r:${FACADES}/System.Runtime.dll" "/r:${FACADES}/System.ObjectModel.dll"
  "/r:${FACADES}/System.Runtime.InteropServices.WindowsRuntime.dll"
  "/r:${framework}/System.Runtime.WindowsRuntime.dll"
  "/r:${framework}/System.Runtime.WindowsRuntime.UI.Xaml.dll"
  "${source_path}" RESULT_VARIABLE compiled)
if(NOT "${compiled}" STREQUAL "0")
  message(FATAL_ERROR "Managed XAML fixture compilation failed: ${compiled}")
endif()
file(WRITE "${FIXTURE}/AppxManifest.xml"
  "<Package><Identity Name=\"ManagedXamlStartupTest\" Publisher=\"CN=Test\" Version=\"1.0.0.0\" ProcessorArchitecture=\"x86\"/><Applications><Application Id=\"Test\" Executable=\"game.exe\"/></Applications></Package>")
execute_process(COMMAND "${FIXTURE}/appx-host.exe" --trace
  WORKING_DIRECTORY "${FIXTURE}" RESULT_VARIABLE result TIMEOUT 15)
if(NOT "${result}" STREQUAL "0" OR NOT EXISTS "${FIXTURE}/startup-passed.txt")
  file(READ "${FIXTURE}/managed.log" log)
  message(FATAL_ERROR "Managed XAML startup failed (${result}): ${log}")
endif()
