#
# Copyright (c) 2016 UAVCAN Team
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
file(GLOB_RECURSE tests_src "*.cpp")
add_executable(run_tests
               ${tests_src}
               ../canard.c)
target_link_libraries(run_tests
                      ${GTEST_BOTH_LIBRARIES}
                      pthread)

# Old test application
add_executable(canard_test
               canard_test.c
               ../canard.c
               ../drivers/socketcan/socketcan.c)
target_link_libraries(canard_test
                      pthread)

# Demo application
add_executable(demo
               demo.c
               ../canard.c
               ../drivers/socketcan/socketcan.c)
