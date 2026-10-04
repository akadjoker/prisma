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

struct VulkanPlatform
{
    void* user = nullptr;
    const char* const* (*instanceExtensions)(void* user, std::uint32_t* count) = nullptr;
    bool (*createSurface)(void* user, void* instance, std::uint64_t* surface) = nullptr;
    void (*framebufferSize)(void* user, std::uint32_t* width, std::uint32_t* height) = nullptr;
};

struct DriverDesc
{
    DriverType type = DriverType::Null;
    const GLPlatform* gl = nullptr;
    const VulkanPlatform* vulkan = nullptr;
    void (*log)(const char* message) = nullptr;
    bool debug = false;
};

struct BufferTag;
struct ShaderTag;
struct PipelineTag;
struct TextureTag;
struct SamplerTag;

using BufferHandle = ct::Handle32<BufferTag>;
using ShaderHandle = ct::Handle32<ShaderTag>;
using PipelineHandle = ct::Handle32<PipelineTag>;
using TextureHandle = ct::Handle32<TextureTag>;
using SamplerHandle = ct::Handle32<SamplerTag>;

enum class BufferUsage : std::uint8_t
{
    Vertex,
    Index,
    Uniform
};

enum class IndexFormat : std::uint8_t
{
    UInt16,
    UInt32
};

enum class BufferUpdate : std::uint8_t
{
    Static,
    Dynamic,
    Stream
};

struct BufferDesc
{
    BufferUsage usage = BufferUsage::Vertex;
    std::uint32_t size = 0;
    const void* data = nullptr;
    BufferUpdate update = BufferUpdate::Static;
    IndexFormat indexFormat = IndexFormat::UInt16;
    const char* debugName = nullptr;
};

enum class TextureFormat : std::uint8_t
{
    None,
    R8,
    RG8,
    RGBA8,
    RGBA8Srgb,
    RGB10A2,
    RGBA16F,
    R11G11B10F,
    Depth32F,
    Depth24Stencil8
};

enum TextureUsage : std::uint32_t
{
    kTextureSampled = 1u << 0,
    kTextureRenderTarget = 1u << 1
};

struct TextureDesc
{
    TextureFormat format = TextureFormat::RGBA8;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mipLevels = 1;
    std::uint32_t usage = kTextureSampled;
    const void* data = nullptr;
    bool generateMipmaps = false;
    const char* debugName = nullptr;
};

enum class Filter : std::uint8_t
{
    Nearest,
    Linear
};

enum class MipFilter : std::uint8_t
{
    None,
    Nearest,
    Linear
};

enum class AddressMode : std::uint8_t
{
    Repeat,
    MirroredRepeat,
    ClampToEdge
};

struct SamplerDesc
{
    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    MipFilter mipFilter = MipFilter::Linear;
    AddressMode addressU = AddressMode::Repeat;
    AddressMode addressV = AddressMode::Repeat;
    const char* debugName = nullptr;
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
    Float1,
    Float2,
    Float3,
    Float4,
    Half2,
    Half4,
    UByte4Norm,
    Byte4Norm,
    UShort2Norm,
    UShort4Norm,
    Short2Norm,
    Short4Norm,
    Int1010102Norm,
    UByte4,
    UShort4
};

enum class VertexStep : std::uint8_t
{
    Vertex,
    Instance
};

struct VertexBufferLayout
{
    std::uint32_t stride = 0;
    VertexStep step = VertexStep::Vertex;
};

struct VertexAttribute
{
    std::uint32_t location = 0;
    VertexFormat format = VertexFormat::Float3;
    std::uint32_t offset = 0;
    std::uint32_t buffer = 0;
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

struct TargetFormats
{
    enum : std::uint32_t
    {
        kMaxColors = 4
    };

    bool window = true;
    TextureFormat colors[kMaxColors] = { TextureFormat::None, TextureFormat::None,
        TextureFormat::None, TextureFormat::None };
    std::uint32_t colorCount = 0;
    TextureFormat depth = TextureFormat::None;
};

struct TextureBinding
{
    const char* name = nullptr;
    std::uint32_t slot = 0;
};

struct PipelineDesc
{
    enum : std::uint32_t
    {
        kMaxAttributes = 16,
        kMaxVertexBuffers = 4,
        kMaxUniformBlocks = 4,
        kMaxTextures = 8
    };

    ShaderHandle vertexShader;
    ShaderHandle fragmentShader;
    VertexAttribute attributes[kMaxAttributes];
    std::uint32_t attributeCount = 0;
    VertexBufferLayout vertexBuffers[kMaxVertexBuffers];
    std::uint32_t vertexBufferCount = 0;
    Topology topology = Topology::Triangles;
    TargetFormats targets;

    UniformBlockBinding uniformBlocks[kMaxUniformBlocks];
    std::uint32_t uniformBlockCount = 0;

    TextureBinding textures[kMaxTextures];
    std::uint32_t textureCount = 0;

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

struct Viewport
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct Rect
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

enum class LoadOp : std::uint8_t
{
    Load,
    Clear,
    DontCare
};

enum class StoreOp : std::uint8_t
{
    Store,
    Discard
};

struct RenderPassDesc
{
    enum : std::uint32_t
    {
        kMaxColorTargets = 4
    };

    TextureHandle colors[kMaxColorTargets];
    std::uint32_t colorCount = 0;
    TextureHandle depth;

    LoadOp colorLoad = LoadOp::Clear;
    LoadOp depthLoad = LoadOp::Clear;
    StoreOp colorStore = StoreOp::Store;
    StoreOp depthStore = StoreOp::Store;
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float clearDepth = 1.0f;
};

} // namespace prisma
