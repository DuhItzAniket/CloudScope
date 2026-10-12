# Stand-in for the "Versioning" module of the Qt Advanced Docking System (found first because this folder is
# earlier on CMAKE_MODULE_PATH than the library's own cmake/modules).
#
# The library's src/CMakeLists.txt does include(Versioning) and calls add_windows_version_resources(<target>).
# Its own module reads the version from `git describe` in CMAKE_SOURCE_DIR, which is CloudScope's tree, not the
# library's, and on Windows writes a resource file with an empty version that the resource compiler refuses. This
# version uses ADS_VERSION (set in CloudScopeDependencies.cmake) and attaches a correct version resource.

function(add_windows_version_resources target)
  if(NOT WIN32)
    return()
  endif()
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "add_windows_version_resources: target '${target}' not found.")
  endif()
  if(NOT ADS_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR "add_windows_version_resources: ADS_VERSION must be major.minor.patch, got '${ADS_VERSION}'")
  endif()
  set(_major "${CMAKE_MATCH_1}")
  set(_minor "${CMAKE_MATCH_2}")
  set(_patch "${CMAKE_MATCH_3}")
  set(_rc "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc")
  file(WRITE "${_rc}" "#include <windows.h>

VS_VERSION_INFO VERSIONINFO
 FILEVERSION     ${_major},${_minor},${_patch},0
 PRODUCTVERSION  ${_major},${_minor},${_patch},0
 FILEFLAGSMASK   VS_FFI_FILEFLAGSMASK
 FILEFLAGS       0
 FILEOS          VOS__WINDOWS32
 FILETYPE        VFT_DLL
 FILESUBTYPE     0
BEGIN
    BLOCK \"StringFileInfo\"
    BEGIN
        BLOCK \"040904b0\"
        BEGIN
            VALUE \"FileDescription\", \"Qt Advanced Docking System\"
            VALUE \"FileVersion\", \"${ADS_VERSION}\"
            VALUE \"ProductName\", \"Qt Advanced Docking System\"
            VALUE \"ProductVersion\", \"${ADS_VERSION}\"
            VALUE \"LegalCopyright\", \"LGPL-2.1-or-later\"
        END
    END
    BLOCK \"VarFileInfo\"
    BEGIN
        VALUE \"Translation\", 0x409, 1200
    END
END
")
  target_sources("${target}" PRIVATE "${_rc}")
endfunction()
