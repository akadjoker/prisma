file(GLOB_RECURSE PRISMA_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/prisma/*.cpp")
if(NOT PRISMA_OPENGL)
  list(FILTER PRISMA_SOURCES EXCLUDE REGEX "/rhi/gl/")
endif()

add_library(prisma STATIC ${PRISMA_SOURCES})
target_include_directories(prisma
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(NOT MSVC)
  target_compile_options(prisma PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

if(PRISMA_OPENGL)
  target_compile_definitions(prisma PRIVATE PRISMA_HAS_OPENGL)
  target_link_libraries(prisma PRIVATE gl)
endif()
