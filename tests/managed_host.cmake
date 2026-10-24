file(MAKE_DIRECTORY "${FIXTURE}")
file(COPY "${SHIM}" "${HOST}" DESTINATION "${FIXTURE}")
file(TO_NATIVE_PATH "${SOURCE}" source_path)
file(TO_NATIVE_PATH "${FIXTURE}/game.exe" output_path)
execute_process(COMMAND "${CSC}" /nologo /platform:x86 /target:exe "${DEFINE}"
  "/out:${output_path}" "${source_path}" RESULT_VARIABLE compiled)
if(NOT "${compiled}" STREQUAL "0")
  message(FATAL_ERROR "Managed fixture compilation failed: ${compiled}")
endif()
file(WRITE "${FIXTURE}/AppxManifest.xml"
  "<Package><Identity Name=\"ManagedHostTest\" Publisher=\"CN=Test\" Version=\"1.0.0.0\" ProcessorArchitecture=\"x86\"/><Applications><Application Id=\"Test\" Executable=\"game.exe\"/></Applications></Package>")
execute_process(COMMAND "${FIXTURE}/appx-host.exe" fixture-argument
  WORKING_DIRECTORY "${FIXTURE}" RESULT_VARIABLE result TIMEOUT 15)
if(NOT "${result}" STREQUAL "0")
  file(READ "${FIXTURE}/managed.log" log)
  message(FATAL_ERROR "Managed host failed (${result}): ${log}")
endif()
