# This software is distributed under the terms of the MIT License.
# Copyright (C) OpenCyphal Development Team  <opencyphal.org>
# SPDX-License-Identifier: MIT
# Author: Pavel Kirienko <pavel@opencyphal.org>
#
# This file is only needed for library development and testing. It is not needed to use the library in your project;
# instead, users should integrate the library by copying canard.c and canard.h.

cmake_minimum_required(VERSION 3.20)

project(canard)
enable_testing()
set(CMAKE_CTEST_ARGUMENTS "-V")  # Enable test outputs when running `make test`
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Shared Clang-Format target for all subprojects.
find_program(clang_format NAMES clang-format)
if (NOT clang_format)
    message(STATUS "Could not locate clang-format")
else ()
    file(GLOB_RECURSE format_files
            ${CMAKE_CURRENT_SOURCE_DIR}/libcanard/*.[ch]
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.[ch]
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.[ch]pp)
    message(STATUS "Using clang-format: ${clang_format}; files: ${format_files}")
    add_custom_target(format COMMAND ${clang_format} -i -fallback-style=none -style=file --verbose ${format_files})
endif ()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests)
