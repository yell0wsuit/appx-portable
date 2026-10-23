# Run the same notification contract under managed-package detection.
file(MAKE_DIRECTORY "${FIXTURE}")
file(COPY "${TOAST}" "${SHIM}" "${HOST}" DESTINATION "${FIXTURE}")
file(WRITE "${FIXTURE}/AppxManifest.xml"
  "<Package><Identity Name=\"IslandsToastTest\" Publisher=\"CN=Test\" Version=\"1.0.0.0\" ProcessorArchitecture=\"x86\"/><Applications><Application Id=\"Test\" Executable=\"appx-host.exe\"/></Applications></Package>")
get_filename_component(test_name "${TOAST}" NAME)
execute_process(COMMAND "${FIXTURE}/${test_name}" --islands
  WORKING_DIRECTORY "${FIXTURE}" RESULT_VARIABLE result TIMEOUT 20)
if(NOT "${result}" STREQUAL "0")
  message(FATAL_ERROR "Islands notification test failed: ${result}")
endif()
