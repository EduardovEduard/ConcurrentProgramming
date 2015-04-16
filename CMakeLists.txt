cmake_minimum_required(VERSION 3.1)
project(concurrent_programming_thread_pool)

find_package(Boost COMPONENTS system thread REQUIRED)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1y -pthread")

set(SOURCE_FILES main.cpp)
add_executable(${CMAKE_PROJECT_NAME} ${SOURCE_FILES})
target_link_libraries(${CMAKE_PROJECT_NAME} boost_system boost_thread)