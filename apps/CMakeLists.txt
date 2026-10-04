if(NOT PRISMA_OPENGL)
  return()
endif()

add_library(mathc STATIC ${PROJECT_SOURCE_DIR}/external/math/mathc.cpp)
target_include_directories(mathc PUBLIC ${PROJECT_SOURCE_DIR}/external/math)

set(apps clear)
if(PRISMA_SHADER_TOOLS)
  list(APPEND apps triangle cube)
endif()

foreach(app ${apps})
  add_executable(${app} ${app}/main.cpp)
  target_include_directories(${app} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${app} PRIVATE prisma platform mathc)
endforeach()

if(PRISMA_SHADER_TOOLS)
  prisma_shaders(triangle triangle/shaders/triangle.vert triangle/shaders/triangle.frag)
  prisma_shaders(cube cube/shaders/cube.vert cube/shaders/cube.frag cube/shaders/copy.vert
                 cube/shaders/copy.frag)
endif()
