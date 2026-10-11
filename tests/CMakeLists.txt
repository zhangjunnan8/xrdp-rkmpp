foreach(mode 0 1 2 3 5)
    add_library(cb_fixture_${mode} MODULE fixture_backend.c)
    target_include_directories(cb_fixture_${mode} PRIVATE ../include)
    target_compile_definitions(cb_fixture_${mode} PRIVATE CB_FIXTURE_MODE=${mode})
endforeach()
add_executable(test-contract test_contract.c)
target_link_libraries(test-contract PRIVATE codecbridge)
add_test(NAME contract COMMAND test-contract
    $<TARGET_FILE:cb_fixture_0> $<TARGET_FILE:cb_fixture_1>
    $<TARGET_FILE:cb_fixture_2> $<TARGET_FILE:cb_fixture_3> $<TARGET_FILE:cb_fixture_5>)
set_tests_properties(contract PROPERTIES TIMEOUT 30)

if(CB_BUILD_X264)
    add_executable(test-x264 test_x264.c)
    target_link_libraries(test-x264 PRIVATE codecbridge)
    add_test(NAME x264-lifecycle COMMAND test-x264 $<TARGET_FILE:cb_backend_x264>)
    set_tests_properties(x264-lifecycle PROPERTIES TIMEOUT 60)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    find_program(FFMPEG_EXECUTABLE ffmpeg)
    find_program(FFPROBE_EXECUTABLE ffprobe)
    if(Python3_FOUND AND FFMPEG_EXECUTABLE AND FFPROBE_EXECUTABLE)
        add_test(NAME h264-roundtrip COMMAND ${Python3_EXECUTABLE} -S
            ${CMAKE_CURRENT_SOURCE_DIR}/verify_roundtrip.py
            $<TARGET_FILE:cb-encode> $<TARGET_FILE:cb_backend_x264>
            ${FFMPEG_EXECUTABLE} ${FFPROBE_EXECUTABLE})
        set_tests_properties(h264-roundtrip PROPERTIES TIMEOUT 180)
    else()
        message(STATUS "H.264 round-trip test unavailable: install Python3, ffmpeg and ffprobe")
    endif()
endif()
