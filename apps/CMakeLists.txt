if(PRISMA_OPENGL)
  add_executable(clear clear/main.cpp)
  target_link_libraries(clear PRIVATE prisma platform)
endif()
