cmake_minimum_required(VERSION 3.16)
project(xrdp_rkmpp VERSION 0.1.0 LANGUAGES NONE)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build: cmake -S . -B build")
endif()
include(cmake/RockchipBuild.cmake)
