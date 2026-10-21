# Finds the third-party libraries and gives each one the same target name on every platform.
#
# Providers: vcpkg (vcpkg.json) on Windows, distribution packages (tools/build/install-deps-debian.sh) on Linux.
# Minimum versions are the ones Debian 13 / Raspberry Pi OS ships, so code must build against those.
#
#   Qt6::Core, Qt6::Sql          Qt >= 6.8 (ADR-001); Sql with the QSQLITE plugin for the frame catalogue (P030)
#   Qt6::Gui, Qt6::Widgets, Qt6::OpenGL, Qt6::OpenGLWidgets   the desktop application (Stage D, ADR-002)
#   CloudScope::ads              Qt Advanced Docking System (ADR-002), built from source: Debian 13 packages it
#                                for Qt 5 only. Shared library (LGPL-2.1), one version on every platform.
#   CloudScope::sdl2             SDL2 for gamepad input (P043)
#   opencv_core, opencv_imgproc, opencv_imgcodecs, opencv_calib3d
#   spdlog::spdlog, fmt::fmt
#   tomlplusplus::tomlplusplus
#   nlohmann_json::nlohmann_json
#   tl::expected                 std::expected for C++20
#   CloudScope::cfitsio
#   CloudScope::turbojpeg
#   Catch2::Catch2, Qt6::Test    only when CLOUDSCOPE_BUILD_TESTS is ON

find_package(Threads REQUIRED)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Sql Gui Widgets OpenGL OpenGLWidgets)
find_package(OpenCV 4.10 REQUIRED COMPONENTS core imgproc imgcodecs calib3d)
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

# SDL2: CMake package on both providers (SDL2::SDL2 is the shared library).
if(NOT TARGET CloudScope::sdl2)
  find_package(SDL2 2.26 CONFIG REQUIRED)
  add_library(cloudscope_dep_sdl2 INTERFACE)
  add_library(CloudScope::sdl2 ALIAS cloudscope_dep_sdl2)
  if(TARGET SDL2::SDL2)
    target_link_libraries(cloudscope_dep_sdl2 INTERFACE SDL2::SDL2)
  else()
    target_link_libraries(cloudscope_dep_sdl2 INTERFACE SDL2::SDL2-static)
  endif()
  # CloudScope has its own main(); SDL must not rename it.
  target_compile_definitions(cloudscope_dep_sdl2 INTERFACE SDL_MAIN_HANDLED)
endif()

