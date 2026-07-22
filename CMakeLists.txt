cmake_minimum_required(VERSION 4.2)
project(libmsp C)

set(CMAKE_C_STANDARD 23)

# The main output - single shared library
add_library(libmsp SHARED ${CMAKE_CURRENT_SOURCE_DIR}/src/libmsp.c)
set_target_properties(libmsp PROPERTIES
        PREFIX ""
        VERSION 1.0.0
        SOVERSION 1
)

target_include_directories(libmsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Executable app sample to demonstrate how library works
add_executable(testapp ${CMAKE_CURRENT_SOURCE_DIR}/testapp/testapp.c)
target_link_libraries(testapp PRIVATE libmsp)