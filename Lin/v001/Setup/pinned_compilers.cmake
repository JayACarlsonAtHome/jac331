# File: Setup/pinned_compilers.cmake
# Purpose: The toolchain file every preset names. It builds with this machine's pinned compilers, so a preset run by hand
#          builds with the same GCC or Clang as bsir.sh, jCppGUI, jac313, Qlite, jText and jText_GUI on this machine.
#
# The pin is one row per machine (uname -n) in <base>/Compiler_Pins/compilers.pin.jtext, beside the repository and
# outside it, or in the file $COMPILER_PINS names. bsir.sh writes this machine's row when there is none, from this
# OS's compilers in Setup/recipes.conf. A row already there is kept, whoever wrote it. This file only reads it.
#
# The preset names the family in JSTR_COMPILER: gcc or clang. A compiler already set, by -DCMAKE_CXX_COMPILER or by an
# earlier configure of the same build folder, is not changed here, because CMake would then empty the cache and lose the
# build type. CMakeLists.txt refuses one that is not the pin, and names the fix.

# CMake's own try_compile projects come here too, with the compilers already set and no JSTR_COMPILER.
get_property(JSTR_IN_TRY_COMPILE GLOBAL PROPERTY IN_TRY_COMPILE)
if(JSTR_IN_TRY_COMPILE)
  return()
endif()
if(NOT JSTR_COMPILER MATCHES "^(gcc|clang)$")
  message(FATAL_ERROR "Name the compiler family: -DJSTR_COMPILER=gcc or clang. The presets do this. Got: '${JSTR_COMPILER}'.")
endif()

if(DEFINED ENV{COMPILER_PINS} AND NOT "$ENV{COMPILER_PINS}" STREQUAL "")
  set(JSTR_PIN_FILE "$ENV{COMPILER_PINS}")
else()
  # This file is <base>/jString/Lin/v001/Setup/pinned_compilers.cmake.
  cmake_path(SET JSTR_PIN_FILE NORMALIZE "${CMAKE_CURRENT_LIST_DIR}/../../../../Compiler_Pins/compilers.pin.jtext")
endif()
cmake_host_system_information(RESULT JSTR_PIN_HOST QUERY HOSTNAME)

set(JSTR_PIN_FIX "Run bash bsir.sh from Lin/v001, which pins this machine's compilers, then configure again.")
if(NOT EXISTS "${JSTR_PIN_FILE}")
  message(FATAL_ERROR "There is no compiler pin file at ${JSTR_PIN_FILE}. ${JSTR_PIN_FIX}")
endif()

# A data row: "1. #|# id|host|gcc_version|gcc_path|clang_version|clang_path|pinned". Values never contain '|'.
file(STRINGS "${JSTR_PIN_FILE}" JSTR_PIN_ROWS REGEX "^ *1\\. #\\|# ")
set(JSTR_PIN_ROW "")
foreach(row IN LISTS JSTR_PIN_ROWS)
  string(REGEX REPLACE "^ *1\\. #\\|# " "" row "${row}")
  string(REPLACE "|" ";" fields "${row}")
  list(LENGTH fields count)
  if(NOT count EQUAL 7)
    message(FATAL_ERROR "${JSTR_PIN_FILE}: a pin row has 7 fields (id|host|gcc_version|gcc_path|clang_version|clang_path|pinned), "
      "and this one has ${count}: ${row}")
  endif()
  list(GET fields 1 host)
  if(host STREQUAL JSTR_PIN_HOST)
    set(JSTR_PIN_ROW "${fields}")
  endif()
endforeach()
if(JSTR_PIN_ROW STREQUAL "")
  message(FATAL_ERROR "${JSTR_PIN_FILE} pins no compilers for this machine, ${JSTR_PIN_HOST}. ${JSTR_PIN_FIX}")
endif()

list(GET JSTR_PIN_ROW 3 JSTR_PINNED_GCC)
list(GET JSTR_PIN_ROW 5 JSTR_PINNED_CLANG)
if(JSTR_COMPILER STREQUAL "gcc")
  set(JSTR_PINNED_CXX "${JSTR_PINNED_GCC}")
  set(JSTR_C_NAME_FROM "g\\+\\+")
  set(JSTR_C_NAME_TO "gcc")
else()
  set(JSTR_PINNED_CXX "${JSTR_PINNED_CLANG}")
  set(JSTR_C_NAME_FROM "clang\\+\\+")
  set(JSTR_C_NAME_TO "clang")
endif()
# Each C compiler is its C++ compiler's name with g++ or clang++ made gcc or clang, in the same folder: g++-15 is
# beside gcc-15, and a GCC Toolset's g++ is beside its gcc.
cmake_path(GET JSTR_PINNED_CXX PARENT_PATH folder)
cmake_path(GET JSTR_PINNED_CXX FILENAME name)
string(REGEX REPLACE "${JSTR_C_NAME_FROM}" "${JSTR_C_NAME_TO}" name "${name}")
set(JSTR_PINNED_C "${folder}/${name}")
foreach(compiler IN ITEMS "${JSTR_PINNED_CXX}" "${JSTR_PINNED_C}")
  if(NOT EXISTS "${compiler}")
    message(FATAL_ERROR "This machine's pin in ${JSTR_PIN_FILE} names ${compiler}, which is not there. Install it, "
      "or pin again with jac313_test_cli pin --repin, or remove this machine's row and run bash bsir.sh.")
  endif()
endforeach()

if(NOT DEFINED CMAKE_CXX_COMPILER OR CMAKE_CXX_COMPILER STREQUAL "")
  set(CMAKE_C_COMPILER "${JSTR_PINNED_C}")
  set(CMAKE_CXX_COMPILER "${JSTR_PINNED_CXX}")
endif()
# CMakeLists.txt checks that the build folder builds with the pinned one.
set(JSTR_PIN_FILE "${JSTR_PIN_FILE}" CACHE INTERNAL "The compiler pin file")
set(JSTR_PIN_HOST "${JSTR_PIN_HOST}" CACHE INTERNAL "This machine's name in the pin file")
set(JSTR_PINNED_CXX "${JSTR_PINNED_CXX}" CACHE INTERNAL "This machine's pinned C++ compiler")

# Red Hat's clang is set to build with one fixed GCC Toolset's C++ library. It is given the pinned GCC's instead, so the
# two compilers share one library. Elsewhere clang already finds the newest GCC installed, which is the pinned one.
if(JSTR_COMPILER STREQUAL "clang")
  cmake_host_system_information(RESULT JSTR_OS_ID QUERY DISTRIB_ID)
  cmake_host_system_information(RESULT JSTR_OS_ID_LIKE QUERY DISTRIB_ID_LIKE)
  if(NOT JSTR_OS_ID STREQUAL "fedora" AND "${JSTR_OS_ID} ${JSTR_OS_ID_LIKE}" MATCHES "rhel")
    execute_process(COMMAND "${JSTR_PINNED_GCC}" -print-libgcc-file-name
      OUTPUT_VARIABLE JSTR_LIBGCC OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE failed)
    if(failed)
      message(FATAL_ERROR "The pinned GCC, ${JSTR_PINNED_GCC}, could not name its library folder.")
    endif()
    cmake_path(GET JSTR_LIBGCC PARENT_PATH JSTR_GCC_INSTALL_DIR)
    set(CMAKE_CXX_FLAGS_INIT "--gcc-install-dir=${JSTR_GCC_INSTALL_DIR}")
  endif()
endif()
