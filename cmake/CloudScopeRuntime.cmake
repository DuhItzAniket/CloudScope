# Runtime helpers.
#
#   cloudscope_copy_runtime_dlls(<executable target>)
#
# Windows has no rpath: an executable only starts when the DLLs it needs are next to it or on PATH.
# vcpkg already copies its own DLLs; this copies the remaining ones (Qt) so that executables and tests
# run straight from the build folder. Does nothing on other systems.

function(cloudscope_copy_runtime_dlls target)
  if(NOT WIN32)
    return()
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:${target}>" "$<TARGET_FILE_DIR:${target}>"
    COMMAND_EXPAND_LISTS
    VERBATIM)
endfunction()
