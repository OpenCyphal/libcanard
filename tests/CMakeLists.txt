# This software is distributed under the terms of the MIT License.
# Copyright (c) OpenCyphal.
# Author: Pavel Kirienko <pavel@opencyphal.org>
# Contributors: https://github.com/OpenCyphal/libcanard/contributors.

cmake_minimum_required(VERSION 3.12)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Woverloaded-virtual -Wnon-virtual-dtor -Wsign-promo")

project(canard_tests C CXX)
enable_testing()

set(CTEST_OUTPUT_ON_FAILURE ON)
set(NO_STATIC_ANALYSIS OFF CACHE BOOL "disable canard static analysis")
set(library_dir "${CMAKE_SOURCE_DIR}/libcanard")
set(unity_root "${CMAKE_SOURCE_DIR}/lib/unity")

include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/lib/cavl2)

# Use -DNO_STATIC_ANALYSIS=1 to suppress static analysis. If not suppressed, the tools used here shall be available.
if (NOT NO_STATIC_ANALYSIS)
    find_program(clang_tidy NAMES clang-tidy)
    if (NOT clang_tidy)
        message(FATAL_ERROR "Could not locate clang-tidy")
    endif ()
    message(STATUS "Using clang-tidy: ${clang_tidy}")
endif()

function(gen_test name files compile_definitions compile_flags link_flags c_standard)
    # Unity
    add_library("${name}_unity" STATIC "${unity_root}/src/unity.c")
    target_include_directories("${name}_unity" SYSTEM PUBLIC "${unity_root}/src/")
    target_compile_definitions("${name}_unity" PUBLIC
            UNITY_INCLUDE_DOUBLE=1 UNITY_OUTPUT_COLOR=1 UNITY_SUPPORT_64=1 UNITY_SHORTHAND_AS_RAW=1)
    set_target_properties(
            "${name}_unity"
            PROPERTIES
            COMPILE_FLAGS "${compile_flags} \
            -Wno-sign-conversion -Wno-conversion -Wno-switch-enum -Wno-float-equal -Wno-double-promotion"
            LINK_FLAGS "${link_flags}"
    )
    # Target executable
    add_executable(${name} ${files})
    target_include_directories(${name} PUBLIC ${library_dir})
    target_compile_definitions(${name} PUBLIC ${compile_definitions})
    target_link_libraries(${name} "${name}_unity")
    set_target_properties(
            ${name}
            PROPERTIES
            COMPILE_FLAGS "${compile_flags} -Wall -Wextra -Werror -pedantic -Wdouble-promotion -Wswitch-enum \
            -Wfloat-equal -Wundef -Wconversion -Wtype-limits -Wsign-conversion -Wcast-align -Wmissing-declarations"
            LINK_FLAGS "${link_flags}"
            C_STANDARD "${c_standard}"
            C_EXTENSIONS OFF
            C_CLANG_TIDY "${clang_tidy}"
            CXX_CLANG_TIDY "${clang_tidy}"
    )
    add_test("run_${name}" "${name}")
endfunction()

function(gen_test_matrix name files)
    gen_test("${name}_x64_c99" "${files}" "" "-m64" "-m64" "99")
    gen_test("${name}_x32_c99" "${files}" "" "-m32" "-m32" "99")
    gen_test("${name}_x64_c11" "${files}" "" "-m64" "-m64" "11")
    gen_test("${name}_x32_c11" "${files}" "" "-m32" "-m32" "11")
endfunction()

function(gen_test_single name files) # When the full matrix is not needed, to keep pipelines fast.
    gen_test("${name}" "${files}" "" "-m32" "-m32" "11")
endfunction()

# Add the test targets.
gen_test_matrix(test_helpers "src/test_helpers.c")
