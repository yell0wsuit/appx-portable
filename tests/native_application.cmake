file(MAKE_DIRECTORY "${FIXTURE}")
file(COPY "${HOST}" "${SHIM}" DESTINATION "${FIXTURE}")
file(COPY_FILE "${TEST_DLL}" "${FIXTURE}/NativeApplication.dll")
file(WRITE "${FIXTURE}/AppxManifest.xml"
  "<Package><Identity Name=\"NativeApplication.Test\" Publisher=\"CN=Test\" Version=\"1.0.0.0\" ProcessorArchitecture=\"${ARCH}\"/><Applications><Application Id=\"Test\" Executable=\"NativeApplication.exe\"/></Applications></Package>")
execute_process(COMMAND "${FIXTURE}/appx-native-host.exe" --log
  WORKING_DIRECTORY "${FIXTURE}" RESULT_VARIABLE result TIMEOUT 20)
if(NOT "${result}" STREQUAL "0")
  message(FATAL_ERROR ".NET Native host contract failed: ${result}")
endif()
