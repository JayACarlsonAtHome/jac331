# File: CMakeLists.txt
# Purpose: Builds the jString v001 tests. jString itself is header-only (include/jString/v001). Everything is found
#          relative to this file. The pin check, the hardening and the warning flags are jCppGUI's, so code that uses
#          jString builds under the same rules it was tested under.
cmake_minimum_required(VERSION 3.26)
project(jString_v001 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Every build uses this machine's pinned compilers, which Setup/pinned_compilers.cmake, the presets' toolchain file, reads.
# A build folder keeps the compiler it was first configured with, so one that no longer matches the pin is refused here.
if(NOT DEFINED JSTR_PINNED_CXX)
  message(FATAL_ERROR "Configure with a preset, such as cmake --preset gcc-debug, so that the build uses this machine's pinned "
    "compilers. Without one: -DCMAKE_TOOLCHAIN_FILE=Setup/pinned_compilers.cmake -DJSTR_COMPILER=gcc (or clang).")
endif()
file(REAL_PATH "${CMAKE_CXX_COMPILER}" JSTR_CXX_REAL)
file(REAL_PATH "${JSTR_PINNED_CXX}" JSTR_PINNED_CXX_REAL)
if(NOT JSTR_CXX_REAL STREQUAL JSTR_PINNED_CXX_REAL)
  message(FATAL_ERROR "This build folder builds with ${CMAKE_CXX_COMPILER}, and this machine (${JSTR_PIN_HOST}) is pinned to "
    "${JSTR_PINNED_CXX} in ${JSTR_PIN_FILE}. Remove ${CMAKE_BINARY_DIR} and configure again, without naming a compiler.")
endif()
# The oldest compilers that build this C++23 code, the same floors as Setup/recipes.conf. A pin written by another
# project could name an older one, which would fail far into the build with hundreds of errors.
set(JSTR_MIN_GNU 14)
set(JSTR_MIN_Clang 19)
if(DEFINED JSTR_MIN_${CMAKE_CXX_COMPILER_ID} AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS JSTR_MIN_${CMAKE_CXX_COMPILER_ID})
  message(FATAL_ERROR "This machine's pinned ${CMAKE_CXX_COMPILER} is ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}, and "
    "this code needs ${CMAKE_CXX_COMPILER_ID} ${JSTR_MIN_${CMAKE_CXX_COMPILER_ID}} or later. Pin a newer one in ${JSTR_PIN_FILE}.")
endif()
# Choosing no build type would quietly choose no optimization and none of the hardening below that needs it, so a
# build must name one. CMake's own defaults for the optimized types are not used: they add -DNDEBUG, and -O3 at
# Release. Nothing here depends on assert, but -O2 is the level these flags were tried at.
set(JSTR_BUILD_TYPES Debug Release RelWithDebInfo MinSizeRel)
if(NOT CMAKE_BUILD_TYPE IN_LIST JSTR_BUILD_TYPES)
  message(FATAL_ERROR "Name a build type, one of: ${JSTR_BUILD_TYPES}. Got: '${CMAKE_BUILD_TYPE}'. The presets do this.")
endif()
foreach(language C CXX)
  set(CMAKE_${language}_FLAGS_RELEASE "-O2")
  set(CMAKE_${language}_FLAGS_RELWITHDEBINFO "-O2 -g")
  set(CMAKE_${language}_FLAGS_MINSIZEREL "-Os")
endforeach()

option(JSTR_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(JSTR_WARNINGS_ONLY "Optimize, without the hardening flags that stop warnings firing. Never ship this." OFF)

# --- Hardening: applies to everything, in every configuration, generated code included ---
add_library(jstr_hardening INTERFACE)
target_compile_definitions(jstr_hardening INTERFACE _GLIBCXX_ASSERTIONS)
target_compile_options(jstr_hardening INTERFACE
  -fstack-protector-strong -fstack-clash-protection -fcf-protection -fPIE
  -ftrivial-auto-var-init=zero -fstrict-flex-arrays=3
  $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3>)
# These three change what the language promises, which is hardening. They also stop g++'s warnings about the
# same promises: -Wnull-dereference, -Wstrict-aliasing and -Wstrict-overflow. The gcc-warn preset leaves them
# out so that those warnings can report. A build that is shipped always has them.
if(NOT JSTR_WARNINGS_ONLY)
  target_compile_options(jstr_hardening INTERFACE
    -fno-delete-null-pointer-checks
    $<$<NOT:$<CONFIG:Debug>>:-fno-strict-overflow -fno-strict-aliasing>)
endif()
target_link_options(jstr_hardening INTERFACE -pie -Wl,-z,relro,-z,now -Wl,-z,noexecstack)
if(JSTR_SANITIZE)
  target_compile_options(jstr_hardening INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_link_options(jstr_hardening INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=all)
endif()

# --- Warnings: our code only. A warning is a failure. ---
set(JSTR_WARNING_FLAGS
  -Werror -pedantic-errors
  -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
  -Wcast-qual -Wcast-align -Wformat=2 -Wnull-dereference -Wdouble-promotion -Wfloat-equal -Wfloat-conversion
  -Wundef -Wredundant-decls -Wextra-semi -Wimplicit-fallthrough -Wswitch-enum -Wswitch-default
  -Wzero-as-null-pointer-constant -Wnon-virtual-dtor -Woverloaded-virtual -Wunused -Wmissing-declarations)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  list(APPEND JSTR_WARNING_FLAGS
    -Wuseless-cast -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Warith-conversion -Wstrict-overflow=2
    -Wctor-dtor-privacy -Wsign-promo -Wstrict-null-sentinel -Wnoexcept -Wcomma-subscript -Wvolatile
    -Walloc-zero -Walloca -Warray-bounds=2 -Wattribute-alias=2 -Wcast-align=strict -Wcatch-value=3
    -Wconditionally-supported -Wdate-time -Wdeprecated-copy-dtor -Wdisabled-optimization
    -Wformat-overflow=2 -Wformat-signedness -Wformat-truncation=2 -Wimplicit-fallthrough=5 -Winvalid-pch
    -Wmismatched-tags -Wmissing-include-dirs -Wmissing-requires -Wmissing-template-keyword -Wpacked
    -Wplacement-new=2 -Wpointer-arith -Wredundant-tags -Wshift-overflow=2 -Wstrict-aliasing=3
    -Wstringop-overflow=4 -Wsuggest-override -Wtrampolines -Wunused-const-variable=2 -Wunused-macros
    -Wvector-operation-performance -Wvirtual-inheritance -Wvla -Wwrite-strings
    -Weffc++ -Wnrvo -Wstack-protector -Wsuggest-attribute=const -Wsuggest-attribute=noreturn
    -Wsuggest-final-methods -Wsuggest-final-types -Wunsafe-loop-optimizations)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  list(APPEND JSTR_WARNING_FLAGS
    -Weverything
    # This code is C++23 only, so warnings about older standards contradict the language in use. This
    # one group also covers c++98-compat and the pre-c++14, 17, 20 and 23 compat families.
    -Wno-c++98-compat-pedantic
    # Warns about the normal, unavoidable alignment gaps in a struct. We do not lay fields out by hand.
    -Wno-padded
    # Contradicts -Wswitch-default, which the owner requires: a switch over an enum names every value,
    # checked by -Wswitch-enum, and also has a default, because a value from outside can be out of range.
    -Wno-covered-switch-default)
  # clang++ 23 brought the lifetime-safety analysis into -Weverything, and all of it is live but one group. Older clang++
  # does not know the group's name and fails on an unknown one, so it is left out only where it is known.
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-Wlifetime-safety-suggestions JSTR_CXX_HAS_LIFETIME_SAFETY)
  if(JSTR_CXX_HAS_LIFETIME_SAFETY)
    list(APPEND JSTR_WARNING_FLAGS
      # Asks for [[clang::lifetimebound]], which g++ rejects as an error under -Wattributes, and a macro to hide it is banned.
      -Wno-lifetime-safety-suggestions)
  endif()
endif()
add_library(jstr_warnings INTERFACE)
target_compile_options(jstr_warnings INTERFACE ${JSTR_WARNING_FLAGS})

# --- The library: header-only ---
add_library(jstring INTERFACE)
target_include_directories(jstring INTERFACE include)
target_compile_features(jstring INTERFACE cxx_std_23)

# --- Tests ---
enable_testing()

function(jstr_test name)
  add_executable(${name} tests/${name}.cpp)
  target_include_directories(${name} PRIVATE tests)
  target_link_libraries(${name} PRIVATE jstring jstr_hardening jstr_warnings)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

jstr_test(test_utf8)
jstr_test(test_string)
# Everything again inside static_assert: a constant evaluation stops at any undefined behaviour.
jstr_test(test_constexpr)
# The shape that set off g++ 15's -Wdangling-pointer in jCppGUI (an Error holding text, inside a std::expected,
# moved into a member). It must build clean under gcc-warn with every g++ the guests pin.
jstr_test(test_error_in_expected)

add_test(NAME source_rules COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_source_rules.sh)

# --- Compile-fail tests: each misuse must fail with its diagnostic, and the same source must compile when the one
#     mistake is left out, so a failure for an unrelated reason is caught. ---
list(JOIN JSTR_WARNING_FLAGS " " JSTR_WARNING_STRING)
function(jstr_compile_fail name regex)
  set(script ${CMAKE_CURRENT_SOURCE_DIR}/tests/compile_fail/run_compile.cmake)
  set(common
    -DCXX=${CMAKE_CXX_COMPILER}
    "-DFLAGS=-std=c++23 ${JSTR_WARNING_STRING} -fsyntax-only"
    -DINCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/include
    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/compile_fail/${name}.cpp)
  add_test(NAME compile_ok_${name} COMMAND ${CMAKE_COMMAND} ${common} -DEXPECT=ok -P ${script})
  add_test(NAME compile_fail_${name} COMMAND ${CMAKE_COMMAND} ${common} -DEXPECT=fail "-DREGEX=${regex}" -P ${script})
  set_tests_properties(compile_ok_${name} compile_fail_${name} PROPERTIES TIMEOUT 120)
endfunction()

set(JSTR_NO_CONVERSION "no matching|no viable|could not convert|cannot convert|conversion from|no known conversion|cannot initialize|not implicitly convertible")
set(JSTR_NOT_CONSTANT "consteval|immediate|constant expression|not a constant|non-constant")
jstr_compile_fail(from_raw_pointer "${JSTR_NO_CONVERSION}")
jstr_compile_fail(from_std_string "${JSTR_NO_CONVERSION}")
jstr_compile_fail(view_to_string_implicitly "${JSTR_NO_CONVERSION}")
jstr_compile_fail(invalid_literal "${JSTR_NOT_CONSTANT}|not_valid")
jstr_compile_fail(invalid_literal_suffix "${JSTR_NOT_CONSTANT}|not_valid")
jstr_compile_fail(runtime_array_as_literal "${JSTR_NOT_CONSTANT}")
jstr_compile_fail(byte_index "no match|no viable|subscript|invalid types")
jstr_compile_fail(ignored_from "nodiscard|unused-result|ignoring return value|discarded")
jstr_compile_fail(raw_units_implicitly "${JSTR_NO_CONVERSION}")
jstr_compile_fail(plain_number_index "${JSTR_NO_CONVERSION}|explicit")
jstr_compile_fail(compare_with_number "no match|no viable|invalid operands|constraints? not satisfied|not satisfied")
jstr_compile_fail(swapped_start_and_count "${JSTR_NO_CONVERSION}")
jstr_compile_fail(substring_plain_numbers "${JSTR_NO_CONVERSION}|explicit")
jstr_compile_fail(substring_of_temporary "deleted")
jstr_compile_fail(index_base_from_int "${JSTR_NO_CONVERSION}")
jstr_compile_fail(index_base_to_int "${JSTR_NO_CONVERSION}")
jstr_compile_fail(index_base_compared_with_int "no match|no viable|invalid operands")
jstr_compile_fail(position_base_as_number "${JSTR_NO_CONVERSION}|template argument|narrowing|could not match|invalid")

# --- Benchmark: jString against std::string. Build it with gcc-release or clang-release to read its numbers; every
#     preset builds it and runs it once with --quick, which only proves it works. ---
add_executable(bench_string bench/bench_string.cpp)
target_link_libraries(bench_string PRIVATE jstring jstr_hardening jstr_warnings)
add_test(NAME bench_string_quick COMMAND bench_string --quick)
set_tests_properties(bench_string_quick PROPERTIES TIMEOUT 120)
