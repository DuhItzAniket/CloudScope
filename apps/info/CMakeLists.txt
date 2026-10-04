# cloudscope-info: prints build and library information and runs the installation self-test.

add_executable(cloudscope_info main.cpp)
set_target_properties(cloudscope_info PROPERTIES OUTPUT_NAME cloudscope-info)
target_link_libraries(cloudscope_info PRIVATE CloudScope::core nlohmann_json::nlohmann_json)
cloudscope_target_defaults(cloudscope_info)
cloudscope_copy_runtime_dlls(cloudscope_info)
