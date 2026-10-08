cmake_minimum_required(VERSION 3.25)

option(CLOUDSCOPE_BUILD_TESTS "Build the unit tests" ON)
option(CLOUDSCOPE_WARNINGS_AS_ERRORS "Treat compiler warnings in CloudScope code as errors" OFF)
option(CLOUDSCOPE_COVERAGE "Instrument CloudScope code to measure test coverage (GCC, Debug builds)" OFF)
set(CLOUDSCOPE_SANITIZE "" CACHE STRING
    "Sanitizers to build CloudScope code and tests with: address,undefined or thread (GCC or Clang)")
set(CLOUDSCOPE_EXPECT_ARCH "" CACHE STRING
    "Architecture the chosen preset is meant for (x86_64 or aarch64); configuring on another one is an error")

# vcpkg reads this before project(): Catch2 is only installed when tests are built.
if(CLOUDSCOPE_BUILD_TESTS)
  list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

project(CloudScope
  VERSION 0.2.0
  DESCRIPTION "Sky-observation system: camera application, pan-tilt control, remote access, STRATIA inference"
  HOMEPAGE_URL "https://github.com/DuhItzAniket/CloudScope"
  LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not supported. Use a preset: cmake --preset <name>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# All executables and DLLs of one configuration land in one folder: build/<preset>/bin/<Config>.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CloudScopeArchitecture)
include(CloudScopeCompilerOptions)
include(CloudScopeDependencies)
include(CloudScopeRuntime)

add_subdirectory(core)
add_subdirectory(apps/info)
add_subdirectory(apps/bench)
add_subdirectory(apps/camtool)

if(CLOUDSCOPE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
