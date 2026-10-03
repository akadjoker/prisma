#include <SDL.h>

#include <unordered_map>
#include <vector>

#include "rhi/Rhi.h"
#include "rhi/gl/GLLoader.h"

namespace prisma::rhi {

uint32_t prepareWindowFlags(Backend backend) {
  if (backend == Backend::OpenGL46) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    return SDL_WINDOW_OPENGL;
  }
  return 0;
}

namespace {

namespace g = gl;

struct GLPipeline {
  GLuint program = 0;
  GLuint vao = 0;
  uint32_t stride = 0;
};

GLuint compile(GLenum type, const char* src, std::string& error) {
  GLuint s = g::glCreateShader(type);
  g::glShaderSource(s, 1, &src, nullptr);
  g::glCompileShader(s);
  GLint ok = 0;
  g::glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    g::glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    error = log;
    g::glDeleteShader(s);
    return 0;
  }
  return s;
}

class GLDevice final : public Device {
 public:
  GLDevice(SDL_Window* window, SDL_GLContext ctx) : window_(window), ctx_(ctx) {}
  ~GLDevice() override {
    for (auto& [id, b] : buffers_) g::glDeleteBuffers(1, &b);
    for (auto& [id, p] : pipelines_) {
      g::glDeleteProgram(p.program);
      g::glDeleteVertexArrays(1, &p.vao);
    }
    if (ctx_) SDL_GL_DeleteContext(ctx_);
  }

  BufferHandle createBuffer(const BufferDesc& d) override {
    GLuint b = 0;
    g::glCreateBuffers(1, &b);
    g::glNamedBufferData(b, static_cast<GLsizeiptr>(d.size), d.data, GL_STATIC_DRAW);
    buffers_[++next_] = b;
    return {next_};
  }

  PipelineHandle createPipeline(const PipelineDesc& d) override {
    std::string err;
    GLuint vs = compile(GL_VERTEX_SHADER, d.vertexSource, err);
    if (!vs) { SDL_Log("vertex shader: %s", err.c_str()); return {}; }
    GLuint fs = compile(GL_FRAGMENT_SHADER, d.fragmentSource, err);
    if (!fs) { SDL_Log("fragment shader: %s", err.c_str()); return {}; }
    GLPipeline p;
    p.program = g::glCreateProgram();
    g::glAttachShader(p.program, vs);
    g::glAttachShader(p.program, fs);
    g::glLinkProgram(p.program);
    g::glDeleteShader(vs);
    g::glDeleteShader(fs);
    GLint ok = 0;
    g::glGetProgramiv(p.program, GL_LINK_STATUS, &ok);
    if (!ok) {
      char log[2048];
      g::glGetProgramInfoLog(p.program, sizeof(log), nullptr, log);
      SDL_Log("link: %s", log);
      return {};
    }
    g::glCreateVertexArrays(1, &p.vao);
    p.stride = d.vertexStride;
    for (uint32_t i = 0; i < d.attributeCount; ++i) {
      const VertexAttribute& a = d.attributes[i];
      GLint comps = a.format == VertexFormat::Float2 ? 2 : a.format == VertexFormat::Float3 ? 3 : 4;
      g::glEnableVertexArrayAttrib(p.vao, a.location);
      g::glVertexArrayAttribFormat(p.vao, a.location, comps, GL_FLOAT, GL_FALSE, a.offset);
      g::glVertexArrayAttribBinding(p.vao, a.location, 0);
    }
    pipelines_[++next_] = p;
    return {next_};
  }

  void destroy(BufferHandle h) override {
    auto it = buffers_.find(h.id);
    if (it == buffers_.end()) return;
    g::glDeleteBuffers(1, &it->second);
    buffers_.erase(it);
  }

  void destroy(PipelineHandle h) override {
    auto it = pipelines_.find(h.id);
    if (it == pipelines_.end()) return;
    g::glDeleteProgram(it->second.program);
    g::glDeleteVertexArrays(1, &it->second.vao);
    pipelines_.erase(it);
  }

  void beginFrame(int w, int h, ClearColor c) override {
    g::glViewport(0, 0, w, h);
    g::glClearColor(c.r, c.g, c.b, c.a);
    g::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }

  void bindPipeline(PipelineHandle h) override {
    current_ = &pipelines_.at(h.id);
    g::glUseProgram(current_->program);
    g::glBindVertexArray(current_->vao);
  }

  void bindVertexBuffer(BufferHandle h) override {
    g::glVertexArrayVertexBuffer(current_->vao, 0, buffers_.at(h.id), 0,
                                 static_cast<GLsizei>(current_->stride));
  }

  void draw(uint32_t count, uint32_t first) override {
    g::glDrawArrays(GL_TRIANGLES, static_cast<GLint>(first), static_cast<GLsizei>(count));
  }

  void endFrame() override { SDL_GL_SwapWindow(window_); }

  const char* rendererName() const override {
    return reinterpret_cast<const char*>(g::glGetString(GL_RENDERER));
  }

 private:
  SDL_Window* window_;
  SDL_GLContext ctx_;
  uint32_t next_ = 0;
  std::unordered_map<uint32_t, GLuint> buffers_;
  std::unordered_map<uint32_t, GLPipeline> pipelines_;
  GLPipeline* current_ = nullptr;
};

}  // namespace

std::unique_ptr<Device> createDevice(Backend backend, SDL_Window* window, std::string& error) {
  if (backend != Backend::OpenGL46) {
    error = "backend not implemented yet";
    return nullptr;
  }
  SDL_GLContext ctx = SDL_GL_CreateContext(window);
  if (!ctx) {
    error = SDL_GetError();
    return nullptr;
  }
  if (const char* missing = gl::loadFunctions()) {
    error = std::string("missing GL function: ") + missing;
    SDL_GL_DeleteContext(ctx);
    return nullptr;
  }
  SDL_GL_SetSwapInterval(1);
  return std::make_unique<GLDevice>(window, ctx);
}

}  // namespace prisma::rhi
