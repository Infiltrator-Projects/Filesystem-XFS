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

# Compile the same on-disk code in the hosted provider and the import-free
# firmware ELF. The packaged carrier embeds the exact output of the child build.
set(XFS_FIRMWARE_ELF "${CMAKE_CURRENT_BINARY_DIR}/firmware/XFS.so")
add_custom_command(OUTPUT "${XFS_FIRMWARE_ELF}"
 COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_SOURCE_DIR}/module/firmware"
   -B "${CMAKE_CURRENT_BINARY_DIR}/firmware" -DCMAKE_BUILD_TYPE=Release
   "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}" "-DCOMMON_INCLUDE_DIR=${infiltratr_common_SOURCE_DIR}/include"
 COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_CURRENT_BINARY_DIR}/firmware"
 DEPENDS module/firmware/CMakeLists.txt module/firmware/runtime.c module/xfs_v2_probe.c
   native/xfs_disk.c native/xfs_disk.h native/xfs_reader.c native/xfs_reader.h
   "${infiltratr_common_SOURCE_DIR}/include/infiltratr/filesystem_module_v2.h"
   "${infiltratr_common_SOURCE_DIR}/include/infiltratr/filesystem_ro_sessions_v2.h"
 VERBATIM)
add_custom_target(xfs-firmware DEPENDS "${XFS_FIRMWARE_ELF}")
add_library(xfs-driver SHARED native/xfs_disk.c native/xfs_reader.c module/xfs_v2_probe.c)
add_dependencies(xfs-driver xfs-firmware)
set_property(TARGET xfs-driver APPEND PROPERTY LINK_DEPENDS "${XFS_FIRMWARE_ELF}")
add_custom_command(TARGET xfs-driver POST_BUILD
 COMMAND "${CMAKE_OBJCOPY}" --add-section ".infiltrator_boot=${XFS_FIRMWARE_ELF}"
   --set-section-flags .infiltrator_boot=readonly "$<TARGET_FILE:xfs-driver>" VERBATIM)
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
   native/xfs_catalog.c native/xfs_common.c native/xfs_disk.c native/xfs_plan.c
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
 add_executable(xfs-namespace-media tests/test_namespace_media.c)
 target_include_directories(xfs-namespace-media PRIVATE "${infiltratr_common_SOURCE_DIR}/include")
 target_compile_options(xfs-namespace-media PRIVATE -Wall -Wextra -Wpedantic -Werror)
 target_link_libraries(xfs-namespace-media PRIVATE ${CMAKE_DL_LIBS})
 add_test(NAME xfs-real-namespace-media
   COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/namespace-media.sh"
     "$<TARGET_FILE:xfs-namespace-media>" "$<TARGET_FILE:xfs-driver>" "${XFS_FIRMWARE_ELF}")
 add_executable(xfs-superblock-media tests/test_superblock_media.c)
 target_include_directories(xfs-superblock-media PRIVATE "${infiltratr_common_SOURCE_DIR}/include")
 target_compile_options(xfs-superblock-media PRIVATE -Wall -Wextra -Wpedantic -Werror)
 target_link_libraries(xfs-superblock-media PRIVATE ${CMAKE_DL_LIBS})
 add_test(NAME xfs-real-superblock-geometry
   COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/superblock-media.sh"
     "$<TARGET_FILE:xfs-superblock-media>" "$<TARGET_FILE:xfs-driver>")
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

 # The installer exercises only the production descriptor, in-memory probe
 # and V2 export contract; historical relocation and real-media regression
 # tests remain in the unrestricted CI suite. The probe executes production
 # code so the instrumented native build collects genuine PGO samples.
 set_tests_properties(xfs-module-contract xfs-common-v2-probe
   xfs-v2-only-dynamic-export PROPERTIES LABELS "install-smoke" TIMEOUT 30)

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
