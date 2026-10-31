# A synthetic version resource tests SDK detection without shipping Xbox binaries.
file(MAKE_DIRECTORY "${FIXTURE}")
if(NOT DEFINED SDK_VERSION)
  set(SDK_VERSION "1.1.0.0")
endif()
file(COPY "${TEST_EXE}" "${SHIM}" DESTINATION "${FIXTURE}")
file(WRITE "${FIXTURE}/version.cs"
  "[assembly: System.Reflection.AssemblyFileVersion(\"${SDK_VERSION}\")] public class XboxVersionFixture {}\n")
file(TO_NATIVE_PATH "${FIXTURE}/version.cs" source_path)
file(TO_NATIVE_PATH "${FIXTURE}/Microsoft.Xbox.dll" output_path)
execute_process(COMMAND "${CSC}" /nologo /target:library "/out:${output_path}" "${source_path}"
  RESULT_VARIABLE compiled)
if(NOT "${compiled}" STREQUAL "0")
  message(FATAL_ERROR "Xbox version fixture compilation failed: ${compiled}")
endif()
get_filename_component(test_name "${TEST_EXE}" NAME)
execute_process(COMMAND "${FIXTURE}/${test_name}" ${TEST_ARGS}
  WORKING_DIRECTORY "${FIXTURE}" RESULT_VARIABLE result TIMEOUT 45)
if(NOT "${result}" STREQUAL "0")
  message(FATAL_ERROR "Xbox SDK ${SDK_VERSION} stand-ins failed: ${result}")
endif()
