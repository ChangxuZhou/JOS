cmake_minimum_required(VERSION 3.3)
project(os_test)

include_directories(
        "include"
        "./"
)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c99")

set(SOURCE_FILES dummy.c)
add_executable(dummy ${SOURCE_FILES})
