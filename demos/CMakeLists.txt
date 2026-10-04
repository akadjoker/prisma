if(NOT PRISMA_OPENGL)
  return()
endif()

add_library(mathc STATIC ${PROJECT_SOURCE_DIR}/external/math/mathc.cpp)
target_include_directories(mathc PUBLIC ${PROJECT_SOURCE_DIR}/external/math)

function(prisma_demo folder name)
  cmake_parse_arguments(DEMO "" "" "SHADERS" ${ARGN})
  if(DEMO_SHADERS AND NOT PRISMA_SHADER_TOOLS)
    return()
  endif()
  add_executable(${name} ${folder}/main.cpp)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE prisma platform mathc)
  if(DEMO_SHADERS)
    prisma_shaders(${name} ${DEMO_SHADERS})
  endif()
endfunction()

prisma_demo(01_clear clear)
prisma_demo(02_triangle triangle
            SHADERS 02_triangle/shaders/triangle.vert 02_triangle/shaders/triangle.frag)
prisma_demo(03_cube cube
            SHADERS 03_cube/shaders/cube.vert 03_cube/shaders/cube.frag
                    03_cube/shaders/copy.vert 03_cube/shaders/copy.frag)
