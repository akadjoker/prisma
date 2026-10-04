add_executable(test_null_driver test_null_driver.cpp)
target_link_libraries(test_null_driver PRIVATE prisma)
add_test(NAME null_driver COMMAND test_null_driver)

add_executable(test_texture_loader test_texture_loader.cpp)
target_include_directories(test_texture_loader PRIVATE ${PROJECT_SOURCE_DIR}/demos/common)
target_link_libraries(test_texture_loader PRIVATE prisma demo_common ct)
target_compile_definitions(test_texture_loader PRIVATE PRISMA_MEDIA_DIR="${PRISMA_MEDIA_DIR}")
add_test(NAME texture_loader COMMAND test_texture_loader)

add_executable(test_sdkmesh test_sdkmesh.cpp)
target_include_directories(test_sdkmesh PRIVATE ${PROJECT_SOURCE_DIR}/demos/common)
target_link_libraries(test_sdkmesh PRIVATE prisma demo_common ct)
target_compile_definitions(test_sdkmesh PRIVATE PRISMA_MEDIA_DIR="${PRISMA_MEDIA_DIR}")
add_test(NAME sdkmesh COMMAND test_sdkmesh)

add_executable(test_free_ranges test_free_ranges.cpp)
target_include_directories(test_free_ranges PRIVATE ${PROJECT_SOURCE_DIR}/libprisma/src)
target_link_libraries(test_free_ranges PRIVATE ct)
add_test(NAME free_ranges COMMAND test_free_ranges)

if(PRISMA_OPENGL AND TARGET platform AND PRISMA_SHADER_TOOLS)
  add_executable(test_driver test_driver.cpp)
  target_include_directories(test_driver PRIVATE ${PROJECT_SOURCE_DIR}/libprisma/src)
  target_link_libraries(test_driver PRIVATE prisma platform)
  prisma_shaders(test_driver
    shaders/plain.vert shaders/red.frag shaders/flat.vert shaders/flat.frag
    shaders/textured.vert shaders/textured.frag shaders/scaled.frag
    shaders/two_targets.frag shaders/instanced.vert shaders/instanced.frag
    shaders/no_buffer.vert shaders/array.frag shaders/cube.frag shaders/volume.frag
    shaders/lod.frag shaders/shadow.frag shaders/cube_array.frag
    shaders/fill.comp shaders/vertices.comp shaders/storage.frag
    shaders/points.geom shaders/adjacency.geom shaders/notch.tesc shaders/notch.tese)
  add_test(NAME gl_driver COMMAND test_driver)
  set_tests_properties(gl_driver PROPERTIES LABELS gpu)
  if(PRISMA_VULKAN)
    add_test(NAME vulkan_driver COMMAND test_driver vulkan)
    set_tests_properties(vulkan_driver PROPERTIES LABELS gpu)
  endif()
endif()
