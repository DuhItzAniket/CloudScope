# cloudscope-aibench: the STRATIA inference benchmark per engine and execution provider (P070).
add_executable(cloudscope_aibench main.cpp)
set_target_properties(cloudscope_aibench PROPERTIES OUTPUT_NAME cloudscope-aibench)
target_link_libraries(cloudscope_aibench PRIVATE CloudScope::core nlohmann_json::nlohmann_json opencv_imgcodecs)
cloudscope_target_defaults(cloudscope_aibench)
cloudscope_copy_runtime_dlls(cloudscope_aibench)
