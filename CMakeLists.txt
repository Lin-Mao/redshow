cmake_minimum_required(VERSION 3.10)
project(CUDA_RedShow)

set(CMAKE_CXX_STANDARD 14)
include_directories(include/ /home/findhao/opt/gpu-patch/include/)
include_directories(/home/findhao/d/spack/opt/spack/linux-ubuntu20.04-zen2/gcc-9.3.0/boost-1.66.0-3vbbb3coqgs5murygfin6eua4sgysgd3/include)
add_library(redshow.so STATIC include/common_lib.h src/common_lib.cpp include/redshow.h include/cxxopts.hpp include/instruction.h
        src/instruction.cpp src/redshow.cpp)