// Minimal OpenGL 4.6 core function loader using SDL_GL_GetProcAddress and the
// system <GL/glcorearb.h>. Add a function by appending a line to the list.
#pragma once

#define GL_GLEXT_PROTOTYPES 0
#include <GL/glcorearb.h>

#define PRISMA_GL_FUNCS(X)                                              \
  X(PFNGLGETSTRINGPROC, glGetString)                                    \
  X(PFNGLGETINTEGERVPROC, glGetIntegerv)                                \
  X(PFNGLENABLEPROC, glEnable)                                          \
  X(PFNGLVIEWPORTPROC, glViewport)                                      \
  X(PFNGLCLEARCOLORPROC, glClearColor)                                  \
  X(PFNGLCLEARPROC, glClear)                                            \
  X(PFNGLDRAWARRAYSPROC, glDrawArrays)                                  \
  X(PFNGLCREATEBUFFERSPROC, glCreateBuffers)                            \
  X(PFNGLNAMEDBUFFERDATAPROC, glNamedBufferData)                        \
  X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                            \
  X(PFNGLCREATEVERTEXARRAYSPROC, glCreateVertexArrays)                  \
  X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)                  \
  X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                        \
  X(PFNGLENABLEVERTEXARRAYATTRIBPROC, glEnableVertexArrayAttrib)        \
  X(PFNGLVERTEXARRAYATTRIBFORMATPROC, glVertexArrayAttribFormat)        \
  X(PFNGLVERTEXARRAYATTRIBBINDINGPROC, glVertexArrayAttribBinding)      \
  X(PFNGLVERTEXARRAYVERTEXBUFFERPROC, glVertexArrayVertexBuffer)        \
  X(PFNGLCREATESHADERPROC, glCreateShader)                              \
  X(PFNGLSHADERSOURCEPROC, glShaderSource)                              \
  X(PFNGLCOMPILESHADERPROC, glCompileShader)                            \
  X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                \
  X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                      \
  X(PFNGLDELETESHADERPROC, glDeleteShader)                              \
  X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                            \
  X(PFNGLATTACHSHADERPROC, glAttachShader)                              \
  X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                \
  X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                              \
  X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                    \
  X(PFNGLUSEPROGRAMPROC, glUseProgram)                                  \
  X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)

namespace prisma::rhi::gl {

#define X(type, name) extern type name;
PRISMA_GL_FUNCS(X)
#undef X

// Loads every function above. Returns the name of the first missing one, or nullptr.
const char* loadFunctions();

}  // namespace prisma::rhi::gl
