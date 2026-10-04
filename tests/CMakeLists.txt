# Tests. Every Catch2 test case becomes one CTest entry (discovered when the test executable is built).
#
#   unit.*   functions of the core library, in-process
#   cli.*    the real executables, started as child processes

include(Catch)

# main() with a QCoreApplication, shared by all test executables.
add_library(cloudscope_test_main STATIC support/qt_catch_main.cpp)
target_link_libraries(cloudscope_test_main PUBLIC Catch2::Catch2 Qt6::Core)
cloudscope_target_defaults(cloudscope_test_main)

#   cloudscope_add_tests(<target> PREFIX <ctest prefix> SOURCES <files>... [LIBRARIES <targets>...])
function(cloudscope_add_tests target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "PREFIX" "SOURCES;LIBRARIES")
  add_executable(${target} ${arg_SOURCES})
  string(REPLACE "_" "-" _output_name "${target}")
  set_target_properties(${target} PROPERTIES OUTPUT_NAME "${_output_name}")
  target_link_libraries(${target} PRIVATE cloudscope_test_main CloudScope::core ${arg_LIBRARIES})
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
  LIBRARIES
    nlohmann_json::nlohmann_json)

cloudscope_add_tests(cloudscope_cli_tests
  PREFIX "cli."
  SOURCES
    integration/test_cli_info.cpp
  LIBRARIES
    nlohmann_json::nlohmann_json)
add_dependencies(cloudscope_cli_tests cloudscope_info)
target_compile_definitions(cloudscope_cli_tests PRIVATE
  "CLOUDSCOPE_INFO_EXE=\"$<TARGET_FILE:cloudscope_info>\"")
