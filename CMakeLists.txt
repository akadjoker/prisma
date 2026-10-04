cmake_minimum_required(VERSION 3.21)
project(prisma VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(PRISMA_OPENGL "Build the OpenGL backend" ON)
option(PRISMA_GLES "Use OpenGL ES 3 instead of OpenGL 4.6 in the OpenGL backend" OFF)
find_package(Vulkan QUIET)
option(PRISMA_VULKAN "Build the Vulkan backend" ${Vulkan_FOUND})
option(PRISMA_BUILD_DEMOS "Build the prisma demos" ON)
option(PRISMA_BUILD_TESTS "Build the prisma tests" ON)
option(PRISMA_WINDOW_TESTS "Also run the GPU tests in a real window" OFF)
option(PRISMA_HEADLESS_DEMOS "Build the demos without a window system, for screenshots" OFF)
set(PRISMA_MEDIA_DIR "${PROJECT_SOURCE_DIR}/tmp/DirectX-SDK-Samples-nov2024/Media" CACHE PATH
    "Folder with the media the demos load")
set(PRISMA_MODELS_DIR "${PROJECT_SOURCE_DIR}/tmp/filament/third_party/models" CACHE PATH
    "glTF models used by the viewer demo and its tests")

add_library(ct INTERFACE)
target_include_directories(ct INTERFACE external/containers/include)

add_subdirectory(libprisma)

if(PRISMA_BUILD_DEMOS OR PRISMA_BUILD_TESTS)
  include(cmake/PrismaShaders.cmake)
endif()

if(PRISMA_BUILD_DEMOS OR PRISMA_BUILD_TESTS)
  set(PLATFORM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(PLATFORM_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  if(PRISMA_HEADLESS_DEMOS)
    set(PLATFORM_USE_FAKE ON CACHE BOOL "" FORCE)
  endif()
  add_subdirectory(external/zen_plataform EXCLUDE_FROM_ALL)
  add_library(demo_common STATIC demos/common/stb_impl.cpp)
  target_include_directories(demo_common PUBLIC demos/common)
  target_compile_options(demo_common PRIVATE -w)
  if(PRISMA_SHADER_TOOLS)
    add_library(demo_ibl STATIC demos/common/Prefilter.cpp demos/common/Froxelizer.cpp
            demos/common/GltfModel.cpp demos/common/GltfGpu.cpp demos/common/cgltf_impl.cpp
            demos/common/third_party/mikktspace.c)
    set_source_files_properties(demos/common/cgltf_impl.cpp demos/common/third_party/mikktspace.c
                                PROPERTIES COMPILE_OPTIONS -w)
    target_include_directories(demo_ibl PUBLIC demos/common)
    target_link_libraries(demo_ibl PUBLIC prisma ct demo_common)
    prisma_shaders(demo_ibl demos/common/shaders/prefilter.vert demos/common/shaders/prefilter.frag)
  endif()
  if(PRISMA_BUILD_DEMOS)
    add_subdirectory(demos)
  endif()
endif()

if(PRISMA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
