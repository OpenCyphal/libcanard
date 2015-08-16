cmake_minimum_required(VERSION 2.6)
project(libcanard)

find_package(GTest REQUIRED)

include_directories(${GTEST_INCLUDE_DIRS})
include_directories(../src)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x -Wall -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

file(GLOB_RECURSE tests_src "*.cpp")
file(GLOB_RECURSE canard_src "../src/*.c")

add_executable(run_tests ${canard_src} ${tests_src})
target_link_libraries(run_tests ${GTEST_BOTH_LIBRARIES} pthread)
