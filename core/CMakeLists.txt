# libcloudscope-core: all domain logic, no UI and no network server code (ADR-003).

set(_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(src/common/build_config.hpp.in "${_generated_dir}/build_config.hpp" @ONLY)

# The git revision is refreshed on every build, not only at configure time.
set(_git_revision_cpp "${_generated_dir}/git_revision.cpp")
add_custom_target(cloudscope_git_revision
  COMMAND "${CMAKE_COMMAND}"
          "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
          "-DOUTPUT=${_git_revision_cpp}"
          -P "${PROJECT_SOURCE_DIR}/cmake/GitRevision.cmake"
  BYPRODUCTS "${_git_revision_cpp}"
  COMMENT "Reading git revision"
  VERBATIM)

add_library(cloudscope_core STATIC
  include/cloudscope/capture/frame.hpp
  include/cloudscope/capture/frame_hub.hpp
  include/cloudscope/common/app_config.hpp
  include/cloudscope/common/build_info.hpp
  include/cloudscope/common/clock.hpp
  include/cloudscope/common/config.hpp
  include/cloudscope/common/error.hpp
  include/cloudscope/common/event_bus.hpp
  include/cloudscope/common/executor.hpp
  include/cloudscope/common/json_schema.hpp
  include/cloudscope/common/log.hpp
  include/cloudscope/common/scope_exit.hpp
  include/cloudscope/common/self_test.hpp
  include/cloudscope/common/signal.hpp
  include/cloudscope/common/spsc_queue.hpp
  include/cloudscope/common/units.hpp
  include/cloudscope/hal/camera.hpp
  include/cloudscope/hal/device.hpp
  include/cloudscope/hal/inference.hpp
  include/cloudscope/hal/mount.hpp
  include/cloudscope/hal/registry.hpp
  include/cloudscope/hal/sensors.hpp
  include/cloudscope/hal/transport.hpp
  include/cloudscope/sim/synthetic_sky.hpp
  src/capture/frame.cpp
  src/capture/frame_hub.cpp
  src/common/app_config.cpp
  src/common/build_info.cpp
  src/common/clock.cpp
  src/common/config.cpp
  src/common/error.cpp
  src/common/executor.cpp
  src/common/json_schema.cpp
  src/common/log.cpp
  src/common/self_test.cpp
  src/common/signal.cpp
  src/common/units.cpp
  src/hal/hal.cpp
  src/sim/synthetic_sky.cpp
  "${_git_revision_cpp}")
add_library(CloudScope::core ALIAS cloudscope_core)
add_dependencies(cloudscope_core cloudscope_git_revision)
set_target_properties(cloudscope_core PROPERTIES OUTPUT_NAME cloudscope-core)

# Files compiled into the library and read through Qt's resource system (":/cloudscope/<name>").
qt_add_resources(cloudscope_core "cloudscope_core_resources"
  PREFIX "/cloudscope"
  BASE "resources"
  FILES
    resources/config.defaults.toml
    resources/config.schema.json)

target_include_directories(cloudscope_core
  PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${_generated_dir}")

set_source_files_properties(src/common/build_info.cpp PROPERTIES
  COMPILE_DEFINITIONS "CLOUDSCOPE_BUILD_TYPE=\"$<CONFIG>\"")

# PUBLIC: libraries whose types appear in CloudScope's public headers.
target_link_libraries(cloudscope_core
  PUBLIC
    Qt6::Core
    tl::expected
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    fmt::fmt
    opencv_core
    Threads::Threads
  PRIVATE
    opencv_imgproc opencv_imgcodecs
    tomlplusplus::tomlplusplus
    CloudScope::cfitsio
    CloudScope::turbojpeg)

cloudscope_target_defaults(cloudscope_core)
