cmake_minimum_required(VERSION 3.21)
project(prisma VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(PRISMA_OPENGL "Build the OpenGL backend" ON)
option(PRISMA_BUILD_APPS "Build the prisma sample apps" ON)
option(PRISMA_BUILD_TESTS "Build the prisma tests" ON)

add_library(ct INTERFACE)
target_include_directories(ct INTERFACE external/containers/include)

if(PRISMA_OPENGL)
  add_library(gl STATIC external/opengl/OpenGL.cpp)
  target_include_directories(gl PUBLIC external/opengl)
  target_link_libraries(gl PUBLIC ${CMAKE_DL_LIBS})
endif()

add_subdirectory(libprisma)

if(PRISMA_BUILD_APPS)
  set(PLATFORM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(PLATFORM_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  add_subdirectory(external/zen_plataform EXCLUDE_FROM_ALL)
  add_subdirectory(apps)
endif()

if(PRISMA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
