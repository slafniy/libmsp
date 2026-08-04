cmake_minimum_required(VERSION 3.25)
project(libmsp C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF)

include(FetchContent)

# black magic
FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.12
)

# Require ALSA because it's a base, the rest are optional
# But in the docker container we install them too just in case, it doesn't affect library size mush
find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED alsa)
pkg_check_modules(PULSE QUIET libpulse)
pkg_check_modules(PIPEWIRE QUIET libpipewire-0.3)
pkg_check_modules(SNDIO QUIET sndio)

set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
set(SDL_UNSUPPORTED ON CACHE BOOL "" FORCE)

set(SDL_VIDEO OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)

set(SDL_AUDIO ON CACHE BOOL "" FORCE)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SDL3)

# The main output - single shared library
add_library(libmsp SHARED ${CMAKE_CURRENT_SOURCE_DIR}/src/libmsp.c
        src/command_q.h
        src/command_q.c
        src/logging.h)
set_target_properties(libmsp PROPERTIES
        PREFIX ""
        VERSION 0.3.1
        SOVERSION 0
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
        pthread
        dl
        SDL3::SDL3-static
)

# Executable app sample to demonstrate how library works
add_executable(testapp ${CMAKE_CURRENT_SOURCE_DIR}/testapp/testapp.c)
target_link_libraries(testapp PRIVATE libmsp)