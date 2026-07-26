cmake_minimum_required(VERSION 4.2)
project(libmsp C)

set(CMAKE_C_STANDARD 23)
set(BUILD_SHARED_LIBS OFF)

# The main output - single shared library
add_library(libmsp SHARED ${CMAKE_CURRENT_SOURCE_DIR}/src/libmsp.c
        src/command_q.h
        src/command_q.c)
set_target_properties(libmsp PROPERTIES
        PREFIX ""
        VERSION 1.0.0
        SOVERSION 1
)

target_include_directories(libmsp PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg-static/include)
target_link_libraries(libmsp PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg-static/lib/libavformat.a
        ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg-static/lib/libavcodec.a
        ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg-static/lib/libswresample.a
        ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg-static/lib/libavutil.a
        m
)

# Executable app sample to demonstrate how library works
add_executable(testapp ${CMAKE_CURRENT_SOURCE_DIR}/testapp/testapp.c)
target_link_libraries(testapp PRIVATE libmsp)