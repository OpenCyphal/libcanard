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
set(ENABLE_COVERAGE OFF CACHE BOOL "enable code coverage measurement")
set(library_dir "${CMAKE_SOURCE_DIR}/libcanard")
set(unity_root "${CMAKE_SOURCE_DIR}/lib/unity")

include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/lib/cavl2)

# Fail the build if poisoned identifiers are present.
# Poison fixed-length 8-bit integer types to maximize portability across DSP cores that may not support them.
set(POISON_REGEXP "\\b(int8_t|uint8_t)\\b")
if (NOT NO_STATIC_ANALYSIS)
    find_program(grep NAMES grep REQUIRED)
    execute_process(
            COMMAND "${grep}" -nE "${POISON_REGEXP}" "${library_dir}/canard.c" "${library_dir}/canard.h"
            RESULT_VARIABLE grep_result
            OUTPUT_VARIABLE grep_stdout
            ERROR_VARIABLE grep_stderr
    )
    if (grep_result EQUAL 0)
        message(FATAL_ERROR "Poisoned identifiers found:\n${grep_stdout}${grep_stderr}")
    elseif (grep_result EQUAL 1)
        message(STATUS "Poisoned identifiers not detected.")
    else ()
        message(FATAL_ERROR "Error occurred while searching for poisoned identifiers:\n${grep_stderr}")
    endif ()
endif ()

function(gen_test name files compile_definitions compile_flags link_flags c_standard)
    # Unity. As an external dependency, it is excluded from static analysis.
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
            C_CLANG_TIDY ""
            CXX_CLANG_TIDY ""
            C_CPPCHECK ""
            CXX_CPPCHECK ""
    )
    # Target executable
    add_executable(${name} ${files})
    target_include_directories(${name} PUBLIC ${library_dir})
    target_compile_definitions(${name} PUBLIC ${compile_definitions})
    target_link_libraries(${name} "${name}_unity")
    # Apply coverage flags if coverage is enabled.
    if (ENABLE_COVERAGE)
        target_compile_options(${name} PRIVATE --coverage -fprofile-arcs -ftest-coverage)
        target_link_options(${name} PRIVATE --coverage -fprofile-arcs)
        target_compile_definitions(${name} PRIVATE NDEBUG=1)  # Remove assertion checks as they interfere with coverage.
    endif ()
    set_target_properties(
            ${name}
            PROPERTIES
            COMPILE_FLAGS "${compile_flags} -Wall -Wextra -Werror -pedantic -Wdouble-promotion -Wswitch-enum \
            -Wfloat-equal -Wundef -Wconversion -Wtype-limits -Wsign-conversion -Wcast-align -Wmissing-declarations"
            LINK_FLAGS "${link_flags}"
            C_STANDARD "${c_standard}"
            C_EXTENSIONS OFF
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

# XXX TODO FIXME REMOVE THIS WHEN IMPLEMENTATION COMPLETE
add_compile_options(-Wno-unused-function)

# Add the test targets.
# Intrusive tests directly #include <canard.c> to access the internals.
gen_test_matrix(test_helpers "src/test_helpers.c")
gen_test_matrix(test_intrusive_util "src/test_intrusive_util.c")
gen_test_matrix(test_intrusive_tx "src/test_intrusive_tx.c")
gen_test_matrix(test_intrusive_rx "src/test_intrusive_rx.c")
gen_test_matrix(test_intrusive_rx_filter "src/test_intrusive_rx_filter.c")
gen_test_matrix(test_intrusive_rx_admission "src/test_intrusive_rx_admission.c")
gen_test_matrix(test_intrusive_rx_session "src/test_intrusive_rx_session.c")
gen_test_matrix(test_intrusive_misc "src/test_intrusive_misc.c")
# API tests.
gen_test_single(test_api_tx "${library_dir}/canard.c;src/test_api_tx.cpp")
gen_test_single(test_api_rx "${library_dir}/canard.c;src/test_api_rx.cpp")

# Coverage targets. Usage:
#   cmake -DENABLE_COVERAGE=ON -DNO_STATIC_ANALYSIS=ON ..
#   make -j$(nproc) && ctest && make coverage
#   xdg-open coverage-html/index.html
if (ENABLE_COVERAGE)
    find_program(LCOV_PATH lcov REQUIRED)
    find_program(GENHTML_PATH genhtml REQUIRED)

    # Reset coverage counters.
    add_custom_target(coverage-reset
            COMMAND ${LCOV_PATH} --zerocounters --directory .
            COMMAND ${LCOV_PATH} --capture --initial --directory . --output-file coverage-base.info
            --rc branch_coverage=1
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Resetting coverage counters"
    )

    # Generate coverage report.
    add_custom_target(coverage
            COMMAND ${LCOV_PATH} --capture --directory . --output-file coverage-total.info --rc branch_coverage=1
            COMMAND ${LCOV_PATH} --extract coverage-total.info '*/libcanard/canard.c' --output-file coverage-canard.info
            --rc branch_coverage=1

            COMMAND ${CMAKE_COMMAND} -E echo ""
            COMMAND ${CMAKE_COMMAND} -E echo "=== COVERAGE SUMMARY ==="
            COMMAND ${LCOV_PATH} --list coverage-canard.info --rc branch_coverage=1
            COMMAND ${CMAKE_COMMAND} -E echo "========================"
            COMMAND ${CMAKE_COMMAND} -E echo ""

            COMMAND ${GENHTML_PATH} coverage-canard.info --output-directory coverage-html --title "libcanard coverage"
            --legend --demangle-cpp --branch-coverage
            COMMAND ${CMAKE_COMMAND} -E echo "Coverage report: file://${CMAKE_BINARY_DIR}/coverage-html/index.html"

            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating coverage HTML report"
    )
endif ()
