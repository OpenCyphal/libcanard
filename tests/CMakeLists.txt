#
# Copyright (c) 2016-2017 UAVCAN Team
#

cmake_minimum_required(VERSION 2.8)
project(libcanard)

# GTest
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

# Libcanard
include_directories(..)
include_directories(../drivers/socketcan)

# Compiler configuration - supporting only Clang and GCC
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -Werror -m32")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -std=c99   -Wall -Wextra -Werror -m32 -pedantic")

# Expose internal API for unit testing
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DCANARD_INTERNAL=''")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -DCANARD_INTERNAL=''")

# Unit tests
file(GLOB tests_src
     RELATIVE "${CMAKE_SOURCE_DIR}"
     "*.cpp"
     "stm32/*.cpp")
message(STATUS "Unit test source files: ${tests_src}")
add_executable(run_tests
               ${tests_src}
               ../canard.c)
target_link_libraries(run_tests
                      ${GTEST_BOTH_LIBRARIES}
                      pthread)

# Demo application
exec_program("git"
             ${CMAKE_CURRENT_SOURCE_DIR}
             ARGS "rev-parse --short=8 HEAD"
             OUTPUT_VARIABLE GIT_HASH)

add_executable(demo
               demo.c
               ../canard.c
               ../drivers/socketcan/socketcan.c)

target_compile_definitions(demo
                           PUBLIC GIT_HASH=0x${GIT_HASH})
