# Tests. Every Catch2 test case becomes one CTest entry (discovered when the test executable is built).
#
#   unit.*   functions of the core library, in-process
#   cli.*    the real executables, started as child processes

include(Catch)

# Shared by all test executables: main() with a QCoreApplication, helpers (fixtures, temporary folders,
# waiting on the Qt event loop), and mock devices for code that uses the HAL.
add_library(cloudscope_test_support STATIC
  support/mock_devices.cpp
  support/mock_devices.hpp
  support/qt_catch_main.cpp
  support/test_support.cpp
  support/test_support.hpp)
target_include_directories(cloudscope_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_compile_definitions(cloudscope_test_support PRIVATE
  "CLOUDSCOPE_TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"")
target_link_libraries(cloudscope_test_support PUBLIC Catch2::Catch2 Qt6::Core Qt6::Test CloudScope::core)
cloudscope_target_defaults(cloudscope_test_support)

# The rules of the HAL interfaces as test functions. Every implementation of an interface runs them:
# mocks and simulators here, real drivers in the hardware-in-the-loop tests.
add_library(cloudscope_hal_contract STATIC
  contract/hal_contract.cpp
  contract/hal_contract.hpp)
target_include_directories(cloudscope_hal_contract PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/contract")
target_link_libraries(cloudscope_hal_contract PUBLIC Catch2::Catch2 CloudScope::core)
cloudscope_target_defaults(cloudscope_hal_contract)

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
    unit/test_acquisition.cpp
    unit/test_build_info.cpp
    unit/test_clock.cpp
    unit/test_config.cpp
    unit/test_decode_statistics.cpp
    unit/test_error_units.cpp
    unit/test_exposure_calibration.cpp
    unit/test_executor.cpp
    unit/test_frame.cpp
    unit/test_hal_mocks.cpp
    unit/test_hal_registry.cpp
    unit/test_intrinsics.cpp
    unit/test_json_schema.cpp
    unit/test_log.cpp
    unit/test_motion_profile.cpp
    unit/test_recording.cpp
    unit/test_rotation.cpp
    unit/test_self_test.cpp
    unit/test_sequencer.cpp
    unit/test_session_catalogue.cpp
    unit/test_signal.cpp
    unit/test_sim_camera.cpp
    unit/test_sim_driver.cpp
    unit/test_sim_rig.cpp
    unit/test_sim_sensors.cpp
    unit/test_solar.cpp
    unit/test_spsc_queue.cpp
    unit/test_synthetic_sky.cpp
    unit/test_test_support.cpp
    unit/test_uvc.cpp
    unit/test_video_files.cpp
  LIBRARIES
    cloudscope_hal_contract
    nlohmann_json::nlohmann_json
    CloudScope::cfitsio
    opencv_imgproc
    opencv_imgcodecs
    opencv_calib3d)

cloudscope_add_tests(cloudscope_cli_tests
  PREFIX "cli."
  SOURCES
    integration/test_cli_info.cpp
  LIBRARIES
    nlohmann_json::nlohmann_json)
add_dependencies(cloudscope_cli_tests cloudscope_info)
target_compile_definitions(cloudscope_cli_tests PRIVATE
  "CLOUDSCOPE_INFO_EXE=\"$<TARGET_FILE:cloudscope_info>\"")

# Benchmarks with a pass mark, run in optimised builds only: a Debug build says nothing about speed.
#   bench.4K frames ...   plan P016: 4K frames copied at more than 60 fps without a dropped frame
add_test(NAME "bench.4K frames are copied and handed over at more than 60 fps without loss"
  COMMAND cloudscope_bench --width 3840 --height 2160 --frames 240 --min-fps 60
  CONFIGURATIONS Release RelWithDebInfo)
set_tests_properties("bench.4K frames are copied and handed over at more than 60 fps without loss"
  PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
