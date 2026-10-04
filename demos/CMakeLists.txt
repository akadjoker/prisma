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
  target_link_libraries(${name} PRIVATE prisma platform mathc demo_common)
  target_compile_definitions(${name} PRIVATE PRISMA_MEDIA_DIR="${PRISMA_MEDIA_DIR}")
  if(DEMO_SHADERS)
    prisma_shaders(${name} ${DEMO_SHADERS})
  endif()
endfunction()

file(GLOB demo_folders RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/[0-9][0-9]_*)
list(SORT demo_folders)
foreach(folder ${demo_folders})
  if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/main.cpp)
    continue()
  endif()
  string(REGEX REPLACE "^[0-9]+_" "" name ${folder})
  file(GLOB shaders RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
       ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/shaders/*.vert
       ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/shaders/*.frag
       ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/shaders/*.comp
       ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/shaders/*.geom
       ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/shaders/*.tesc
       ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/shaders/*.tese)
  if(shaders)
    prisma_demo(${folder} ${name} SHADERS ${shaders})
  else()
    prisma_demo(${folder} ${name})
  endif()
endforeach()
