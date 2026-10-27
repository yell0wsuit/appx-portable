file(MAKE_DIRECTORY "${FIXTURE}/package")
execute_process(COMMAND "${TEST}" --write-fixture "${FIXTURE}/Example.xbf" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Cannot write synthetic XBF fixture: ${result}")
endif()
file(COPY "${FIXTURE}/Example.pri.xml" DESTINATION "${FIXTURE}/package")
file(WRITE "${FIXTURE}/config.xml" [=[<?xml version="1.0" encoding="utf-8"?>
<resources targetOsVersion="10.0.0" majorVersion="1">
  <index root="\" startIndexAt="\">
    <default><qualifier name="Language" value="en-US"/></default>
    <indexer-config type="folder" foldernameAsQualifier="true" filenameAsQualifier="true" qualifierDelimiter="."/>
    <indexer-config type="priinfo" emitStrings="true" emitPaths="true" emitEmbeddedData="true"/>
  </index>
</resources>
]=])
execute_process(COMMAND "${MAKEPRI}" new /pr "${FIXTURE}/package" /cf "${FIXTURE}/config.xml"
  /in AppxPortable.XbfFixture /of "${FIXTURE}/package/resources.pri" /o
  RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE errors)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Cannot build synthetic PRI: ${result} ${errors}")
endif()
# The following probe must succeed after the loose file is gone, and after
# OpenPackageResource releases the native MRT objects that owned the mapping.
execute_process(COMMAND "${TEST}" "${FIXTURE}/package" Example.xbf
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Embedded XBF failed: ${result}\n${output}\n${errors}")
endif()
if(NOT output MATCHES "2 Button Toggle root=0 collapsed=1")
  message(FATAL_ERROR "Original embedded connection metadata was not decoded: ${output}")
endif()
