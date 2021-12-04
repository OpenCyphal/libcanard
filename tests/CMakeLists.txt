# This software is distributed under the terms of the MIT License.
# Copyright (c) 2016 UAVCAN Consortium.
# Author: Pavel Kirienko <pavel@uavcan.org>
# Contributors: https://github.com/UAVCAN/libcanard/contributors.

cmake_minimum_required(VERSION 3.12)
project(canard_tests C CXX)
enable_testing()

set(CTEST_OUTPUT_ON_FAILURE ON)
set(library_dir "${CMAKE_SOURCE_DIR}/../libcanard")

# Use -DNO_STATIC_ANALYSIS=1 to suppress static analysis.
# If not suppressed, the tools used here shall be available, otherwise the build will fail.
if (NOT NO_STATIC_ANALYSIS)
    # clang-tidy (separate config files per directory)
    find_program(clang_tidy NAMES clang-tidy-12 clang-tidy)
    if (NOT clang_tidy)
        message(FATAL_ERROR "Could not locate clang-tidy")
    endif ()
    message(STATUS "Using clang-tidy: ${clang_tidy}")
    set(CMAKE_C_CLANG_TIDY ${clang_tidy})
    set(CMAKE_CXX_CLANG_TIDY ${clang_tidy})
endif ()

# clang-format
find_program(clang_format NAMES clang-format-12 clang-format)
if (NOT clang_format)
    message(STATUS "Could not locate clang-format")
else ()
    file(GLOB format_files ${library_dir}/*.[ch] ${CMAKE_SOURCE_DIR}/*.[ch]pp)
    message(STATUS "Using clang-format: ${clang_format}; files: ${format_files}")
    add_custom_target(format COMMAND ${clang_format} -i -fallback-style=none -style=file --verbose ${format_files})
endif ()

set(CMAKE_CXX_STANDARD 17)
set(CXX_EXTENSIONS OFF)
add_compile_options(
        -Wall -Wextra -Werror -pedantic -fstrict-aliasing -Wdouble-promotion -Wswitch-enum -Wfloat-equal -Wundef
        -Wconversion -Wtype-limits -Wsign-conversion -Wcast-align
)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Woverloaded-virtual -Wnon-virtual-dtor -Wsign-promo")

include_directories(catch ${library_dir})
add_definitions(-DCATCH_CONFIG_FAST_COMPILE=1 -DCATCH_CONFIG_ENABLE_ALL_STRINGMAKERS=1)

set(common_sources catch/main.cpp ${library_dir}/canard.c)

function(gen_test name files compile_definitions compile_flags link_flags c_standard)
    add_executable(${name} ${common_sources} ${files})
    target_compile_definitions(${name} PUBLIC ${compile_definitions})
    target_link_libraries(${name} pthread)
    set_target_properties(
            ${name}
            PROPERTIES
            COMPILE_FLAGS "${compile_flags}"
            LINK_FLAGS "${link_flags}"
            C_STANDARD "${c_standard}"
    )
    add_test("run_${name}" "${name}" --rng-seed time)
endfunction()

function(gen_test_matrix name files compile_definitions compile_flags)
    gen_test("${name}_x64_c99" "${files}" "${compile_definitions}" "${compile_flags} -m64" "-m64" "99")
    gen_test("${name}_x32_c99" "${files}" "${compile_definitions}" "${compile_flags} -m32" "-m32" "99")
    gen_test("${name}_x64_c11" "${files}" "${compile_definitions}" "${compile_flags} -m64" "-m64" "11")
    gen_test("${name}_x32_c11" "${files}" "${compile_definitions}" "${compile_flags} -m32" "-m32" "11")
    # Coverage is only available for GCC builds.
    if ((CMAKE_CXX_COMPILER_ID STREQUAL "GNU") AND (CMAKE_BUILD_TYPE STREQUAL "Debug"))
        gen_test("${name}_cov"
                "${files}"
                "${compile_definitions}"
                "${compile_flags} -g -O0 --coverage"
                "--coverage"
                "11")
    endif ()
endfunction()

# Disable missing declaration warning to allow exposure of private definitions.
gen_test_matrix(test_private
        "test_private_crc.cpp;test_private_rx.cpp;test_private_tx.cpp;test_private_cavl.cpp;"
        "-DCANARD_CONFIG_HEADER=\"${CMAKE_CURRENT_SOURCE_DIR}/canard_config_private.h\""
        "-Wno-missing-declarations")
# test CRC with static table disabled
gen_test_matrix(test_private_crc_table
        "test_private_crc.cpp;"
        "-DCANARD_CONFIG_HEADER=\"${CMAKE_CURRENT_SOURCE_DIR}/canard_config_private.h\""
        "-DCANARD_CRC_TABLE=0"
        "-Wno-missing-declarations")

gen_test_matrix(test_public
        "test_public_tx.cpp;test_public_rx.cpp;test_public_roundtrip.cpp;test_self.cpp;test_public_filters.cpp"
        ""
        "-Wmissing-declarations")
