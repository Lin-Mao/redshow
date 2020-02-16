cmake_minimum_required(VERSION 3.10)
project(CUDA_RedShow)

set(CMAKE_CXX_STANDARD 14)
include_directories(include/ /home/find/opt/gpu-patch/include/)
include_directories(/home/find/hpc/spack/opt/spack/linux-ubuntu18.04-zen/gcc-7.4.0/boost-1.66.0-3nisemp3zggzjyyh37oytgvganxgubcv/include)
add_library(redshow.so STATIC include/common_lib.h src/common_lib.cpp include/redshow.h include/cxxopts.hpp include/instruction.h
        src/instruction.cpp src/redshow.cpp)
add_executable(CUDA_RedShow main.cpp include/common_lib.h src/common_lib.cpp include/redshow.h include/cxxopts.hpp include/instruction.h
        src/instruction.cpp src/redshow.cpp)