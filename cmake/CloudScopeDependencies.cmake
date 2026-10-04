# Finds the third-party libraries and gives each one the same target name on every platform.
#
# Providers: vcpkg (vcpkg.json) on Windows, distribution packages (tools/build/install-deps-debian.sh) on Linux.
# Minimum versions are the ones Debian 13 / Raspberry Pi OS ships, so code must build against those.
#
#   Qt6::Core                    Qt >= 6.8 (ADR-001)
#   opencv_core, opencv_imgproc, opencv_imgcodecs
#   spdlog::spdlog, fmt::fmt
#   tomlplusplus::tomlplusplus
#   nlohmann_json::nlohmann_json
#   tl::expected                 std::expected for C++20
#   CloudScope::cfitsio
#   CloudScope::turbojpeg
#   Catch2::Catch2, Qt6::Test    only when CLOUDSCOPE_BUILD_TESTS is ON

find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
find_package(OpenCV 4.10 REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(spdlog 1.15 CONFIG REQUIRED)
find_package(fmt 10.1 CONFIG REQUIRED)
find_package(tomlplusplus 3.4 CONFIG REQUIRED)
find_package(nlohmann_json 3.11 CONFIG REQUIRED)
# Debian's tl-expected 1.1.0 calls itself 1.0.0 in its CMake package file, hence the lower number here.
find_package(tl-expected 1.0 CONFIG REQUIRED)

# CFITSIO: CMake package from upstream (vcpkg), pkg-config file on Debian.
if(NOT TARGET CloudScope::cfitsio)
  add_library(cloudscope_dep_cfitsio INTERFACE)
  add_library(CloudScope::cfitsio ALIAS cloudscope_dep_cfitsio)
  find_package(cfitsio CONFIG QUIET)
  if(TARGET CFITSIO::cfitsio)
    target_link_libraries(cloudscope_dep_cfitsio INTERFACE CFITSIO::cfitsio)
  elseif(TARGET cfitsio)
    target_link_libraries(cloudscope_dep_cfitsio INTERFACE cfitsio)
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CFITSIO REQUIRED IMPORTED_TARGET cfitsio>=4.0)
    target_link_libraries(cloudscope_dep_cfitsio INTERFACE PkgConfig::CFITSIO)
  endif()
endif()

# TurboJPEG API of libjpeg-turbo (shared library preferred, static as fallback).
if(NOT TARGET CloudScope::turbojpeg)
  find_package(libjpeg-turbo 2.1 CONFIG REQUIRED)
  add_library(cloudscope_dep_turbojpeg INTERFACE)
  add_library(CloudScope::turbojpeg ALIAS cloudscope_dep_turbojpeg)
  if(TARGET libjpeg-turbo::turbojpeg)
    target_link_libraries(cloudscope_dep_turbojpeg INTERFACE libjpeg-turbo::turbojpeg)
  else()
    target_link_libraries(cloudscope_dep_turbojpeg INTERFACE libjpeg-turbo::turbojpeg-static)
  endif()
endif()

if(CLOUDSCOPE_BUILD_TESTS)
  find_package(Catch2 3.7 CONFIG REQUIRED)
  find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
endif()

message(STATUS "CloudScope dependencies: Qt ${Qt6_VERSION}, OpenCV ${OpenCV_VERSION}, spdlog ${spdlog_VERSION}, "
               "fmt ${fmt_VERSION}, toml++ ${tomlplusplus_VERSION}, nlohmann-json ${nlohmann_json_VERSION}, "
               "libjpeg-turbo ${libjpeg-turbo_VERSION}, tl-expected ${tl-expected_VERSION}")
