cmake_minimum_required(VERSION 3.1.0)
project(cannelloni)

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

add_executable(cannelloni cannelloni.cpp)
add_library(addsources STATIC
            connection.cpp
            framebuffer.cpp
            thread.cpp
            timer.cpp
            udpthread.cpp
            canthread.cpp
            sctpthread.cpp)
set_target_properties(addsources PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(cannelloni addsources pthread sctp)
target_compile_features(cannelloni PRIVATE cxx_auto_type)
target_compile_features(addsources PRIVATE cxx_auto_type)

install(TARGETS cannelloni DESTINATION bin)
