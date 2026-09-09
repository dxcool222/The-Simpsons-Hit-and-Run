# Install script for directory: /Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/prefix")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng-build/libpng16.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libpng16.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libpng16.a")
    execute_process(COMMAND "/usr/bin/ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libpng16.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE DIRECTORY FILES "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng-build/png.framework" USE_SOURCE_PERMISSIONS)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/png.framework/Versions/1.6.43/png" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/png.framework/Versions/1.6.43/png")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" -x "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/png.framework/Versions/1.6.43/png")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE FILE FILES "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng-build/libpng.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES
    "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng/png.h"
    "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng/pngconf.h"
    "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng-build/pnglibconf.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/libpng16" TYPE FILE FILES
    "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng/png.h"
    "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng/pngconf.h"
    "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng-build/pnglibconf.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE PROGRAM FILES "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng-build/libpng-config")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE PROGRAM FILES "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng-build/libpng16-config")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/man/man3" TYPE FILE FILES
    "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng/libpng.3"
    "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng/libpngpf.3"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/man/man5" TYPE FILE FILES "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng/png.5")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng-build/libpng.pc")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE PROGRAM FILES "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng-build/libpng-config")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng-build/libpng16.pc")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE PROGRAM FILES "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng-build/libpng16-config")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libpng/libpng16.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libpng/libpng16.cmake"
         "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng-build/CMakeFiles/Export/27e805ec6cde31251f2809ac4798f31f/libpng16.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libpng/libpng16-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libpng/libpng16.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/libpng" TYPE FILE FILES "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng-build/CMakeFiles/Export/27e805ec6cde31251f2809ac4798f31f/libpng16.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/libpng" TYPE FILE FILES "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng-build/CMakeFiles/Export/27e805ec6cde31251f2809ac4798f31f/libpng16-release.cmake")
  endif()
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng-build/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
if(CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_COMPONENT MATCHES "^[a-zA-Z0-9_.+-]+$")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
  else()
    string(MD5 CMAKE_INST_COMP_HASH "${CMAKE_INSTALL_COMPONENT}")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INST_COMP_HASH}.txt")
    unset(CMAKE_INST_COMP_HASH)
  endif()
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/src/libpng-build/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
