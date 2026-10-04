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
    void* (*getProcAddress)(const char* name) = nullptr;
};

struct DriverDesc
{
    DriverType type = DriverType::Null;
    const GLPlatform* gl = nullptr;
    void (*log)(const char* message) = nullptr;
    bool debug = false;
};

struct BufferTag;
struct ShaderTag;
struct PipelineTag;

using BufferHandle = ct::Handle32<BufferTag>;
using ShaderHandle = ct::Handle32<ShaderTag>;
using PipelineHandle = ct::Handle32<PipelineTag>;

enum class BufferUsage : std::uint8_t
{
    Vertex,
    Index,
    Uniform
};

struct BufferDesc
{
    BufferUsage usage = BufferUsage::Vertex;
    std::uint32_t size = 0;
    const void* data = nullptr;
    bool dynamic = false;
    const char* debugName = nullptr;
};

enum class IndexFormat : std::uint8_t
{
    UInt16,
    UInt32
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
    const char* debugName = nullptr;
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

enum class CompareOp : std::uint8_t
{
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};

enum class CullMode : std::uint8_t
{
    None,
    Front,
    Back
};

enum class FrontFace : std::uint8_t
{
    CounterClockwise,
    Clockwise
};

enum class BlendFactor : std::uint8_t
{
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstColor,
    OneMinusDstColor,
    DstAlpha,
    OneMinusDstAlpha
};

struct UniformBlockBinding
{
    const char* name = nullptr;
    std::uint32_t slot = 0;
};

struct PipelineDesc
{
    enum : std::uint32_t
    {
        kMaxAttributes = 8,
        kMaxUniformBlocks = 4
    };

    ShaderHandle vertexShader;
    ShaderHandle fragmentShader;
    VertexAttribute attributes[kMaxAttributes];
    std::uint32_t attributeCount = 0;
    std::uint32_t vertexStride = 0;
    Topology topology = Topology::Triangles;

    UniformBlockBinding uniformBlocks[kMaxUniformBlocks];
    std::uint32_t uniformBlockCount = 0;

    bool depthTest = false;
    bool depthWrite = true;
    CompareOp depthCompare = CompareOp::Less;

    CullMode cullMode = CullMode::None;
    FrontFace frontFace = FrontFace::CounterClockwise;

    bool blend = false;
    BlendFactor srcColor = BlendFactor::One;
    BlendFactor dstColor = BlendFactor::Zero;
    BlendFactor srcAlpha = BlendFactor::One;
    BlendFactor dstAlpha = BlendFactor::Zero;

    const char* debugName = nullptr;
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
