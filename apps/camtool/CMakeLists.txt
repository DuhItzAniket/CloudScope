# cloudscope-camtool: the camera subsystem from the command line (enumeration, modes, controls, streaming,
# exposure response, decode timing, soak test).

add_executable(cloudscope_camtool main.cpp)
set_target_properties(cloudscope_camtool PROPERTIES OUTPUT_NAME cloudscope-camtool)
target_link_libraries(cloudscope_camtool PRIVATE CloudScope::core fmt::fmt opencv_imgproc opencv_imgcodecs)
if(WIN32)
  target_link_libraries(cloudscope_camtool PRIVATE psapi)
endif()
cloudscope_target_defaults(cloudscope_camtool)
cloudscope_copy_runtime_dlls(cloudscope_camtool)
