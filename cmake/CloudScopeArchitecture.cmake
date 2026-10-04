# Normalises the target architecture name and checks it against the expectation of the preset.
#
# Sets CLOUDSCOPE_ARCH to "x86_64" or "aarch64".

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _cs_processor)
if(_cs_processor MATCHES "^(x86_64|amd64|x64)$")
  set(CLOUDSCOPE_ARCH "x86_64")
elseif(_cs_processor MATCHES "^(aarch64|arm64)$")
  set(CLOUDSCOPE_ARCH "aarch64")
else()
  message(FATAL_ERROR "Unsupported processor '${CMAKE_SYSTEM_PROCESSOR}'. CloudScope supports x86_64 and aarch64.")
endif()
unset(_cs_processor)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "CloudScope needs a 64-bit target (16 MP frames and large capture files).")
endif()

if(CLOUDSCOPE_EXPECT_ARCH AND NOT CLOUDSCOPE_EXPECT_ARCH STREQUAL CLOUDSCOPE_ARCH)
  message(FATAL_ERROR
    "This preset is for ${CLOUDSCOPE_EXPECT_ARCH}, but this machine builds for ${CLOUDSCOPE_ARCH}. "
    "Use the preset that matches the machine (linux-x64 or linux-aarch64).")
endif()
