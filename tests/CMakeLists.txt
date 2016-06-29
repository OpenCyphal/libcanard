#
# Copyright (c) 2016 UAVCAN Team
#

cmake_minimum_required(VERSION 2.6)
project(libcanard)

find_package(GTest REQUIRED)

include_directories(${GTEST_INCLUDE_DIRS})
include_directories(..)
include_directories(../drivers/socketcan)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -Werror -m32")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -std=c99   -Wall -Wextra -Werror -m32")

# Expose internal API for unit testing
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DCANARD_INTERNAL=''")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -DCANARD_INTERNAL=''")

file(GLOB_RECURSE tests_src "*.cpp")

set(canard_src "../canard.c")
set(socketcan_src "../drivers/socketcan/socketcan.c")

add_executable(run_tests ${tests_src} ${canard_src})
target_link_libraries(run_tests ${GTEST_BOTH_LIBRARIES} pthread)

add_executable(canard_test canard_test.c ${canard_src} ${socketcan_src})
target_link_libraries(canard_test pthread)
