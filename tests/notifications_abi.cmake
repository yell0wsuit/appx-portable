file(MAKE_DIRECTORY "${FIXTURE}")
file(COPY_FILE "${TEST_EXE}" "${FIXTURE}/notificationsabitest.exe" ONLY_IF_DIFFERENT)
file(COPY_FILE "${SHIM}" "${FIXTURE}/appxportable.dll" ONLY_IF_DIFFERENT)
if(MODE STREQUAL "modern")
  # A synthetic metadata marker selects the changed vtable without game files.
  file(WRITE "${FIXTURE}/NotificationsExtensions.winmd" "get_ContentId")
endif()
execute_process(COMMAND "${FIXTURE}/notificationsabitest.exe" "${MODE}"
  WORKING_DIRECTORY "${FIXTURE}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Notifications ${MODE} failed (${result}): ${output}${error}")
endif()
message(STATUS "${output}")
