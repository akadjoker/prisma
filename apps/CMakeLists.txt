if(NOT PRISMA_OPENGL)
  return()
endif()

add_library(mathc STATIC ${PROJECT_SOURCE_DIR}/external/math/mathc.cpp)
target_include_directories(mathc PUBLIC ${PROJECT_SOURCE_DIR}/external/math)

if(PRISMA_VULKAN)
  find_program(PRISMA_GLSLANG glslangValidator HINTS ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE})
endif()

function(prisma_spirv target)
  if(NOT PRISMA_VULKAN OR NOT PRISMA_GLSLANG)
    return()
  endif()
  set(directory ${CMAKE_CURRENT_BINARY_DIR}/spirv)
  foreach(shader ${ARGN})
    get_filename_component(name ${shader} NAME)
    string(REPLACE "." "_" variable ${name})
    set(output ${directory}/${name}.h)
    add_custom_command(
      OUTPUT ${output}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${directory}
      COMMAND ${PRISMA_GLSLANG} -V --quiet --vn ${variable} -o ${output}
              ${CMAKE_CURRENT_SOURCE_DIR}/${shader}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${shader}
      VERBATIM)
    target_sources(${target} PRIVATE ${output})
  endforeach()
  target_include_directories(${target} PRIVATE ${directory})
  target_compile_definitions(${target} PRIVATE PRISMA_APP_SPIRV)
endfunction()

foreach(app clear triangle cube)
  add_executable(${app} ${app}/main.cpp)
  target_include_directories(${app} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${app} PRIVATE prisma platform mathc)
endforeach()

prisma_spirv(triangle triangle/shaders/triangle.vert triangle/shaders/triangle.frag)
prisma_spirv(cube cube/shaders/cube.vert cube/shaders/cube.frag cube/shaders/copy.vert
             cube/shaders/copy.frag)
