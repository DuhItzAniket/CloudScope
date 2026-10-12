# cloudscope-desktop: the CloudScope Desktop workbench (Stage D, ADR-002): Qt Widgets with the Qt Advanced
# Docking System. Everything except main() is in a static library so that the user-interface tests link it.

add_library(cloudscope_desktop_lib STATIC
  src/app_context.cpp
  src/app_context.hpp
  src/camera_panel.cpp
  src/camera_panel.hpp
  src/camera_profiles.cpp
  src/camera_profiles.hpp
  src/camera_service.cpp
  src/camera_service.hpp
  src/capture_panel.cpp
  src/capture_panel.hpp
  src/console_panel.cpp
  src/console_panel.hpp
  src/display_stretch.cpp
  src/display_stretch.hpp
  src/gamepad.cpp
  src/gamepad.hpp
  src/histogram_panel.cpp
  src/histogram_panel.hpp
  src/image_tools.cpp
  src/image_tools.hpp
  src/image_tools_panel.cpp
  src/image_tools_panel.hpp
  src/live_view.cpp
  src/live_view.hpp
  src/main_window.cpp
  src/main_window.hpp
  src/overlay.cpp
  src/overlay.hpp
  src/preferences_dialog.cpp
  src/preferences_dialog.hpp
  src/preview_pipeline.cpp
  src/preview_pipeline.hpp
  src/session_browser.cpp
  src/session_browser.hpp
  src/shortcuts.cpp
  src/shortcuts.hpp
  src/status_bar.cpp
  src/status_bar.hpp
  src/theme.cpp
  src/theme.hpp
  src/viewport.cpp
  src/viewport.hpp)
add_library(CloudScope::desktop ALIAS cloudscope_desktop_lib)
set_target_properties(cloudscope_desktop_lib PROPERTIES AUTOMOC ON OUTPUT_NAME cloudscope-desktop)
target_include_directories(cloudscope_desktop_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(cloudscope_desktop_lib
  PUBLIC
    CloudScope::core
    CloudScope::ads
    Qt6::Widgets Qt6::OpenGLWidgets
    opencv_core
  PRIVATE
    CloudScope::sdl2
    Qt6::Sql
    fmt::fmt
    opencv_imgproc opencv_imgcodecs)
cloudscope_target_defaults(cloudscope_desktop_lib)

add_executable(cloudscope_desktop main.cpp)
set_target_properties(cloudscope_desktop PROPERTIES OUTPUT_NAME cloudscope-desktop)
target_link_libraries(cloudscope_desktop PRIVATE CloudScope::desktop nlohmann_json::nlohmann_json)
cloudscope_target_defaults(cloudscope_desktop)
cloudscope_copy_runtime_dlls(cloudscope_desktop GUI)
