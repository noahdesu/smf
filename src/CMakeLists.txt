add_subdirectory(third_party/HdrHistogram_c EXCLUDE_FROM_ALL)
add_subdirectory(third_party/flatbuffers EXCLUDE_FROM_ALL)

set(ZSTD_BUILD_SHARED ON CACHE BOOL
  "Enable building zstd static library")
set(ZSTD_BUILD_SHARED OFF CACHE BOOL
  "Disable building zstd shared library")
add_subdirectory(third_party/zstd/build/cmake EXCLUDE_FROM_ALL)
include_directories(third_party/zstd/lib)

set(SEASTAR_WITH_TESTS OFF CACHE BOOL
  "Disable building Seastar tests and apps")
add_subdirectory(third_party/seastar)

add_subdirectory(googletest/googlemock)

include_directories(third_party/flatbuffers/include)

# so fastrange/fastrange.h resolves correctly. fastrange is so tiny that it
# seems like the perfect candidate for just checking into the tree.
include_directories(third_party)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# TODO: replace with cmake compiler flag test tools
# TODO: https://github.com/arsenm/sanitizers-cmake
# TODO: check cmake default flags (debug: O0, O2, -g)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=auto -Wall -Werror -Wextra -Wformat")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wmissing-braces -Wparentheses -Wpointer-arith")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wformat-security -Wunused -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wcast-align -Wno-missing-field-initializers")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wdelete-non-virtual-dtor -Wno-ignored-qualifiers")

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 6.1)
    add_definitions(-DSMF_GCC_CONCEPTS=1)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fconcepts")
    if(CMAKE_BUILD_TYPE MATCHES Debug)
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lasan -lubsan")
    endif()
  endif()
endif()

add_subdirectory(flatbuffers)
add_subdirectory(utils)
add_subdirectory(histogram)
add_subdirectory(rpc)
add_subdirectory(demo_apps)
add_subdirectory(smfb)
add_subdirectory(filesystem)
add_subdirectory(chain_replication)
add_subdirectory(platform)

if(ENABLE_INTEGRATION_TESTS OR ENABLE_UNIT_TESTS)
  include(${PROJECT_SOURCE_DIR}/CMake/tests.cmake)
endif()

if(ENABLE_INTEGRATION_TESTS)
  add_subdirectory(integration_tests)
endif(ENABLE_INTEGRATION_TESTS)

if(ENABLE_UNIT_TESTS)
  add_subdirectory(test)
endif(ENABLE_UNIT_TESTS)
