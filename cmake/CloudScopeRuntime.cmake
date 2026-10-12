# Runtime helpers.
#
#   cloudscope_copy_runtime_dlls(<executable target> [GUI])
#
# Windows has no rpath: an executable only starts when the DLLs it needs are next to it or on PATH.
# vcpkg already copies its own DLLs; this copies the remaining ones (Qt) so that executables and tests
# run straight from the build folder. GUI also copies the Qt platform plugins (windows, offscreen) and the
# Windows style, which Qt looks for next to the executable once Qt6Core.dll lives there. Does nothing on
# other systems.

function(cloudscope_copy_runtime_dlls target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "GUI" "" "")
  if(NOT WIN32)
    return()
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:${target}>" "$<TARGET_FILE_DIR:${target}>"
    COMMAND_EXPAND_LISTS
    VERBATIM)
  # Qt loads its SQL driver as a plugin from <application dir>/sqldrivers; the frame catalogue needs QSQLITE.
  if(TARGET Qt6::QSQLiteDriverPlugin)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/sqldrivers"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "$<TARGET_FILE:Qt6::QSQLiteDriverPlugin>" "$<TARGET_FILE_DIR:${target}>/sqldrivers/"
      VERBATIM)
  endif()
  if(arg_GUI)
    foreach(_plugin IN ITEMS
        "platforms;Qt6::QWindowsIntegrationPlugin"
        "platforms;Qt6::QOffscreenIntegrationPlugin"
        "styles;Qt6::QModernWindowsStylePlugin"
        "styles;Qt6::QWindowsVistaStylePlugin")
      list(GET _plugin 0 _folder)
      list(GET _plugin 1 _plugin_target)
      if(TARGET ${_plugin_target})
        add_custom_command(TARGET ${target} POST_BUILD
          COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/${_folder}"
          COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                  "$<TARGET_FILE:${_plugin_target}>" "$<TARGET_FILE_DIR:${target}>/${_folder}/"
          VERBATIM)
      endif()
    endforeach()
  endif()
endfunction()
