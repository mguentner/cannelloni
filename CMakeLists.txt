cmake_minimum_required(VERSION 2.6)
project(cannelloni)

ADD_DEFINITIONS(
    -std=c++11
)

add_executable(cannelloni cannelloni.cpp)
add_library(addsources STATIC connection.cpp)
set_target_properties(addsources PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(cannelloni addsources pthread)
