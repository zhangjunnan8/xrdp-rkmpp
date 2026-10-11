cmake_minimum_required(VERSION 3.16)
project(codecbridge VERSION 0.1.0 LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "The runtime currently targets Linux. Build on the target board.")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(GNUInstallDirs)
include(CTest)
option(CB_BUILD_X264 "Build the software H.264 plugin" ON)
set(CB_SANITIZERS "" CACHE STRING "Sanitizers: empty, address, undefined, or address,undefined")

add_compile_definitions(_POSIX_C_SOURCE=200809L)
add_compile_options(-Wall -Wextra -Werror -Wformat=2 -Wshadow -fvisibility=hidden)
if(CB_SANITIZERS)
    if(NOT CB_SANITIZERS MATCHES "^(address|undefined|address,undefined)$")
        message(FATAL_ERROR "CB_SANITIZERS must be address, undefined, or address,undefined")
    endif()
    add_compile_options(-fsanitize=${CB_SANITIZERS} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${CB_SANITIZERS})
endif()

add_library(codecbridge SHARED src/codecbridge.c)
target_include_directories(codecbridge PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(codecbridge PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(codecbridge PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 1)

add_executable(cb-encode tools/cb_encode.c)
target_link_libraries(cb-encode PRIVATE codecbridge)
set_target_properties(cb-encode PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")

if(CB_BUILD_X264)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(X264 REQUIRED IMPORTED_TARGET x264)
    add_library(cb_backend_x264 MODULE backends/x264/x264_backend.c)
    target_include_directories(cb_backend_x264 PRIVATE include)
    target_link_libraries(cb_backend_x264 PRIVATE PkgConfig::X264)
    set_target_properties(cb_backend_x264 PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
    install(TARGETS cb_backend_x264 LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/codecbridge)
endif()

install(TARGETS codecbridge cb-encode
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/codecbridge DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
