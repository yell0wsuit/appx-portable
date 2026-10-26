# Runs storetest beside an appx-portable.ini. INI is the file's text, ENV a
# "|"-separated list of --unset=NAME then NAME=value items (add_test splits
# on ";"), ARGS storetest's arguments.
file(REMOVE_RECURSE "${FIXTURE}")
file(MAKE_DIRECTORY "${FIXTURE}")
file(COPY "${TEST}" "${SHIM}" DESTINATION "${FIXTURE}")
file(WRITE "${FIXTURE}/AppxManifest.xml" [=[<Package>
<Identity Name="StoreSettingsFixture" Version="1.0.0.0" ProcessorArchitecture="x86"/>
</Package>]=])
string(REPLACE "\\n" "\n" ini "${INI}")
file(WRITE "${FIXTURE}/appx-portable.ini" "${ini}\n")
string(REPLACE "|" ";" env "${ENV}")
separate_arguments(args NATIVE_COMMAND "${ARGS}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${env} "${FIXTURE}/storetest.exe" ${args}
    WORKING_DIRECTORY "${FIXTURE}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Store settings test failed: ${result}")
endif()
