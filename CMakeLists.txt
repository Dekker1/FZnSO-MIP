cmake_minimum_required(VERSION 3.20)
project(fznso-mip CXX C)

# The FZnSO headers live in the enclosing checkout; nothing is vendored, because
# this solver ships beside them.
get_filename_component(FZNSO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# HiGHS, linked in rather than dlopened. An installed one is used if there is
# one; otherwise it is fetched and built here, so a checkout needs nothing
# beyond a compiler and a network.
set(HIGHS_VERSION "1.15.1" CACHE STRING "HiGHS version to fetch when none is installed")
find_package(HIGHS ${HIGHS_VERSION} QUIET)
if(NOT HIGHS_FOUND)
  include(FetchContent)
  set(BUILD_SHARED_LIBS OFF)
  set(BUILD_TESTING OFF)
  set(BUILD_EXAMPLES OFF)
  set(ZLIB OFF)
  FetchContent_Declare(highs
    GIT_REPOSITORY https://github.com/ERGO-Code/HiGHS.git
    GIT_TAG "v${HIGHS_VERSION}"
    GIT_SHALLOW ON)
  FetchContent_MakeAvailable(highs)
endif()

# The solver-independent half: reading a model and turning it into columns and
# rows. Every backend links this.
add_library(fznso-mip-core OBJECT
  src/mip_backend.cpp
  src/mip_model.cpp
  src/mip_solver.cpp
  src/signatures.cpp
)
target_include_directories(fznso-mip-core PUBLIC
  "${FZNSO_ROOT}/c" "${FZNSO_ROOT}/cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(fznso-mip-core PRIVATE -Wall -Wextra)

# One shared library per backend. The file's base name is the FZnSO solver name,
# which prefixes the thirteen entry points, so `OUTPUT_NAME` is not cosmetic;
# `lib/fznso/` keeps it apart from the library it wraps.
function(fznso_mip_backend name)
  add_library(fznso-${name} SHARED ${ARGN} $<TARGET_OBJECTS:fznso-mip-core>)
  target_link_libraries(fznso-${name} PRIVATE fznso-mip-core highs::highs)
  target_compile_options(fznso-${name} PRIVATE -Wall -Wextra)
  set_target_properties(fznso-${name} PROPERTIES
    OUTPUT_NAME ${name}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib/fznso")
endfunction()

fznso_mip_backend(highs src/highs/highs_backend.cpp)
# `highs_c_api.h` declares a `static` function it does not define.
set_source_files_properties(src/highs/highs_backend.cpp
  PROPERTIES COMPILE_OPTIONS -Wno-unused-function)

# The self-test loads a built library the way a consumer does, so it exercises
# the entry-point naming and the export path as well as the behaviour.
enable_testing()
add_executable(fznso-mip-test src/test/fznso_mip_test.cpp)
target_include_directories(fznso-mip-test PRIVATE "${FZNSO_ROOT}/c" "${FZNSO_ROOT}/cpp")
target_compile_options(fznso-mip-test PRIVATE -Wall -Wextra)
if(NOT WIN32)
  target_link_libraries(fznso-mip-test PRIVATE ${CMAKE_DL_LIBS})
endif()
add_dependencies(fznso-mip-test fznso-highs)
add_test(NAME fznso-highs COMMAND fznso-mip-test $<TARGET_FILE:fznso-highs>)
