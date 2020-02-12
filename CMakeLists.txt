cmake_minimum_required(VERSION 3.10)
project(CUDA_RedShow)

set(CMAKE_CXX_STANDARD 14)
include_directories(include)
add_executable(CUDA_RedShow main.cpp include/common_lib.h src/common_lib.cpp)