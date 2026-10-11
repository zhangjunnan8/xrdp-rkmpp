include(ExternalProject)
include("${CMAKE_CURRENT_LIST_DIR}/UpstreamVersions.cmake")
find_package(Git REQUIRED)
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)

option(XRDP_RKMPP_FETCH_ONLY "Fetch pinned source and check patches; produce no binaries" OFF)
if(DEFINED RKMPP_CONFIGURED_FETCH_ONLY AND NOT "${RKMPP_CONFIGURED_FETCH_ONLY}" STREQUAL "${XRDP_RKMPP_FETCH_ONLY}")
    message(FATAL_ERROR "Use a new build directory when changing XRDP_RKMPP_FETCH_ONLY")
endif()
set(RKMPP_CONFIGURED_FETCH_ONLY "${XRDP_RKMPP_FETCH_ONLY}" CACHE INTERNAL "Configured build mode")
set(XRDP_RKMPP_JOBS "4" CACHE STRING "Parallel jobs per upstream GNU Make build")
if(NOT XRDP_RKMPP_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "XRDP_RKMPP_JOBS must be a positive integer")
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Use Linux on the RK3576 board, or Linux/WSL with XRDP_RKMPP_FETCH_ONLY=ON")
endif()

execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE RKMPP_SOURCE_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _git_result)
if(NOT _git_result EQUAL 0)
    message(FATAL_ERROR "Build from a Git checkout of xrdp-rkmpp")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" symbolic-ref -q HEAD
    OUTPUT_VARIABLE _branch_ref OUTPUT_STRIP_TRAILING_WHITESPACE)
foreach(_ref HEAD packed-refs ${_branch_ref})
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" rev-parse --git-path "${_ref}"
        OUTPUT_VARIABLE _ref_path OUTPUT_STRIP_TRAILING_WHITESPACE)
    get_filename_component(_ref_path "${_ref_path}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
    if(EXISTS "${_ref_path}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_ref_path}")
    endif()
endforeach()
set(RKMPP_XRDP_PREFIX "/opt/xrdp-rkmpp/${RKMPP_SOURCE_COMMIT}")
set(RKMPP_XORG_PREFIX "/opt/xrdp-rkmpp-xorg/${RKMPP_SOURCE_COMMIT}")
if(NOT XRDP_RKMPP_FETCH_ONLY)
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$" OR CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "Native ARM64 Linux is required. On a workstation, use -DXRDP_RKMPP_FETCH_ONLY=ON to check sources only.")
    endif()
    enable_language(C CXX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RKMPP_DEPS REQUIRED rockchip_mpp x264 "openh264>=2.0" "fuse3>=3.1" xorg-server x11 xfixes xrandr pixman-1 openssl)
    foreach(_program make autoreconf autoconf automake libtoolize)
        string(TOUPPER "${_program}" _variable)
        find_program(RKMPP_${_variable} NAMES "${_program}")
        if(NOT RKMPP_${_variable})
            message(FATAL_ERROR "Missing ${_program}; install the documented build dependencies")
        endif()
    endforeach()
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    endif()
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _build_type)
    set(RKMPP_CFLAGS "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${_build_type}}")
    set(RKMPP_CXXFLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_build_type}}")
endif()

set(_helper "${PROJECT_SOURCE_DIR}/tools/build-upstream.py")
set(_integration "${PROJECT_SOURCE_DIR}/rk3576")
set(_common_args --repo "${PROJECT_SOURCE_DIR}" --root "${PROJECT_BINARY_DIR}"
    --git "${GIT_EXECUTABLE}" --source-commit "${RKMPP_SOURCE_COMMIT}")

