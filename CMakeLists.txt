cmake_minimum_required(VERSION 3.20)
project(FilesystemXFS VERSION 1.0.0 LANGUAGES C)
include(CTest)
include(FetchContent)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(INFILTRATR_COMMON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INFILTRATR_COMMON_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(infiltratr_common
 GIT_REPOSITORY https://github.com/Infiltrator-Projects/Libraries.git
 GIT_TAG c76ec95ef66fc728df6a906ad75a6a4dd63b419b)
FetchContent_MakeAvailable(infiltratr_common)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version.h" "#define LD_VERSION \"1.0.0\"\n")
add_library(xfs-driver SHARED
 native/xfs_catalog.c native/xfs_common.c native/xfs_plan.c
 native/xfs_metadata.c native/xfs_worker.c
 compat/legacy_services.c compat/runtime_alloc.c module/xfs_module.c
 fixtures/xfs_codec.c module/xfs_fixture_bridge.c)
set_source_files_properties(native/xfs_worker.c PROPERTIES
 COMPILE_DEFINITIONS "main=ifs_xfs_worker_main")
set_source_files_properties(fixtures/xfs_codec.c PROPERTIES
 COMPILE_DEFINITIONS "ldtm_raw_xfs=ifs_xfs_raw_fixture")
set_target_properties(xfs-driver PROPERTIES PREFIX "" OUTPUT_NAME "XFS" SUFFIX ".so")
target_compile_definitions(xfs-driver PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64)
target_compile_options(xfs-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(xfs-driver PRIVATE -Wl,-z,defs)
target_include_directories(xfs-driver PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/native" "${CMAKE_CURRENT_SOURCE_DIR}/compat"
 "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/include" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(xfs-driver PRIVATE InfiltratrCommon::Common OpenSSL::Crypto SQLite::SQLite3 Threads::Threads)
if(BUILD_TESTING)
 add_executable(xfs-module-test tests/test_module.c)
 target_include_directories(xfs-module-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/compat")
 target_link_libraries(xfs-module-test PRIVATE xfs-driver)
 add_test(NAME xfs-module-contract COMMAND xfs-module-test)
 # The canonical filesystem repository, not Defragmenter, owns the native
 # parser/relayout regressions. Both tests link against this exact XFS.so.
 add_executable(xfs-native-test tests/test_xfs_native.c)
 target_include_directories(xfs-native-test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/native" "${CMAKE_CURRENT_SOURCE_DIR}/compat")
 target_compile_definitions(xfs-native-test PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64)
 target_compile_options(xfs-native-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
 target_link_libraries(xfs-native-test PRIVATE xfs-driver)
 add_test(NAME xfs-native COMMAND xfs-native-test)

 # This test includes the canonical metadata translation unit deliberately to
 # exercise static B+tree validation without adding test symbols to the ABI.
 add_executable(xfs-metadata-test tests/test_xfs_metadata_whitebox.c)
 target_include_directories(xfs-metadata-test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/native" "${CMAKE_CURRENT_SOURCE_DIR}/compat")
 target_compile_definitions(xfs-metadata-test PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64)
 target_compile_options(xfs-metadata-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
 target_link_libraries(xfs-metadata-test PRIVATE xfs-driver)
 add_test(NAME xfs-metadata COMMAND xfs-metadata-test)

endif()
install(TARGETS xfs-driver LIBRARY DESTINATION /System/Filesystems)
