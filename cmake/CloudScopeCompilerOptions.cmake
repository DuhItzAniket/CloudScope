# Compiler settings shared by every CloudScope target.
#
#   cloudscope_target_defaults(<target>)
#
# Third-party headers are not affected: include directories of imported targets are system includes.

function(cloudscope_target_defaults target)
  get_target_property(_type ${target} TYPE)
  if(_type STREQUAL "INTERFACE_LIBRARY")
    set(_scope INTERFACE)
  else()
    set(_scope PRIVATE)
  endif()

  if(MSVC)
    target_compile_options(${target} ${_scope}
      /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor
      /w14242   # conversion, possible loss of data
      /w14254   # larger bit field assigned to a smaller one
      /w14263   # member function hides a virtual function without overriding it
      /w14265   # class has virtual functions but a non-virtual destructor
      /w14287   # unsigned/negative constant mismatch
      /w14296   # expression is always true or always false
      /w14311   # pointer truncation
      /w14545 /w14546 /w14547 /w14549 /w14555   # suspicious comma and call expressions
      /w14619   # unknown warning number in a pragma
      /w14640   # thread-unsafe static member initialisation
      /w14826   # sign-extended conversion
      /w14905 /w14906 /w14928)                  # suspicious string and copy-initialisation conversions
    target_compile_definitions(${target} ${_scope} NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    if(CLOUDSCOPE_WARNINGS_AS_ERRORS)
      target_compile_options(${target} ${_scope} /WX)
    endif()
  else()
    target_compile_options(${target} ${_scope}
      -Wall -Wextra -Wpedantic
      -Wshadow -Wconversion -Wsign-conversion -Wdouble-promotion
      -Wnon-virtual-dtor -Woverloaded-virtual -Wold-style-cast -Wcast-align
      -Wnull-dereference -Wformat=2 -Wimplicit-fallthrough -Wundef)
    if(CLOUDSCOPE_WARNINGS_AS_ERRORS)
      target_compile_options(${target} ${_scope} -Werror)
    endif()
  endif()

  if(CLOUDSCOPE_SANITIZE)
    if(MSVC)
      message(FATAL_ERROR "CLOUDSCOPE_SANITIZE is set up for GCC and Clang only.")
    endif()
    # no-sanitize-recover: the first finding stops the program, so a test cannot pass over it.
    target_compile_options(${target} ${_scope}
      -fsanitize=${CLOUDSCOPE_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(${target} ${_scope} -fsanitize=${CLOUDSCOPE_SANITIZE})
    if(CLOUDSCOPE_SANITIZE MATCHES "thread" AND _type STREQUAL "EXECUTABLE")
      target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/cmake/tsan_suppressions.cpp")
    endif()
  endif()

  if(CLOUDSCOPE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      message(FATAL_ERROR "CLOUDSCOPE_COVERAGE needs GCC (the report is made with gcov and gcovr).")
    endif()
    # atomic: counters stay correct when tests run threads; abs-path: gcovr finds sources from any folder.
    target_compile_options(${target} ${_scope} --coverage -fprofile-update=atomic -fprofile-abs-path)
    target_link_options(${target} ${_scope} --coverage)
  endif()

  # Qt: no keyword macros (signals/slots/emit), no implicit string conversions, no APIs deprecated before 6.8.
  target_compile_definitions(${target} ${_scope}
    QT_NO_KEYWORDS
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_FOREACH
    QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
    QT_DISABLE_DEPRECATED_UP_TO=0x060800)
endfunction()
