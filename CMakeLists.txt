cmake_minimum_required(VERSION 3.10)
project(CUDA_RedShow)

set(CMAKE_CXX_STANDARD 14)
include_directories(include/ /home/findhao/opt/gpu-patch/include/ include/analysis/ include/binutils/ include/common/ include/operation)
include_directories(src/ src/analysis src/binutils/ src/common src/operation)
include_directories(/home/findhao/d/spack/opt/spack/linux-ubuntu20.04-zen2/gcc-9.3.0/boost-1.66.0-3vbbb3coqgs5murygfin6eua4sgysgd3/include)
aux_source_directory(src/ DIR_SRCS)
FILE(GLOB_RECURSE SOURCE_FILES src/*/*.cpp )

add_library(redshow.so STATIC ${SOURCE_FILES})