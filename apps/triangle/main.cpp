#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "rhi/Rhi.h"

using namespace prisma::rhi;

static const char* kVs = R"(#version 460 core
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec3 inColor;
out vec3 vColor;
void main() { vColor = inColor; gl_Position = vec4(inPos, 0.0, 1.0); }
)";

static const char* kFs = R"(#version 460 core
in vec3 vColor;
out vec4 outColor;
void main() { outColor = vec4(vColor, 1.0); }
)";

int main() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  uint32_t flags = prepareWindowFlags(Backend::OpenGL46);
  SDL_Window* win = SDL_CreateWindow("prisma triangle", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, 1280, 720,
                                     flags | SDL_WINDOW_RESIZABLE);
  if (!win) {
    std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    return 1;
  }
  std::string err;
  auto dev = createDevice(Backend::OpenGL46, win, err);
  if (!dev) {
    std::fprintf(stderr, "createDevice: %s\n", err.c_str());
    return 1;
  }
  std::printf("renderer: %s\n", dev->rendererName());

  const float verts[] = {
      //  x      y     r  g  b
      -0.6f, -0.5f, 1, 0, 0,
       0.6f, -0.5f, 0, 1, 0,
       0.0f,  0.6f, 0, 0, 1,
  };
  BufferDesc bd;
  bd.size = sizeof(verts);
  bd.data = verts;
  BufferHandle vb = dev->createBuffer(bd);

  VertexAttribute attrs[] = {{0, VertexFormat::Float2, 0}, {1, VertexFormat::Float3, 8}};
  PipelineDesc pd;
  pd.vertexSource = kVs;
  pd.fragmentSource = kFs;
  pd.attributes = attrs;
  pd.attributeCount = 2;
  pd.vertexStride = 20;
  PipelineHandle pipe = dev->createPipeline(pd);
  if (!pipe) return 1;

  bool running = true;
  int frames = 0;
  const char* maxFrames = std::getenv("PRISMA_FRAMES");  // for automated runs
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
    }
    int w, h;
    SDL_GetWindowSize(win, &w, &h);
    dev->beginFrame(w, h, {0.05f, 0.06f, 0.09f, 1.0f});
    dev->bindPipeline(pipe);
    dev->bindVertexBuffer(vb);
    dev->draw(3);
    dev->endFrame();
    if (maxFrames && ++frames >= std::atoi(maxFrames)) running = false;
  }

  dev->destroy(pipe);
  dev->destroy(vb);
  dev.reset();
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
