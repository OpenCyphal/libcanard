# This software is distributed under the terms of the MIT License.
# Copyright (c) OpenCyphal.
# Author: Pavel Kirienko <pavel@opencyphal.org>

cmake_minimum_required(VERSION 3.12)

add_executable(heartbeat_monitor heartbeat_monitor.c ${CMAKE_SOURCE_DIR}/libcanard/canard.c)
target_include_directories(heartbeat_monitor PRIVATE ${CMAKE_SOURCE_DIR}/libcanard)
target_include_directories(heartbeat_monitor SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/lib/cavl2)
set_target_properties(
        heartbeat_monitor
        PROPERTIES
        C_STANDARD 99
        C_EXTENSIONS OFF
        COMPILE_FLAGS "-Wall -Wextra -Werror -pedantic -Wconversion -Wsign-conversion -Wno-unused-function"
        C_CLANG_TIDY ""
        C_CPPCHECK ""
        CXX_CLANG_TIDY ""
        CXX_CPPCHECK ""
)