# Qt Advanced Docking System, fetched at configure time (the release archive, verified by its SHA-256) and built
# as a shared library next to CloudScope's own targets. ADS_VERSION tells its build the version so that it does
# not ask git. To build offline, point FETCHCONTENT_SOURCE_DIR_QT_ADVANCED_DOCKING_SYSTEM at an unpacked copy.
if(NOT TARGET CloudScope::ads)
  include(FetchContent)
  set(ADS_VERSION "4.5.0" CACHE STRING "Version of the Qt Advanced Docking System sources" FORCE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "Qt Advanced Docking System examples" FORCE)
  set(BUILD_STATIC OFF CACHE BOOL "Qt Advanced Docking System as a static library (not allowed: LGPL)" FORCE)
  FetchContent_Declare(qt_advanced_docking_system
    URL      https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System/archive/refs/tags/4.5.0.tar.gz
    URL_HASH SHA256=a438810b9749684232fa29721857bb700167006f44407ab4dd51f5c90d171f09
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  # Its src/CMakeLists.txt does include(Versioning): the library's own module is not on the module path when
  # ADS_VERSION is given, and it would read the version from CloudScope's git tree anyway. cmake/Versioning.cmake
  # in this folder (earlier on CMAKE_MODULE_PATH) stands in for it.
  FetchContent_MakeAvailable(qt_advanced_docking_system)
  if(NOT TARGET qtadvanceddocking-qt6)
    message(FATAL_ERROR "The Qt Advanced Docking System build did not produce the target qtadvanceddocking-qt6.")
  endif()
  # Its headers are third-party headers: not subject to CloudScope's warning set. Its library goes where
  # CloudScope's own binaries go.
  get_target_property(_ads_includes qtadvanceddocking-qt6 INTERFACE_INCLUDE_DIRECTORIES)
  set_target_properties(qtadvanceddocking-qt6 PROPERTIES
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_ads_includes}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
  add_library(cloudscope_dep_ads INTERFACE)
  add_library(CloudScope::ads ALIAS cloudscope_dep_ads)
  target_link_libraries(cloudscope_dep_ads INTERFACE qtadvanceddocking-qt6)
endif()

if(CLOUDSCOPE_BUILD_TESTS)
  find_package(Catch2 3.7 CONFIG REQUIRED)
  find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
endif()

message(STATUS "CloudScope dependencies: Qt ${Qt6_VERSION}, OpenCV ${OpenCV_VERSION}, spdlog ${spdlog_VERSION}, "
               "fmt ${fmt_VERSION}, toml++ ${tomlplusplus_VERSION}, nlohmann-json ${nlohmann_json_VERSION}, "
               "libjpeg-turbo ${libjpeg-turbo_VERSION}, tl-expected ${tl-expected_VERSION}, SDL2 ${SDL2_VERSION}, "
               "Qt Advanced Docking System ${ADS_VERSION}")

# ONNX Runtime (ADR-004), fetched at configure time as the prebuilt release archives (verified by SHA-256):
#   Windows  the NuGet package Microsoft.ML.OnnxRuntime.DirectML (CPU and DirectML providers in one onnxruntime.dll)
#            and its dependency Microsoft.AI.DirectML (DirectML.dll, the Microsoft DirectML licence terms);
#   Linux    onnxruntime-linux-<arch> (CPU provider). The CUDA provider is a runtime add-on (ADR-004): a build
#            whose onnxruntime reports it gets an "onnx:inference:cuda" device, nothing else changes.
# To build offline, point FETCHCONTENT_SOURCE_DIR_ONNXRUNTIME (and _DIRECTML on Windows) at unpacked copies.
option(CLOUDSCOPE_ONNXRUNTIME "STRATIA inference with ONNX Runtime" ON)
if(CLOUDSCOPE_ONNXRUNTIME AND NOT TARGET CloudScope::onnxruntime)
  include(FetchContent)
  set(CLOUDSCOPE_ONNXRUNTIME_VERSION "1.24.4")
  set(CLOUDSCOPE_DIRECTML_VERSION "1.15.4")
  if(WIN32)
    FetchContent_Declare(onnxruntime
      URL      "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/${CLOUDSCOPE_ONNXRUNTIME_VERSION}"
      URL_HASH SHA256=57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681
      DOWNLOAD_NAME "Microsoft.ML.OnnxRuntime.DirectML.${CLOUDSCOPE_ONNXRUNTIME_VERSION}.zip"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_Declare(directml
      URL      "https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/${CLOUDSCOPE_DIRECTML_VERSION}"
      URL_HASH SHA256=4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9
      DOWNLOAD_NAME "Microsoft.AI.DirectML.${CLOUDSCOPE_DIRECTML_VERSION}.zip"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(onnxruntime directml)
    set(_ort_include "${onnxruntime_SOURCE_DIR}/build/native/include")
    set(_ort_native "${onnxruntime_SOURCE_DIR}/runtimes/win-x64/native")
    add_library(cloudscope_dep_onnxruntime_lib SHARED IMPORTED GLOBAL)
    set_target_properties(cloudscope_dep_onnxruntime_lib PROPERTIES
      IMPORTED_LOCATION "${_ort_native}/onnxruntime.dll"
      IMPORTED_IMPLIB "${_ort_native}/onnxruntime.lib"
      INTERFACE_INCLUDE_DIRECTORIES "${_ort_include}"
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_ort_include}")
    # Loaded by onnxruntime.dll at run time, not linked: copied next to the executables with the other DLLs.
    set_property(GLOBAL APPEND PROPERTY CLOUDSCOPE_EXTRA_RUNTIME_FILES
      "${_ort_native}/onnxruntime_providers_shared.dll"
      "${directml_SOURCE_DIR}/bin/x64-win/DirectML.dll")
    set(_ort_definitions CLOUDSCOPE_HAS_ONNXRUNTIME=1 CLOUDSCOPE_HAS_DIRECTML=1)
  else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
      set(_ort_arch "aarch64")
      set(_ort_hash "866109a9248d057671a039b9d725be4bd86888e3754140e6701ec621be9d4d7e")
    else()
      set(_ort_arch "x64")
      set(_ort_hash "3a211fbea252c1e66290658f1b735b772056149f28321e71c308942cdb54b747")
    endif()
    FetchContent_Declare(onnxruntime
      URL      "https://github.com/microsoft/onnxruntime/releases/download/v${CLOUDSCOPE_ONNXRUNTIME_VERSION}/onnxruntime-linux-${_ort_arch}-${CLOUDSCOPE_ONNXRUNTIME_VERSION}.tgz"
      URL_HASH SHA256=${_ort_hash}
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(onnxruntime)
    add_library(cloudscope_dep_onnxruntime_lib SHARED IMPORTED GLOBAL)
    set_target_properties(cloudscope_dep_onnxruntime_lib PROPERTIES
      IMPORTED_LOCATION "${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so.${CLOUDSCOPE_ONNXRUNTIME_VERSION}"
      IMPORTED_SONAME "libonnxruntime.so.1"
      INTERFACE_INCLUDE_DIRECTORIES "${onnxruntime_SOURCE_DIR}/include"
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${onnxruntime_SOURCE_DIR}/include")
    set(_ort_definitions CLOUDSCOPE_HAS_ONNXRUNTIME=1 CLOUDSCOPE_HAS_DIRECTML=0)
  endif()
  add_library(cloudscope_dep_onnxruntime INTERFACE)
  add_library(CloudScope::onnxruntime ALIAS cloudscope_dep_onnxruntime)
  target_link_libraries(cloudscope_dep_onnxruntime INTERFACE cloudscope_dep_onnxruntime_lib)
  target_compile_definitions(cloudscope_dep_onnxruntime INTERFACE ${_ort_definitions})
elseif(NOT TARGET CloudScope::onnxruntime)
  add_library(cloudscope_dep_onnxruntime INTERFACE)
  add_library(CloudScope::onnxruntime ALIAS cloudscope_dep_onnxruntime)
  target_compile_definitions(cloudscope_dep_onnxruntime INTERFACE CLOUDSCOPE_HAS_ONNXRUNTIME=0 CLOUDSCOPE_HAS_DIRECTML=0)
endif()
