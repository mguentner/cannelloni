cmake_minimum_required(VERSION 3.1)
project(cannelloni)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

# Options
option(SCTP_SUPPORT "SCTP_SUPPORT" ON)

set(CMAKE_CXX_FLAGS -Wall)

if(SCTP_SUPPORT)
  include(FindSCTP)
else(SCTP_SUPPORT)
  message(STATUS "Building cannelloni without SCTP support (SCTP_SUPPORT=OFF)")
endif(SCTP_SUPPORT)

if(NOT SCTP_FOUND AND SCTP_SUPPORT)
  set(SCTP_SUPPORT OFF)
  message(STATUS "SCTP not found. cannelloni will be build without SCTP support.")
  message(STATUS "Install lksctp-tools for SCTP.")
endif(NOT SCTP_FOUND AND SCTP_SUPPORT)

find_file(LINUX_VERSION_H "linux/version.h")
if(LINUX_VERSION_H)
  execute_process(
    COMMAND sed -n "s/#define\\s\\+LINUX_VERSION_CODE\\s\\+\\([0-9]\\+\\)/\\1/p" ${LINUX_VERSION_H}
    OUTPUT_VARIABLE LINUX_VERSION_CODE
  )
  if(LINUX_VERSION_CODE LESS 198144)
    # The Linux Kernel headers are older than version 3.6
    # We need to use the ones shipped with cannelloni
    include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  endif()
endif()

CONFIGURE_FILE(
  ${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/config.h
)

include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR})

add_executable(cannelloni cannelloni.cpp)
add_library(addsources STATIC
            connection.cpp
            framebuffer.cpp
            thread.cpp
            timer.cpp
            udpthread.cpp
            canthread.cpp)

add_library(cannelloni-common SHARED
            parser.cpp)

set_target_properties ( cannelloni-common
  PROPERTIES
  VERSION 0.0.1
  SOVERSION 0
)

include(GNUInstallDirs)

if(SCTP_SUPPORT)
    add_library(sctpthread STATIC sctpthread.cpp)
    target_link_libraries(sctpthread addsources sctp)
    target_link_libraries(addsources sctpthread)
endif(SCTP_SUPPORT)
set_target_properties(addsources PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(cannelloni addsources cannelloni-common pthread)
target_compile_features(cannelloni PRIVATE cxx_auto_type)
target_compile_features(addsources PRIVATE cxx_auto_type)

install(TARGETS cannelloni DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS cannelloni-common DESTINATION ${CMAKE_INSTALL_LIBDIR})
