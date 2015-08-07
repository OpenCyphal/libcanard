cmake_minimum_required(VERSION 2.6)
project(libcanard)

find_package(GTest REQUIRED)

include_directories(${GTEST_INCLUDE_DIRS})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x -Wall -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

file(GLOB_RECURSE tests_src "*.cpp")

add_executable(run_tests ${tests_src})
target_link_libraries(run_tests ${GTEST_BOTH_LIBRARIES} pthread)
