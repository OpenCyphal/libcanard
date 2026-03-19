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

# STATIC ANALYSIS
option(NO_STATIC_ANALYSIS "Disable static analysis to speed up the build" OFF)
if (NOT NO_STATIC_ANALYSIS)
    # Clang-Tidy
    find_program(clang_tidy NAMES clang-tidy REQUIRED)
    message(STATUS "Using clang-tidy: ${clang_tidy}")
    set(CMAKE_CXX_CLANG_TIDY ${clang_tidy})
    set(CMAKE_C_CLANG_TIDY ${clang_tidy})

    # Cppcheck
    find_program(cppcheck NAMES cppcheck)
    if (NOT cppcheck)
        message(FATAL_ERROR "Could not locate cppcheck")
    endif ()
    message(STATUS "Using cppcheck: ${cppcheck}")
    set(cppcheck_options
            --enable=warning,style,performance,portability
            --error-exitcode=2
            --suppress=missingIncludeSystem
            --suppress=badBitmaskCheck
            --suppress=constParameterCallback
            --suppress=duplInheritedMember
            --suppress=preprocessorErrorDirective
            --suppress=unreadVariable
            --suppress=passedByValue
            --suppress=containerOutOfBounds
            --inline-suppr
    )
    set(CMAKE_C_CPPCHECK ${cppcheck};--language=c;--std=c11;${cppcheck_options})
    set(CMAKE_CXX_CPPCHECK ${cppcheck};--language=c++;--std=c++20;${cppcheck_options})

    # Clang-Format
    find_program(clang_format NAMES clang-format REQUIRED)
    file(GLOB format_files
            ${CMAKE_SOURCE_DIR}/libcanard/*.[ch]
            ${CMAKE_SOURCE_DIR}/tests/*/*.[ch]
            ${CMAKE_SOURCE_DIR}/tests/*/*.[ch]pp
    )
    message(STATUS "Using clang-format: ${clang_format}; files to format: ${format_files}")
    add_custom_target(format COMMAND ${clang_format} -i -fallback-style=none -style=file --verbose ${format_files})
    add_custom_target(
            format_check
            ALL
            COMMAND ${clang_format} --dry-run --Werror -fallback-style=none -style=file --verbose ${format_files}
            COMMENT "Checking clang-format conformance"
    )
endif ()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests)
