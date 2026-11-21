# File: tests/compile_fail/run_compile.cmake
# Purpose: Compiles one case. EXPECT=ok must compile cleanly. EXPECT=fail adds the mistake, and must
#          fail with a diagnostic matching REGEX, so that a failure for another reason is caught.
separate_arguments(flag_list UNIX_COMMAND "${FLAGS}")
set(defines "")
if(EXPECT STREQUAL "fail")
  set(defines -DJSTR_BREAK)
endif()
execute_process(
  COMMAND ${CXX} ${flag_list} -I${INCLUDE} ${defines} ${SOURCE}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE output)
if(EXPECT STREQUAL "ok")
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "must compile, but failed:\n${output}")
  endif()
else()
  if(result EQUAL 0)
    message(FATAL_ERROR "must not compile, but it did")
  endif()
  if(NOT output MATCHES "${REGEX}")
    message(FATAL_ERROR "failed for another reason (wanted: ${REGEX}):\n${output}")
  endif()
endif()
