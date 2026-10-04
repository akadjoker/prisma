#pragma once

#include <ct/slotmap32.hpp>

#include <cstdint>

namespace prisma
{

enum class DriverType : std::uint8_t
{
    Null,
    OpenGL,
    Vulkan
};

enum class DriverError : std::uint8_t
{
    None,
    NotCompiled,
    MissingPlatform,
    ContextFailed,
    LoaderFailed,
    VersionTooLow
};

struct GLPlatform
{
    void* user = nullptr;
    bool (*makeCurrent)(void* user) = nullptr;
    void (*swapBuffers)(void* user) = nullptr;
    void (*framebufferSize)(void* user, std::uint32_t* width, std::uint32_t* height) = nullptr;
};

struct DriverDesc
{
    DriverType type = DriverType::Null;
    const GLPlatform* gl = nullptr;
    void (*log)(const char* message) = nullptr;
};

struct BufferTag;
struct ShaderTag;
struct PipelineTag;

using BufferHandle = ct::Handle32<BufferTag>;
using ShaderHandle = ct::Handle32<ShaderTag>;
using PipelineHandle = ct::Handle32<PipelineTag>;

enum class BufferUsage : std::uint8_t
{
    Vertex
};

struct BufferDesc
{
    BufferUsage usage = BufferUsage::Vertex;
    std::uint32_t size = 0;
    const void* data = nullptr;
};

enum class ShaderStage : std::uint8_t
{
    Vertex,
    Fragment
};

struct ShaderDesc
{
    ShaderStage stage = ShaderStage::Vertex;
    const char* source = nullptr;
};

enum class VertexFormat : std::uint8_t
{
    Float2,
    Float3,
    Float4
};

struct VertexAttribute
{
    std::uint32_t location = 0;
    VertexFormat format = VertexFormat::Float3;
    std::uint32_t offset = 0;
};

enum class Topology : std::uint8_t
{
    Triangles,
    TriangleStrip,
    Lines,
    LineStrip,
    Points
};

struct PipelineDesc
{
    enum : std::uint32_t
    {
        kMaxAttributes = 8
    };

    ShaderHandle vertexShader;
    ShaderHandle fragmentShader;
    VertexAttribute attributes[kMaxAttributes];
    std::uint32_t attributeCount = 0;
    std::uint32_t vertexStride = 0;
    Topology topology = Topology::Triangles;
};

enum class LoadOp : std::uint8_t
{
    Load,
    Clear,
    DontCare
};

struct RenderPassDesc
{
    LoadOp colorLoad = LoadOp::Clear;
    LoadOp depthLoad = LoadOp::Clear;
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float clearDepth = 1.0f;
};

} // namespace prisma
