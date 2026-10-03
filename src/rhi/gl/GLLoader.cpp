#include "rhi/gl/GLLoader.h"

#include <SDL.h>

namespace prisma::rhi::gl {

#define X(type, name) type name = nullptr;
PRISMA_GL_FUNCS(X)
#undef X

const char* loadFunctions() {
#define X(type, name)                                             \
  name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));    \
  if (!name) return #name;
  PRISMA_GL_FUNCS(X)
#undef X
  return nullptr;
}

}  // namespace prisma::rhi::gl
