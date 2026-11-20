# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FILESYSTEM_VERSION LIMIT_COUNT 1)
project(FilesystemXFS VERSION ${FILESYSTEM_VERSION} LANGUAGES C)
include(CTest)
include(FetchContent)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(INFILTRATR_COMMON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INFILTRATR_COMMON_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(infiltratr_common
 GIT_REPOSITORY https://github.com/Infiltrator-Projects/Libraries.git
 GIT_TAG d23a52693fd3212368d327e1c1302e07ae9510ec)
FetchContent_MakeAvailable(infiltratr_common)
set_property(DIRECTORY "${infiltratr_common_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version.h" "#define LD_VERSION \"${PROJECT_VERSION}\"\n")

# Native endian/CRC32C engine and Common V2 probe are the entire production
# provider. Dead code is discarded; the public ELF exports only V2.
add_library(xfs-driver SHARED
 native/xfs_common.c module/xfs_v2_probe.c)
set_target_properties(xfs-driver PROPERTIES
 PREFIX "" OUTPUT_NAME "XFS" SUFFIX ".so" C_VISIBILITY_PRESET hidden)
target_compile_definitions(xfs-driver PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64)
target_compile_options(xfs-driver PRIVATE
 -Wall -Wextra -Wpedantic -Werror -ffunction-sections -fdata-sections)
target_link_options(xfs-driver PRIVATE
 -Wl,-z,defs -Wl,--gc-sections
 "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/module/v2-only.map")
target_include_directories(xfs-driver PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/native"
 "${CMAKE_CURRENT_SOURCE_DIR}/compat"
 "${infiltratr_common_SOURCE_DIR}/include")
target_link_libraries(xfs-driver PRIVATE InfiltratrCommon::Common)

if(BUILD_TESTING)
 # Preserve original native mutation/metadata and fixture tests. Do not
 # package or load this historical test library in production.
 add_library(xfs-historical-test-support SHARED
   native/xfs_catalog.c native/xfs_common.c native/xfs_plan.c
   native/xfs_metadata.c native/xfs_worker.c
   compat/legacy_services.c compat/runtime_alloc.c
   fixtures/xfs_codec.c module/xfs_fixture_bridge.c)
 set_source_files_properties(native/xfs_worker.c PROPERTIES
   COMPILE_DEFINITIONS "main=ifs_xfs_worker_main")
 set_source_files_properties(fixtures/xfs_codec.c PROPERTIES
   COMPILE_DEFINITIONS "ldtm_raw_xfs=ifs_xfs_raw_fixture")
 target_compile_definitions(xfs-historical-test-support PRIVATE
   _GNU_SOURCE _FILE_OFFSET_BITS=64)
 target_compile_options(xfs-historical-test-support PRIVATE
   -Wall -Wextra -Wpedantic -Werror)
 target_include_directories(xfs-historical-test-support PRIVATE
   "${CMAKE_CURRENT_SOURCE_DIR}/native"
   "${CMAKE_CURRENT_SOURCE_DIR}/compat"
   "${CMAKE_CURRENT_SOURCE_DIR}/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/include"
   "${CMAKE_CURRENT_BINARY_DIR}"
   "${infiltratr_common_SOURCE_DIR}/include")
 target_link_libraries(xfs-historical-test-support PRIVATE
   InfiltratrCommon::Common OpenSSL::Crypto SQLite::SQLite3 Threads::Threads)

 add_executable(xfs-module-test tests/test_module.c)
 target_include_directories(xfs-module-test PRIVATE
   "${infiltratr_common_SOURCE_DIR}/include")
 target_link_libraries(xfs-module-test PRIVATE xfs-driver InfiltratrCommon::Common)
 add_test(NAME xfs-module-contract COMMAND xfs-module-test)

 # The independently generated CRC32C fixture uses the historical test
 # library's canonical checksum helper, NOT a new implementation inside Boot.
 add_executable(xfs-v2-probe-test tests/test_xfs_v2_probe.c)
 target_include_directories(xfs-v2-probe-test PRIVATE
   "${infiltratr_common_SOURCE_DIR}/include")
 target_link_libraries(xfs-v2-probe-test PRIVATE
   xfs-driver xfs-historical-test-support InfiltratrCommon::Common)
 target_compile_options(xfs-v2-probe-test PRIVATE
   -Wall -Wextra -Wpedantic -Werror)
 add_test(NAME xfs-common-v2-probe COMMAND xfs-v2-probe-test)
 add_test(NAME xfs-v2-only-dynamic-export
   COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_v2_exports.sh"
     "$<TARGET_FILE:xfs-driver>")

 add_library(xfs-test-host STATIC tests/test_host_services.c)
 target_include_directories(xfs-test-host PRIVATE
   "${CMAKE_CURRENT_SOURCE_DIR}/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/compat")
 target_compile_definitions(xfs-test-host PRIVATE
   _GNU_SOURCE _FILE_OFFSET_BITS=64)
 target_compile_options(xfs-test-host PRIVATE
   -Wall -Wextra -Wpedantic -Werror)
 target_link_libraries(xfs-test-host PUBLIC xfs-historical-test-support)

 add_executable(xfs-native-test tests/test_xfs_native.c)
 target_include_directories(xfs-native-test PRIVATE
   "${CMAKE_CURRENT_SOURCE_DIR}/native"
   "${CMAKE_CURRENT_SOURCE_DIR}/compat")
 target_compile_definitions(xfs-native-test PRIVATE
   _GNU_SOURCE _FILE_OFFSET_BITS=64)
 target_compile_options(xfs-native-test PRIVATE
   -Wall -Wextra -Wpedantic -Werror)
 target_link_libraries(xfs-native-test PRIVATE
   xfs-test-host InfiltratrCommon::Common SQLite::SQLite3
   OpenSSL::Crypto Threads::Threads)
 add_test(NAME xfs-native COMMAND xfs-native-test)

 add_executable(xfs-metadata-test tests/test_xfs_metadata_whitebox.c)
 target_include_directories(xfs-metadata-test PRIVATE
   "${CMAKE_CURRENT_SOURCE_DIR}/native"
   "${CMAKE_CURRENT_SOURCE_DIR}/compat")
 target_compile_definitions(xfs-metadata-test PRIVATE
   _GNU_SOURCE _FILE_OFFSET_BITS=64)
 target_compile_options(xfs-metadata-test PRIVATE
   -Wall -Wextra -Wpedantic -Werror)
 target_link_libraries(xfs-metadata-test PRIVATE
   xfs-test-host InfiltratrCommon::Common SQLite::SQLite3
   OpenSSL::Crypto Threads::Threads)
 add_test(NAME xfs-metadata COMMAND xfs-metadata-test)
endif()
install(TARGETS xfs-driver LIBRARY DESTINATION /System/Filesystems)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/XFS.pdf"
  DESTINATION /System/Resources/Documentation
  RENAME Filesystem-XFS.pdf)
