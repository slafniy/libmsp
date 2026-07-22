cmake_minimum_required(VERSION 4.2)
project(libmsp C)

set(CMAKE_C_STANDARD 23)

add_library(libmsp STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/libmsp.c)
set_target_properties(libmsp PROPERTIES PREFIX "")

target_include_directories(libmsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(testapp ${CMAKE_CURRENT_SOURCE_DIR}/testapp/testapp.c)

target_link_libraries(testapp PRIVATE libmsp)