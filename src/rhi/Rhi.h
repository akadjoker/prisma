// prisma RHI - v0 (minimal). Grows with the renderer; every addition must come
// from a concrete need of a ported technique (see CLAUDE.md).
//
// Design reference: Filament backend (filament/backend/include/backend/
// DriverEnums.h, Handle.h, DriverApi) for handle-based resources.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

struct SDL_Window;

namespace prisma::rhi {

enum class Backend { OpenGL46, Vulkan };

struct BufferHandle { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
struct PipelineHandle { uint32_t id = 0; explicit operator bool() const { return id != 0; } };

enum class BufferUsage : uint32_t { Vertex = 1, Index = 2, Uniform = 4, Storage = 8 };

struct BufferDesc {
  size_t size = 0;
  BufferUsage usage = BufferUsage::Vertex;
  const void* data = nullptr;
};

enum class VertexFormat { Float2, Float3, Float4 };

struct VertexAttribute {
  uint32_t location = 0;
  VertexFormat format = VertexFormat::Float3;
  uint32_t offset = 0;
};

struct PipelineDesc {
  const char* vertexSource = nullptr;    // GLSL text for GL (SPIR-V for Vulkan later)
  const char* fragmentSource = nullptr;
  const VertexAttribute* attributes = nullptr;
  uint32_t attributeCount = 0;
  uint32_t vertexStride = 0;
};

struct ClearColor { float r = 0, g = 0, b = 0, a = 1; };

class Device {
 public:
  virtual ~Device() = default;

  virtual BufferHandle createBuffer(const BufferDesc& desc) = 0;
  virtual PipelineHandle createPipeline(const PipelineDesc& desc) = 0;
  virtual void destroy(BufferHandle h) = 0;
  virtual void destroy(PipelineHandle h) = 0;

  // Frame: renders to the window's default framebuffer.
  virtual void beginFrame(int width, int height, ClearColor clear) = 0;
  virtual void bindPipeline(PipelineHandle p) = 0;
  virtual void bindVertexBuffer(BufferHandle b) = 0;
  virtual void draw(uint32_t vertexCount, uint32_t firstVertex = 0) = 0;
  virtual void endFrame() = 0;  // swaps buffers

  virtual const char* rendererName() const = 0;
};

// Creates a device for an SDL window that was created with SDL_WINDOW_OPENGL
// (see createWindowFlags). Returns nullptr and fills `error` on failure.
std::unique_ptr<Device> createDevice(Backend backend, SDL_Window* window, std::string& error);

// SDL window flags/attributes required before SDL_CreateWindow for a backend.
uint32_t prepareWindowFlags(Backend backend);

}  // namespace prisma::rhi
