# Run the ABI probe with managed-package detection enabled. The CLR host is
# only the manifest's managed executable marker; this test never launches it.
file(MAKE_DIRECTORY "${FIXTURE}")
file(COPY "${TEST}" "${SHIM}" "${HOST}" DESTINATION "${FIXTURE}")
file(WRITE "${FIXTURE}/AppxManifest.xml"
  "<Package><Identity Name=\"WebViewContract\" Publisher=\"CN=Test\" Version=\"1.0.0.0\" ProcessorArchitecture=\"x86\"/><Applications><Application Id=\"Test\" Executable=\"appx-host.exe\"/></Applications></Package>")
execute_process(COMMAND "${FIXTURE}/webviewtest.exe" WORKING_DIRECTORY "${FIXTURE}" RESULT_VARIABLE result TIMEOUT 30)
if(NOT "${result}" STREQUAL "0")
  message(FATAL_ERROR "WebView composition contract failed (${result})")
endif()
