file(REMOVE_RECURSE "${FIXTURE}")
file(MAKE_DIRECTORY "${FIXTURE}")
file(COPY "${HOST}" "${SHIM}" DESTINATION "${FIXTURE}")
file(COPY_FILE "${TEST_EXE}" "${FIXTURE}/NativeLaunch.exe")
file(COPY_FILE "${PLUGIN}" "${FIXTURE}/Plugin.dll")
file(WRITE "${FIXTURE}/AppxManifest.xml"
  "<Package><Identity Name=\"NativeLaunch.Test\" Publisher=\"CN=Test\" Version=\"1.0.0.0\" ProcessorArchitecture=\"${ARCH}\"/><Applications><Application Id=\"Test\" Executable=\"NativeLaunch.exe\"/></Applications></Package>")
execute_process(COMMAND "${FIXTURE}/appx-native-host.exe" --log --fixture-argument
  WORKING_DIRECTORY "${FIXTURE}" RESULT_VARIABLE result TIMEOUT 20)
if(NOT "${result}" STREQUAL "0")
  message(FATAL_ERROR "native executable launch failed: ${result}")
endif()
# The game continues the host's log rather than starting a new one.
file(READ "${FIXTURE}/appx-portable.log" log)
if(NOT log MATCHES "exe=[^\n]*appx-native-host.exe" OR NOT log MATCHES "exe=[^\n]*NativeLaunch.exe")
  message(FATAL_ERROR "host and game do not share one log:\n${log}")
endif()
