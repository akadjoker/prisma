add_executable(test_null_driver test_null_driver.cpp)
target_link_libraries(test_null_driver PRIVATE prisma)
add_test(NAME null_driver COMMAND test_null_driver)

if(PRISMA_OPENGL AND PRISMA_BUILD_APPS)
  add_executable(test_driver test_driver.cpp)
  target_include_directories(test_driver PRIVATE ${PROJECT_SOURCE_DIR}/libprisma/src)
  target_link_libraries(test_driver PRIVATE prisma platform)
  if(COMMAND prisma_spirv)
    prisma_spirv(test_driver
      shaders/plain.vert shaders/red.frag shaders/flat.vert shaders/flat.frag
      shaders/textured.vert shaders/textured.frag shaders/scaled.frag
      shaders/two_targets.frag shaders/instanced.vert shaders/instanced.frag
      shaders/no_buffer.vert shaders/array.frag shaders/cube.frag shaders/volume.frag
      shaders/lod.frag shaders/shadow.frag)
  endif()
  add_test(NAME gl_driver COMMAND test_driver)
  set_tests_properties(gl_driver PROPERTIES LABELS gpu)
  if(PRISMA_VULKAN)
    add_test(NAME vulkan_driver COMMAND test_driver vulkan)
    set_tests_properties(vulkan_driver PROPERTIES LABELS gpu)
  endif()
endif()
