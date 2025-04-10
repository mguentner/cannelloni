cmake_minimum_required(VERSION 3.15)
project(cannelloni)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

if(NOT PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  # Git auto-ignore out-of-source build directory
  file(GENERATE OUTPUT .gitignore CONTENT "*")
endif()

# Create compile_commands.json file
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(SCTP_SUPPORT "SCTP_SUPPORT" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")

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

CONFIGURE_FILE(
  ${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/config.h
)

include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR})

add_executable(cannelloni cannelloni.cpp)
add_library(addsources STATIC
            connection.cpp
            framebuffer.cpp
            inet_address.cpp
            thread.cpp
            timer.cpp
            udpthread.cpp
            tcpthread.cpp
            tcp_client_thread.cpp
            tcp_server_thread.cpp
            canthread.cpp)

add_library(cannelloni-common SHARED
            parser.cpp
            decoder.cpp)

add_library(cannelloni-common-static STATIC
            parser.cpp
            decoder.cpp)

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
target_link_libraries(cannelloni addsources cannelloni-common-static pthread)
target_compile_features(cannelloni PRIVATE cxx_auto_type)
target_compile_features(addsources PRIVATE cxx_auto_type)

install(TARGETS cannelloni DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS cannelloni-common DESTINATION ${CMAKE_INSTALL_LIBDIR})
