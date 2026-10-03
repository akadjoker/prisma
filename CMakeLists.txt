cmake_minimum_required(VERSION 3.21)
project(prisma VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED)

# --- RHI -------------------------------------------------------------------
add_library(prisma_rhi STATIC
  src/rhi/gl/GLLoader.cpp
  src/rhi/gl/GLDevice.cpp
)
target_include_directories(prisma_rhi PUBLIC src)
target_link_libraries(prisma_rhi PUBLIC SDL2::SDL2)
target_compile_options(prisma_rhi PRIVATE -Wall -Wextra)

# --- apps ------------------------------------------------------------------
add_executable(triangle apps/triangle/main.cpp)
target_link_libraries(triangle PRIVATE prisma_rhi)
