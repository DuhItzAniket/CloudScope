# cloudscope-bench: measures the frame pipeline (copy into pooled buffers, hand-over to consumers).

add_executable(cloudscope_bench main.cpp)
set_target_properties(cloudscope_bench PROPERTIES OUTPUT_NAME cloudscope-bench)
target_link_libraries(cloudscope_bench PRIVATE CloudScope::core nlohmann_json::nlohmann_json fmt::fmt)
cloudscope_target_defaults(cloudscope_bench)
cloudscope_copy_runtime_dlls(cloudscope_bench)
