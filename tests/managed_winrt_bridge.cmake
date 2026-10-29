file(MAKE_DIRECTORY "${FIXTURE}")
file(COPY "${SHIM}" "${BRIDGE}" DESTINATION "${FIXTURE}")
file(TO_NATIVE_PATH "${SOURCE}" source_path)
file(TO_NATIVE_PATH "${FIXTURE}/AppxPortable.WinRT.dll" bridge_path)
file(TO_NATIVE_PATH "${FIXTURE}/managed-winrt-bridge.exe" output_path)
execute_process(COMMAND "${CSC}" /nologo /codepage:65001 /warnaserror+ /platform:${ARCH} /target:exe
  "/r:${bridge_path}" "/out:${output_path}" "${source_path}" RESULT_VARIABLE compiled)
if(NOT "${compiled}" STREQUAL "0")
  message(FATAL_ERROR "Managed WinRT bridge fixture compilation failed: ${compiled}")
endif()
file(WRITE "${FIXTURE}/AppxManifest.xml"
  "<Package><Identity Name=\"ManagedWinrtBridgeTest\" Publisher=\"CN=bridge-đá-💎\" Version=\"1.0.0.0\" ProcessorArchitecture=\"${ARCH}\"/><Applications><Application Id=\"Test\" Executable=\"managed-winrt-bridge.exe\"/></Applications></Package>")
execute_process(COMMAND "${FIXTURE}/managed-winrt-bridge.exe"
  WORKING_DIRECTORY "${FIXTURE}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
if(NOT "${result}" STREQUAL "0")
  message(FATAL_ERROR "Managed WinRT bridge failed (${result}): ${output}${error}")
endif()
message(STATUS "${output}")
