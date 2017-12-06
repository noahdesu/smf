include(ExternalProject)
ExternalProject_Add(zstd_ext
  SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/third_party/zstd/build/cmake
  CMAKE_ARGS -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
             -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
             -DCMAKE_C_FLAGS=${ZSTD_C_FLAGS}
             -DCMAKE_AR=${CMAKE_AR}
  BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/libzstd
  BUILD_COMMAND $(MAKE) libzstd_static
  INSTALL_COMMAND "true")

# force zstd make to be called on each time
ExternalProject_Add_Step(zstd_ext forcebuild
  DEPENDEES configure
  DEPENDERS build
  COMMAND "true"
  ALWAYS 1)

add_library(zstd STATIC IMPORTED)
set_property(TARGET zstd PROPERTY
  IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/libzstd/lib/libzstd.a")
add_dependencies(zstd zstd_ext)
set(ZSTD_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/src/third_party/zstd/lib)

# TODO: smf_seastar_target pollutes CFLAGS. this is primarily a function of the
# Seastar pkg-config not being correctly setup, forcing some work arounds in
# cmake. ideally we could include xxhash.c in smf_utils sources, but we'll need
# to fully separate cflags from cxxflags in the future for this work well.
add_library(xxhash STATIC
  ${PROJECT_SOURCE_DIR}/src/third_party/xxHash/xxhash.c)
target_include_directories(xxhash PUBLIC
  ${PROJECT_SOURCE_DIR}/src/third_party/xxHash)

add_library(smf_utils STATIC
  random.cc
  checks/cpu.cc
  checks/memory.cc
  checks/disk.cc
  human_bytes_printing_utils.cc
  compression.cc)

target_link_libraries (smf_utils xxhash zstd lz4 Seastar::seastar)
target_include_directories(smf_utils PRIVATE ${ZSTD_INCLUDE_DIR})
install(TARGETS smf_utils DESTINATION lib)

install(FILES
  caching/clock_pro/chunk.h
  caching/clock_pro/clock_list.h
  caching/clock_pro/clock_pro.h
  DESTINATION include/smf/utils/caching/clock_pro
  )
install(FILES
  checks/cpu.h
  checks/disk.h
  checks/memory.h
  DESTINATION include/smf/utils/checks
  )
install(FILES
  compression.h
  human_bytes_printing_utils.h
  random.h
  time_utils.h
  DESTINATION include/smf/utils
  )
