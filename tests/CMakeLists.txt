# Tests. Every Catch2 test case becomes one CTest entry (discovered when the test executable is built).
#
#   unit.*   functions of the core library, in-process
#   cli.*    the real executables, started as child processes

include(Catch)

# Shared by all test executables: main() with a QCoreApplication, and helpers (fixtures, temporary folders,
# waiting on the Qt event loop).
add_library(cloudscope_test_support STATIC
  support/qt_catch_main.cpp
  support/test_support.cpp
  support/test_support.hpp)
target_include_directories(cloudscope_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_compile_definitions(cloudscope_test_support PRIVATE
  "CLOUDSCOPE_TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"")
target_link_libraries(cloudscope_test_support PUBLIC Catch2::Catch2 Qt6::Core Qt6::Test)
cloudscope_target_defaults(cloudscope_test_support)

#   cloudscope_add_tests(<target> PREFIX <ctest prefix> SOURCES <files>... [LIBRARIES <targets>...])
function(cloudscope_add_tests target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "PREFIX" "SOURCES;LIBRARIES")
  add_executable(${target} ${arg_SOURCES})
  string(REPLACE "_" "-" _output_name "${target}")
  set_target_properties(${target} PROPERTIES OUTPUT_NAME "${_output_name}")
  target_link_libraries(${target} PRIVATE cloudscope_test_support CloudScope::core ${arg_LIBRARIES})
  cloudscope_target_defaults(${target})
  cloudscope_copy_runtime_dlls(${target})
  catch_discover_tests(${target} TEST_PREFIX "${arg_PREFIX}" PROPERTIES TIMEOUT 60)
endfunction()

cloudscope_add_tests(cloudscope_unit_tests
  PREFIX "unit."
  SOURCES
    unit/test_build_info.cpp
    unit/test_clock.cpp
    unit/test_config.cpp
    unit/test_error_units.cpp
    unit/test_json_schema.cpp
    unit/test_log.cpp
    unit/test_self_test.cpp
    unit/test_synthetic_sky.cpp
    unit/test_test_support.cpp
  LIBRARIES
    nlohmann_json::nlohmann_json
    opencv_imgproc
    opencv_imgcodecs)

cloudscope_add_tests(cloudscope_cli_tests
  PREFIX "cli."
  SOURCES
    integration/test_cli_info.cpp
  LIBRARIES
    nlohmann_json::nlohmann_json)
add_dependencies(cloudscope_cli_tests cloudscope_info)
target_compile_definitions(cloudscope_cli_tests PRIVATE
  "CLOUDSCOPE_INFO_EXE=\"$<TARGET_FILE:cloudscope_info>\"")