function(rkmpp_upstream target kind repository revision version prefix)
    set(_outer "${PROJECT_BINARY_DIR}/${kind}")
    set(_work "${_outer}/${kind}-${version}")
    set(_source "${PROJECT_BINARY_DIR}/_deps/${kind}-src")
    set(_args ${_common_args} --kind "${kind}" --upstream "${_source}"
        --revision "${revision}" --work "${_work}" --output "${_outer}" --prefix "${prefix}")
    if(XRDP_RKMPP_FETCH_ONLY)
        set(_configure "${Python3_EXECUTABLE}" -S "${_helper}" prepare ${_args})
        set(_build "${CMAKE_COMMAND}" -E true)
        set(_install "${CMAKE_COMMAND}" -E true)
    else()
        list(APPEND _args --cc "${CMAKE_C_COMPILER}" --cxx "${CMAKE_CXX_COMPILER}"
            "--cflags=${RKMPP_CFLAGS}" "--cxxflags=${RKMPP_CXXFLAGS}"
            "--ldflags=${CMAKE_EXE_LINKER_FLAGS}" --make "${RKMPP_MAKE}"
            --xrdp-prefix "${RKMPP_XRDP_PREFIX}" --xrdp-stage "${PROJECT_BINARY_DIR}/xrdp/stage")
        set(_configure "${Python3_EXECUTABLE}" -S "${_helper}" configure ${_args})
        set(_build "${RKMPP_MAKE}" -C "${_work}" "-j${XRDP_RKMPP_JOBS}")
        set(_install "${Python3_EXECUTABLE}" -S "${_helper}" stage ${_args})
    endif()
    ExternalProject_Add(${target}
        PREFIX "${PROJECT_BINARY_DIR}/_deps/${kind}-ep"
        SOURCE_DIR "${_source}" BINARY_DIR "${_work}"
        DOWNLOAD_COMMAND "${Python3_EXECUTABLE}" -S "${_helper}" fetch
            --repo "${PROJECT_SOURCE_DIR}" --root "${PROJECT_BINARY_DIR}"
            --kind "${kind}" --git "${GIT_EXECUTABLE}" --upstream "${_source}"
            --revision "${revision}" --repository "${repository}"
        UPDATE_COMMAND ""
        CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E chdir "${PROJECT_BINARY_DIR}" ${_configure}
        BUILD_COMMAND ${_build} INSTALL_COMMAND ${_install}
        BUILD_ALWAYS TRUE
        LOG_CONFIGURE TRUE LOG_BUILD TRUE LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE)
    # Prepare scripts expect pristine source. Reconfigure from the clean upstream
    # checkout when our integration changes, rather than applying a patch twice.
    file(GLOB _inputs CONFIGURE_DEPENDS "${_integration}/*.c" "${_integration}/*.h"
        "${_integration}/*.inc" "${_integration}/prepare*.py")
    ExternalProject_Add_StepDependencies(${target} configure ${_inputs} "${_helper}")
endfunction()

rkmpp_upstream(rkmpp-xrdp xrdp "${XRDP_RKMPP_XRDP_REPOSITORY}"
    "${RKMPP_XRDP_COMMIT}" "${RKMPP_XRDP_VERSION}" "${RKMPP_XRDP_PREFIX}")
rkmpp_upstream(rkmpp-xorgxrdp xorgxrdp "${XRDP_RKMPP_XORG_REPOSITORY}"
    "${RKMPP_XORG_COMMIT}" "${RKMPP_XORG_VERSION}" "${RKMPP_XORG_PREFIX}")
add_dependencies(rkmpp-xorgxrdp rkmpp-xrdp)

if(NOT XRDP_RKMPP_FETCH_ONLY)
    message(STATUS "Build produces staged binaries only; it never installs or restarts a service.")
    message(STATUS "XRDP stage: ${PROJECT_BINARY_DIR}/xrdp/stage${RKMPP_XRDP_PREFIX}")
    message(STATUS "Xorg stage: ${PROJECT_BINARY_DIR}/xorgxrdp/stage${RKMPP_XORG_PREFIX}")
else()
    message(STATUS "FETCH_ONLY: downloading and checking pinned sources; NO binaries or hardware validation.")
endif()
