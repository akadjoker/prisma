#include "prisma/rhi/Driver.h"
#include "prisma/rhi/HandleCast.h"
#include "prisma/rhi/gl/GL.h"
#include "prisma/rhi/gl/GLState.h"

#include <ct/vector.hpp>

#include <stdio.h>
#include <string.h>

namespace prisma
{

namespace
{

struct GLBuffer
{
    GLuint id = 0;
    std::uint32_t size = 0;
    BufferUsage usage = BufferUsage::Vertex;
    IndexFormat indexFormat = IndexFormat::UInt16;
};

struct GLShader
{
    GLuint id = 0;
};

struct GLTexture
{
    GLuint id = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormat format = TextureFormat::RGBA8;
    std::uint32_t usage = 0;
};

struct GLFramebuffer
{
    GLuint id = 0;
    std::uint32_t colors[RenderPassDesc::kMaxColorTargets] = {};
    std::uint32_t colorCount = 0;
    std::uint32_t depth = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool stencil = false;
    TargetFormats formats;
};

static_assert(static_cast<std::uint32_t>(TargetFormats::kMaxColors) ==
                      static_cast<std::uint32_t>(RenderPassDesc::kMaxColorTargets),
        "pipeline and render pass must agree on the colour target count");

bool sameTargets(const TargetFormats& a, const TargetFormats& b)
{
    if (a.window != b.window) return false;
    if (a.window) return true;
    if (a.colorCount != b.colorCount || a.depth != b.depth) return false;
    for (std::uint32_t i = 0; i < a.colorCount; ++i)
        if (a.colors[i] != b.colors[i]) return false;
    return true;
}

bool isDepthFormat(TextureFormat format)
{
    return format == TextureFormat::Depth32F || format == TextureFormat::Depth24Stencil8;
}

struct GLSampler
{
    GLuint id = 0;
};

struct GLFormat
{
    GLenum internal;
    GLenum format;
    GLenum type;
};

GLFormat toGLFormat(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::None:
            break;
        case TextureFormat::R8:
            return { GL_R8, GL_RED, GL_UNSIGNED_BYTE };
        case TextureFormat::RG8:
            return { GL_RG8, GL_RG, GL_UNSIGNED_BYTE };
        case TextureFormat::RGBA8:
            return { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE };
        case TextureFormat::RGBA8Srgb:
            return { GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE };
        case TextureFormat::RGB10A2:
            return { GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV };
        case TextureFormat::RGBA16F:
            return { GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT };
        case TextureFormat::R11G11B10F:
            return { GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV };
        case TextureFormat::Depth32F:
            return { GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT };
        case TextureFormat::Depth24Stencil8:
            return { GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8 };
    }
    return { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE };
}

GLint toGLMinFilter(Filter filter, MipFilter mip)
{
    const bool linear = filter == Filter::Linear;
    switch (mip)
    {
        case MipFilter::None:
            return linear ? GL_LINEAR : GL_NEAREST;
        case MipFilter::Nearest:
            return linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
        case MipFilter::Linear:
            return linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    }
    return GL_LINEAR;
}

GLint toGLAddress(AddressMode mode)
{
    switch (mode)
    {
        case AddressMode::Repeat:
            return GL_REPEAT;
        case AddressMode::MirroredRepeat:
            return GL_MIRRORED_REPEAT;
        case AddressMode::ClampToEdge:
            return GL_CLAMP_TO_EDGE;
    }
    return GL_REPEAT;
}

std::uint32_t fullMipCount(std::uint32_t width, std::uint32_t height)
{
    std::uint32_t size = width > height ? width : height;
    std::uint32_t levels = 1;
    while (size > 1)
    {
        size >>= 1;
        ++levels;
    }
    return levels;
}

struct GLPipeline
{
    GLuint program = 0;
    GLuint vertexArray = 0;
    GLenum topology = GL_TRIANGLES;
    VertexBufferLayout vertexBuffers[PipelineDesc::kMaxVertexBuffers];
    std::uint32_t vertexBufferCount = 0;
    std::uint32_t attributeCount = 0;
    VertexAttribute attributes[PipelineDesc::kMaxAttributes];

    bool depthTest = false;
    bool depthWrite = true;
    GLenum depthFunc = GL_LESS;
    bool cull = false;
    GLenum cullFace = GL_BACK;
    GLenum frontFace = GL_CCW;
    bool blend = false;
    GLenum blendFunc[4] = { GL_ONE, GL_ZERO, GL_ONE, GL_ZERO };
    TargetFormats targets;
};

struct GLVertexFormat
{
    GLint components;
    GLenum type;
    GLboolean normalized;
    bool integer;
};

GLVertexFormat toGLVertexFormat(VertexFormat format)
{
    switch (format)
    {
        case VertexFormat::Float1:
            return { 1, GL_FLOAT, GL_FALSE, false };
        case VertexFormat::Float2:
            return { 2, GL_FLOAT, GL_FALSE, false };
        case VertexFormat::Float3:
            return { 3, GL_FLOAT, GL_FALSE, false };
        case VertexFormat::Float4:
            return { 4, GL_FLOAT, GL_FALSE, false };
        case VertexFormat::Half2:
            return { 2, GL_HALF_FLOAT, GL_FALSE, false };
        case VertexFormat::Half4:
            return { 4, GL_HALF_FLOAT, GL_FALSE, false };
        case VertexFormat::UByte4Norm:
            return { 4, GL_UNSIGNED_BYTE, GL_TRUE, false };
        case VertexFormat::Byte4Norm:
            return { 4, GL_BYTE, GL_TRUE, false };
        case VertexFormat::UShort2Norm:
            return { 2, GL_UNSIGNED_SHORT, GL_TRUE, false };
        case VertexFormat::UShort4Norm:
            return { 4, GL_UNSIGNED_SHORT, GL_TRUE, false };
        case VertexFormat::Short2Norm:
            return { 2, GL_SHORT, GL_TRUE, false };
        case VertexFormat::Short4Norm:
            return { 4, GL_SHORT, GL_TRUE, false };
        case VertexFormat::Int1010102Norm:
            return { 4, GL_INT_2_10_10_10_REV, GL_TRUE, false };
        case VertexFormat::UByte4:
            return { 4, GL_UNSIGNED_BYTE, GL_FALSE, true };
        case VertexFormat::UShort4:
            return { 4, GL_UNSIGNED_SHORT, GL_FALSE, true };
    }
    return { 4, GL_FLOAT, GL_FALSE, false };
}

GLenum toGLTopology(Topology topology)
{
    switch (topology)
    {
        case Topology::Triangles:
            return GL_TRIANGLES;
        case Topology::TriangleStrip:
            return GL_TRIANGLE_STRIP;
        case Topology::Lines:
            return GL_LINES;
        case Topology::LineStrip:
            return GL_LINE_STRIP;
        case Topology::Points:
            return GL_POINTS;
    }
    return GL_TRIANGLES;
}

GLenum toGLUpdate(BufferUpdate update)
{
    switch (update)
    {
        case BufferUpdate::Static:
            return GL_STATIC_DRAW;
        case BufferUpdate::Dynamic:
            return GL_DYNAMIC_DRAW;
        case BufferUpdate::Stream:
            return GL_STREAM_DRAW;
    }
    return GL_STATIC_DRAW;
}

GLenum toGLCompare(CompareOp op)
{
    switch (op)
    {
        case CompareOp::Never:
            return GL_NEVER;
        case CompareOp::Less:
            return GL_LESS;
        case CompareOp::Equal:
            return GL_EQUAL;
        case CompareOp::LessEqual:
            return GL_LEQUAL;
        case CompareOp::Greater:
            return GL_GREATER;
        case CompareOp::NotEqual:
            return GL_NOTEQUAL;
        case CompareOp::GreaterEqual:
            return GL_GEQUAL;
        case CompareOp::Always:
            return GL_ALWAYS;
    }
    return GL_LESS;
}

GLenum toGLBlendFactor(BlendFactor factor)
{
    switch (factor)
    {
        case BlendFactor::Zero:
            return GL_ZERO;
        case BlendFactor::One:
            return GL_ONE;
        case BlendFactor::SrcColor:
            return GL_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:
            return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::SrcAlpha:
            return GL_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:
            return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstColor:
            return GL_DST_COLOR;
        case BlendFactor::OneMinusDstColor:
            return GL_ONE_MINUS_DST_COLOR;
        case BlendFactor::DstAlpha:
            return GL_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:
            return GL_ONE_MINUS_DST_ALPHA;
    }
    return GL_ONE;
}

class GLDriver final : public Driver
{
public:
    GLDriver(const GLPlatform& platform, void (*log)(const char*), bool debug, int major, int minor)
            : platform_(platform),
              log_(log),
              debug_(debug && glDebugMessageCallback != nullptr)
    {
        if (debug_)
        {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(&GLDriver::debugMessage, this);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0,
                    nullptr, GL_FALSE);
        }

        GLint value = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
        caps_.maxTextureSize = static_cast<std::uint32_t>(value);
        glGetIntegerv(GL_MAX_DRAW_BUFFERS, &value);
        caps_.maxColorTargets = static_cast<std::uint32_t>(value);
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &value);
        caps_.uniformBufferOffsetAlignment = static_cast<std::uint32_t>(value);
#ifdef PRISMA_GLES
        caps_.gles = true;
        caps_.compute = major > 3 || (major == 3 && minor >= 1);
#else
        caps_.compute = true;
#endif
        caps_.versionMajor = static_cast<std::uint32_t>(major);
        caps_.versionMinor = static_cast<std::uint32_t>(minor);
        caps_.debugOutput = debug_;
#ifdef PRISMA_GLES
        caps_.floatColorTargets = glESExt::EXT_color_buffer_float;
#else
        caps_.floatColorTargets = true;
#endif

        glGenVertexArrays(1, &scratchVertexArray_);
#ifndef PRISMA_GLES
        glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
#endif
    }

    ~GLDriver() override
    {
        if (debug_) glDebugMessageCallback(nullptr, nullptr);
        for (GLPipeline& pipeline: pipelines_)
        {
            glDeleteProgram(pipeline.program);
            glDeleteVertexArrays(1, &pipeline.vertexArray);
        }
        for (GLShader& shader: shaders_) glDeleteShader(shader.id);
        for (std::size_t i = 0; i < framebuffers_.size(); ++i)
            glDeleteFramebuffers(1, &framebuffers_[i].id);
        for (GLTexture& texture: textures_) glDeleteTextures(1, &texture.id);
        for (GLSampler& sampler: samplers_) glDeleteSamplers(1, &sampler.id);
        for (GLBuffer& buffer: buffers_) glDeleteBuffers(1, &buffer.id);
        glDeleteVertexArrays(1, &scratchVertexArray_);
    }

    DriverType type() const override { return DriverType::OpenGL; }
    const Caps& caps() const override { return caps_; }

    BufferHandle createBuffer(const BufferDesc& desc) override
    {
        if (desc.size == 0) return BufferHandle();

        GLBuffer buffer;
        buffer.size = desc.size;
        buffer.usage = desc.usage;
        buffer.indexFormat = desc.indexFormat;
        glGenBuffers(1, &buffer.id);
        const GLenum target = bindForEdit(buffer);
        glBufferData(target, desc.size, desc.data, toGLUpdate(desc.update));
        label(GL_BUFFER, buffer.id, desc.debugName);
        return handleCast<BufferHandle>(buffers_.insert(buffer));
    }

    void updateBuffer(BufferHandle handle, std::uint32_t offset, const void* data,
            std::uint32_t size) override
    {
        if (passActive_)
        {
            log("updateBuffer: not allowed inside a render pass");
            return;
        }
        const GLBuffer* buffer = buffers_.get(handleCast<BufferSlot>(handle));
        if (!buffer || !data || static_cast<std::uint64_t>(offset) + size > buffer->size)
        {
            log("updateBuffer: invalid buffer handle or range");
            return;
        }
        const GLenum target = bindForEdit(*buffer);
        glBufferSubData(target, offset, size, data);
    }

    ShaderHandle createShader(const ShaderDesc& desc) override
    {
        if (!desc.source) return ShaderHandle();

        GLShader shader;
        shader.id = glCreateShader(
                desc.stage == ShaderStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
        label(GL_SHADER, shader.id, desc.debugName);
        const char* source = desc.source;
#ifdef PRISMA_GLES
        if (desc.stage == ShaderStage::Vertex)
        {
            GLint versionLine = 0;
            if (strncmp(source, "#version", 8) == 0)
            {
                const char* end = strchr(source, '\n');
                versionLine = static_cast<GLint>(end ? end - source + 1 : strlen(source));
            }
            const char* parts[4] = {
                source,
                versionLine > 0 ? "#define main prismaUserMain\n#line 2\n"
                                : "#define main prismaUserMain\n#line 1\n",
                source + versionLine,
                "\n#undef main\nvoid main()\n{\n    prismaUserMain();\n"
                "    gl_Position.z = 2.0 * gl_Position.z - gl_Position.w;\n}\n",
            };
            const GLint lengths[4] = { versionLine, -1, -1, -1 };
            glShaderSource(shader.id, 4, parts, lengths);
        }
        else
#endif
            glShaderSource(shader.id, 1, &source, nullptr);
        glCompileShader(shader.id);

        GLint compiled = GL_FALSE;
        glGetShaderiv(shader.id, GL_COMPILE_STATUS, &compiled);
        if (!compiled)
        {
            char message[1024];
            glGetShaderInfoLog(shader.id, sizeof(message), nullptr, message);
            log(message);
            glDeleteShader(shader.id);
            return ShaderHandle();
        }
        return handleCast<ShaderHandle>(shaders_.insert(shader));
    }

    TextureHandle createTexture(const TextureDesc& desc) override
    {
        if (desc.format == TextureFormat::None || desc.width == 0 || desc.height == 0 ||
                desc.width > caps_.maxTextureSize || desc.height > caps_.maxTextureSize)
        {
            log("createTexture: invalid size");
            return TextureHandle();
        }

        const GLFormat format = toGLFormat(desc.format);
        const std::uint32_t fullChain = fullMipCount(desc.width, desc.height);
        std::uint32_t levels = desc.mipLevels == 0 ? fullChain : desc.mipLevels;
        if (levels > fullChain) levels = fullChain;

        GLTexture texture;
        texture.width = desc.width;
        texture.height = desc.height;
        texture.format = desc.format;
        texture.usage = desc.usage;
        glGenTextures(1, &texture.id);
        state_.bindTexture(0, GL_TEXTURE_2D, texture.id);
        glTexStorage2D(GL_TEXTURE_2D, static_cast<GLsizei>(levels), format.internal,
                static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height));
        if (desc.data && !isDepthFormat(desc.format))
        {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(desc.width),
                    static_cast<GLsizei>(desc.height), format.format, format.type, desc.data);
            if (desc.generateMipmaps && levels > 1) glGenerateMipmap(GL_TEXTURE_2D);
        }
        label(GL_TEXTURE, texture.id, desc.debugName);
        return handleCast<TextureHandle>(textures_.insert(texture));
    }

    SamplerHandle createSampler(const SamplerDesc& desc) override
    {
        GLSampler sampler;
        glGenSamplers(1, &sampler.id);
        glSamplerParameteri(sampler.id, GL_TEXTURE_MIN_FILTER,
                toGLMinFilter(desc.minFilter, desc.mipFilter));
        glSamplerParameteri(sampler.id, GL_TEXTURE_MAG_FILTER,
                desc.magFilter == Filter::Linear ? GL_LINEAR : GL_NEAREST);
        glSamplerParameteri(sampler.id, GL_TEXTURE_WRAP_S, toGLAddress(desc.addressU));
        glSamplerParameteri(sampler.id, GL_TEXTURE_WRAP_T, toGLAddress(desc.addressV));
        label(GL_SAMPLER, sampler.id, desc.debugName);
        return handleCast<SamplerHandle>(samplers_.insert(sampler));
    }

    PipelineHandle createPipeline(const PipelineDesc& desc) override
    {
        const GLShader* vertex = shaders_.get(handleCast<ShaderSlot>(desc.vertexShader));
        const GLShader* fragment = shaders_.get(handleCast<ShaderSlot>(desc.fragmentShader));
        if (!vertex || !fragment || desc.attributeCount > PipelineDesc::kMaxAttributes ||
                desc.uniformBlockCount > PipelineDesc::kMaxUniformBlocks ||
                desc.textureCount > PipelineDesc::kMaxTextures ||
                desc.targets.colorCount > TargetFormats::kMaxColors || !validVertexInput(desc))
        {
            log("createPipeline: invalid shader handle or too many attributes or uniform blocks");
            return PipelineHandle();
        }

        GLPipeline pipeline;
        pipeline.program = glCreateProgram();
        glAttachShader(pipeline.program, vertex->id);
        glAttachShader(pipeline.program, fragment->id);
        glLinkProgram(pipeline.program);
        glDetachShader(pipeline.program, vertex->id);
        glDetachShader(pipeline.program, fragment->id);

        GLint linked = GL_FALSE;
        glGetProgramiv(pipeline.program, GL_LINK_STATUS, &linked);
        if (!linked)
        {
            char message[1024];
            glGetProgramInfoLog(pipeline.program, sizeof(message), nullptr, message);
            log(message);
            glDeleteProgram(pipeline.program);
            return PipelineHandle();
        }

        pipeline.topology = toGLTopology(desc.topology);
        pipeline.vertexBufferCount = desc.vertexBufferCount;
        for (std::uint32_t i = 0; i < desc.vertexBufferCount; ++i)
            pipeline.vertexBuffers[i] = desc.vertexBuffers[i];
        pipeline.attributeCount = desc.attributeCount;
        pipeline.targets = desc.targets;
        pipeline.depthTest = desc.depthTest;
        pipeline.depthWrite = desc.depthWrite;
        pipeline.depthFunc = toGLCompare(desc.depthCompare);
        pipeline.cull = desc.cullMode != CullMode::None;
        pipeline.cullFace = desc.cullMode == CullMode::Front ? GL_FRONT : GL_BACK;
        pipeline.frontFace = desc.frontFace == FrontFace::Clockwise ? GL_CW : GL_CCW;
        pipeline.blend = desc.blend;
        pipeline.blendFunc[0] = toGLBlendFactor(desc.srcColor);
        pipeline.blendFunc[1] = toGLBlendFactor(desc.dstColor);
        pipeline.blendFunc[2] = toGLBlendFactor(desc.srcAlpha);
        pipeline.blendFunc[3] = toGLBlendFactor(desc.dstAlpha);

        for (std::uint32_t i = 0; i < desc.uniformBlockCount; ++i)
        {
            const UniformBlockBinding& block = desc.uniformBlocks[i];
            const GLuint index = glGetUniformBlockIndex(pipeline.program, block.name);
            if (index == GL_INVALID_INDEX)
            {
                log("createPipeline: uniform block not found in the shaders");
                continue;
            }
            glUniformBlockBinding(pipeline.program, index, block.slot);
        }

        if (desc.textureCount > 0) state_.useProgram(pipeline.program);
        for (std::uint32_t i = 0; i < desc.textureCount; ++i)
        {
            const TextureBinding& binding = desc.textures[i];
            const GLint location = glGetUniformLocation(pipeline.program, binding.name);
            if (location < 0)
            {
                log("createPipeline: texture not found in the shaders");
                continue;
            }
            glUniform1i(location, static_cast<GLint>(binding.slot));
        }
        glGenVertexArrays(1, &pipeline.vertexArray);
        state_.bindVertexArray(pipeline.vertexArray);
        label(GL_PROGRAM, pipeline.program, desc.debugName);
        for (std::uint32_t i = 0; i < desc.attributeCount; ++i)
        {
            const VertexAttribute& attribute = desc.attributes[i];
            pipeline.attributes[i] = attribute;
            glEnableVertexAttribArray(attribute.location);
            glVertexAttribDivisor(attribute.location,
                    desc.vertexBuffers[attribute.buffer].step == VertexStep::Instance ? 1 : 0);
        }
        return handleCast<PipelineHandle>(pipelines_.insert(pipeline));
    }

    void destroy(BufferHandle handle) override
    {
        const BufferSlot slot = handleCast<BufferSlot>(handle);
        const GLBuffer* buffer = buffers_.get(slot);
        if (!buffer) return;
        glDeleteBuffers(1, &buffer->id);
        state_.bufferDeleted(buffer->id);
        buffers_.erase(slot);
    }

    void destroy(ShaderHandle handle) override
    {
        const ShaderSlot slot = handleCast<ShaderSlot>(handle);
        const GLShader* shader = shaders_.get(slot);
        if (!shader) return;
        glDeleteShader(shader->id);
        shaders_.erase(slot);
    }

    void destroy(PipelineHandle handle) override
    {
        const PipelineSlot slot = handleCast<PipelineSlot>(handle);
        const GLPipeline* pipeline = pipelines_.get(slot);
        if (!pipeline) return;
        glDeleteProgram(pipeline->program);
        glDeleteVertexArrays(1, &pipeline->vertexArray);
        state_.programDeleted(pipeline->program);
        state_.vertexArrayDeleted(pipeline->vertexArray);
        pipelines_.erase(slot);
    }

    void destroy(TextureHandle handle) override
    {
        const TextureSlot slot = handleCast<TextureSlot>(handle);
        const GLTexture* texture = textures_.get(slot);
        if (!texture) return;
        for (std::size_t i = framebuffers_.size(); i > 0; --i)
        {
            if (!framebufferUses(framebuffers_[i - 1], handle.bits())) continue;
            glDeleteFramebuffers(1, &framebuffers_[i - 1].id);
            state_.framebufferDeleted(framebuffers_[i - 1].id);
            framebuffers_.erase(framebuffers_.begin() + (i - 1));
        }
        glDeleteTextures(1, &texture->id);
        state_.textureDeleted(texture->id);
        textures_.erase(slot);
    }

    void destroy(SamplerHandle handle) override
    {
        const SamplerSlot slot = handleCast<SamplerSlot>(handle);
        const GLSampler* sampler = samplers_.get(slot);
        if (!sampler) return;
        glDeleteSamplers(1, &sampler->id);
        state_.samplerDeleted(sampler->id);
        samplers_.erase(slot);
    }

    void beginFrame() override {}

    void beginRenderPass(const RenderPassDesc& desc) override
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        passActive_ = false;
        passOffscreen_ = desc.colorCount > 0 || desc.depth.valid();
        passColorCount_ = desc.colorCount;
        passHasDepth_ = true;
        passStencil_ = false;
        passColorStore_ = desc.colorStore;
        passDepthStore_ = desc.depthStore;

        if (passOffscreen_)
        {
            const GLFramebuffer* framebuffer = findFramebuffer(desc);
            if (!framebuffer) framebuffer = createFramebuffer(desc);
            if (!framebuffer)
            {
                log("beginRenderPass: invalid or unsupported render targets");
                return;
            }
            state_.bindFramebuffer(framebuffer->id);
            width = framebuffer->width;
            height = framebuffer->height;
            passHasDepth_ = desc.depth.valid();
            passStencil_ = framebuffer->stencil;
            passFormats_ = framebuffer->formats;
        }
        else
        {
            platform_.framebufferSize(platform_.user, &width, &height);
            state_.bindFramebuffer(0);
            passFormats_ = TargetFormats();
        }
        passActive_ = true;
        pipeline_ = PipelineHandle();
        for (std::uint32_t i = 0; i < PipelineDesc::kMaxVertexBuffers; ++i)
        {
            vertexBuffers_[i] = BufferHandle();
            vertexOffsets_[i] = 0;
        }
        indexBuffer_ = BufferHandle();
        vertexDirty_ = true;
        indexDirty_ = true;
        state_.framebufferSrgb(passOffscreen_);
        passWidth_ = width;
        passHeight_ = height;
        state_.viewport(0, 0, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
        state_.depthRange(0.0f, 1.0f);
        state_.scissorTest(false);

        GLbitfield mask = 0;
        if (desc.colorLoad == LoadOp::Clear && (!passOffscreen_ || desc.colorCount > 0))
        {
            state_.clearColor(desc.clearColor);
            mask |= GL_COLOR_BUFFER_BIT;
        }
        if (desc.depthLoad == LoadOp::Clear && passHasDepth_)
        {
            state_.depthMask(true);
            state_.clearDepth(desc.clearDepth);
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (mask) glClear(mask);
    }

    void setViewport(const Viewport& viewport) override
    {
        if (!passActive_) return;
        const std::int32_t width = static_cast<std::int32_t>(viewport.width + 0.5f);
        const std::int32_t height = static_cast<std::int32_t>(viewport.height + 0.5f);
        const std::int32_t x = static_cast<std::int32_t>(viewport.x + 0.5f);
        const std::int32_t top = static_cast<std::int32_t>(viewport.y + 0.5f);
        state_.viewport(x, static_cast<std::int32_t>(passHeight_) - (top + height), width, height);
        state_.depthRange(viewport.minDepth, viewport.maxDepth);
    }

    void setScissor(const Rect& rect) override
    {
        if (!passActive_) return;
        const bool whole = rect.x <= 0 && rect.y <= 0 &&
                           rect.x + static_cast<std::int64_t>(rect.width) >= passWidth_ &&
                           rect.y + static_cast<std::int64_t>(rect.height) >= passHeight_;
        state_.scissorTest(!whole);
        if (whole) return;
        const std::int32_t height = static_cast<std::int32_t>(rect.height);
        state_.scissor(rect.x, static_cast<std::int32_t>(passHeight_) - (rect.y + height),
                static_cast<std::int32_t>(rect.width), height);
    }

    void bindPipeline(PipelineHandle handle) override
    {
        pipeline_ = handle;
        vertexDirty_ = true;
        indexDirty_ = true;
    }

    void bindVertexBuffer(std::uint32_t slot, BufferHandle handle, std::uint32_t offset) override
    {
        if (slot >= PipelineDesc::kMaxVertexBuffers)
        {
            log("bindVertexBuffer: slot out of range");
            return;
        }
        vertexBuffers_[slot] = handle;
        vertexOffsets_[slot] = offset;
        vertexDirty_ = true;
    }

    void bindIndexBuffer(BufferHandle handle) override
    {
        indexBuffer_ = handle;
        indexDirty_ = true;
    }

    void bindUniformBuffer(std::uint32_t slot, BufferHandle handle, std::uint32_t offset,
            std::uint32_t size) override
    {
        const GLBuffer* buffer = buffers_.get(handleCast<BufferSlot>(handle));
        if (!buffer || buffer->usage != BufferUsage::Uniform || size == 0 ||
                static_cast<std::uint64_t>(offset) + size > buffer->size ||
                slot >= GLState::kMaxUniformSlots)
        {
            log("bindUniformBuffer: invalid buffer handle, range or slot");
            return;
        }
        state_.bindUniformBufferRange(slot, buffer->id, offset, size);
    }

    void bindTexture(std::uint32_t slot, TextureHandle textureHandle,
            SamplerHandle samplerHandle) override
    {
        const GLTexture* texture = textures_.get(handleCast<TextureSlot>(textureHandle));
        const GLSampler* sampler = samplers_.get(handleCast<SamplerSlot>(samplerHandle));
        if (!texture || !sampler || slot >= GLState::kMaxTextureUnits)
        {
            log("bindTexture: invalid texture handle, sampler handle or slot");
            return;
        }
        state_.bindTexture(slot, GL_TEXTURE_2D, texture->id);
        state_.bindSampler(slot, sampler->id);
    }

    void draw(std::uint32_t vertexCount, std::uint32_t firstVertex,
            std::uint32_t instanceCount) override
    {
        const GLPipeline* pipeline =
                prepareDraw(static_cast<std::uint64_t>(firstVertex) + vertexCount, instanceCount);
        if (!pipeline) return;
        if (instanceCount == 1)
            glDrawArrays(pipeline->topology, static_cast<GLint>(firstVertex),
                    static_cast<GLsizei>(vertexCount));
        else
            glDrawArraysInstanced(pipeline->topology, static_cast<GLint>(firstVertex),
                    static_cast<GLsizei>(vertexCount), static_cast<GLsizei>(instanceCount));
    }

    void drawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex,
            std::uint32_t instanceCount) override
    {
        const GLPipeline* pipeline = prepareDraw(0, instanceCount);
        if (!pipeline) return;

        const GLBuffer* indices = buffers_.get(handleCast<BufferSlot>(indexBuffer_));
        if (!indices || indices->usage != BufferUsage::Index)
        {
            log("drawIndexed: no valid index buffer bound");
            return;
        }
        if (indexDirty_)
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices->id);
            indexDirty_ = false;
        }

        const bool wide = indices->indexFormat == IndexFormat::UInt32;
        const std::size_t indexSize = wide ? 4 : 2;
        const std::size_t offset = static_cast<std::size_t>(firstIndex) * indexSize;
        if (offset + static_cast<std::size_t>(indexCount) * indexSize > indices->size)
        {
            log("drawIndexed: index range is outside the index buffer");
            return;
        }
        const GLenum type = wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
        if (instanceCount == 1)
            glDrawElements(pipeline->topology, static_cast<GLsizei>(indexCount), type,
                    reinterpret_cast<const void*>(offset));
        else
            glDrawElementsInstanced(pipeline->topology, static_cast<GLsizei>(indexCount), type,
                    reinterpret_cast<const void*>(offset), static_cast<GLsizei>(instanceCount));
    }

    void endRenderPass() override
    {
        if (!passActive_) return;
        passActive_ = false;

        GLenum attachments[RenderPassDesc::kMaxColorTargets + 1];
        GLsizei count = 0;
        if (passColorStore_ == StoreOp::Discard)
        {
            if (!passOffscreen_) attachments[count++] = GL_COLOR;
            for (std::uint32_t i = 0; passOffscreen_ && i < passColorCount_; ++i)
                attachments[count++] = GL_COLOR_ATTACHMENT0 + i;
        }
        if (passDepthStore_ == StoreOp::Discard && passHasDepth_)
        {
            if (!passOffscreen_) attachments[count++] = GL_DEPTH;
            else
                attachments[count++] =
                        passStencil_ ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
        }
        if (count > 0) glInvalidateFramebuffer(GL_FRAMEBUFFER, count, attachments);
    }
    void endFrame() override {}

    void present() override { platform_.swapBuffers(platform_.user); }

private:
    using BufferSlot = ct::Handle32<GLBuffer>;
    using ShaderSlot = ct::Handle32<GLShader>;
    using PipelineSlot = ct::Handle32<GLPipeline>;
    using TextureSlot = ct::Handle32<GLTexture>;
    using SamplerSlot = ct::Handle32<GLSampler>;

    static bool framebufferUses(const GLFramebuffer& framebuffer, std::uint32_t texture)
    {
        if (framebuffer.depth == texture) return true;
        for (std::uint32_t i = 0; i < framebuffer.colorCount; ++i)
            if (framebuffer.colors[i] == texture) return true;
        return false;
    }

    const GLFramebuffer* findFramebuffer(const RenderPassDesc& desc) const
    {
        for (std::size_t i = 0; i < framebuffers_.size(); ++i)
        {
            const GLFramebuffer& framebuffer = framebuffers_[i];
            if (framebuffer.colorCount != desc.colorCount || framebuffer.depth != desc.depth.bits())
                continue;
            bool same = true;
            for (std::uint32_t c = 0; c < desc.colorCount; ++c)
                if (framebuffer.colors[c] != desc.colors[c].bits()) same = false;
            if (same) return &framebuffer;
        }
        return nullptr;
    }

    const GLFramebuffer* createFramebuffer(const RenderPassDesc& desc)
    {
        if (desc.colorCount > RenderPassDesc::kMaxColorTargets ||
                desc.colorCount > caps_.maxColorTargets)
            return nullptr;

        GLFramebuffer framebuffer;
        framebuffer.colorCount = desc.colorCount;
        framebuffer.depth = desc.depth.bits();
        framebuffer.formats.window = false;
        framebuffer.formats.colorCount = desc.colorCount;

        const GLTexture* colors[RenderPassDesc::kMaxColorTargets] = {};
        for (std::uint32_t i = 0; i < desc.colorCount; ++i)
        {
            colors[i] = textures_.get(handleCast<TextureSlot>(desc.colors[i]));
            if (!colors[i] || !(colors[i]->usage & kTextureRenderTarget) ||
                    isDepthFormat(colors[i]->format))
                return nullptr;
            framebuffer.colors[i] = desc.colors[i].bits();
            framebuffer.formats.colors[i] = colors[i]->format;
            framebuffer.width = colors[i]->width;
            framebuffer.height = colors[i]->height;
        }

        const GLTexture* depth = nullptr;
        if (desc.depth.valid())
        {
            depth = textures_.get(handleCast<TextureSlot>(desc.depth));
            if (!depth || !(depth->usage & kTextureRenderTarget) || !isDepthFormat(depth->format))
                return nullptr;
            framebuffer.width = depth->width;
            framebuffer.height = depth->height;
            framebuffer.stencil = depth->format == TextureFormat::Depth24Stencil8;
            framebuffer.formats.depth = depth->format;
        }

        glGenFramebuffers(1, &framebuffer.id);
        state_.bindFramebuffer(framebuffer.id);
        GLenum drawBuffers[RenderPassDesc::kMaxColorTargets] = { GL_NONE, GL_NONE, GL_NONE,
            GL_NONE };
        for (std::uint32_t i = 0; i < desc.colorCount; ++i)
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D,
                    colors[i]->id, 0);
            drawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
        }
        if (depth)
            glFramebufferTexture2D(GL_FRAMEBUFFER,
                    framebuffer.stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
                    GL_TEXTURE_2D, depth->id, 0);
        glDrawBuffers(desc.colorCount > 0 ? static_cast<GLsizei>(desc.colorCount) : 1, drawBuffers);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            state_.bindFramebuffer(0);
            glDeleteFramebuffers(1, &framebuffer.id);
            state_.framebufferDeleted(framebuffer.id);
            return nullptr;
        }
        framebuffers_.push_back(framebuffer);
        return &framebuffers_[framebuffers_.size() - 1];
    }

    static bool validVertexInput(const PipelineDesc& desc)
    {
        if (desc.vertexBufferCount > PipelineDesc::kMaxVertexBuffers) return false;
        for (std::uint32_t i = 0; i < desc.attributeCount && i < PipelineDesc::kMaxAttributes; ++i)
            if (desc.attributes[i].buffer >= desc.vertexBufferCount) return false;
        return true;
    }

    GLenum bindForEdit(const GLBuffer& buffer)
    {
        switch (buffer.usage)
        {
            case BufferUsage::Vertex:
                state_.bindArrayBuffer(buffer.id);
                return GL_ARRAY_BUFFER;
            case BufferUsage::Index:
                state_.bindVertexArray(scratchVertexArray_);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.id);
                return GL_ELEMENT_ARRAY_BUFFER;
            case BufferUsage::Uniform:
                state_.bindUniformBuffer(buffer.id);
                return GL_UNIFORM_BUFFER;
        }
        return GL_ARRAY_BUFFER;
    }

    const GLPipeline* prepareDraw(std::uint64_t vertexEnd, std::uint32_t instanceCount)
    {
        const GLPipeline* pipeline = pipelines_.get(handleCast<PipelineSlot>(pipeline_));
        if (!passActive_ || !pipeline || instanceCount == 0)
        {
            log("draw: no active render pass, no pipeline bound in this pass or no instances");
            return nullptr;
        }
        if (!sameTargets(pipeline->targets, passFormats_))
        {
            log("draw: the pipeline was created for different render targets than this pass");
            return nullptr;
        }

        const GLBuffer* buffers[PipelineDesc::kMaxVertexBuffers] = {};
        for (std::uint32_t i = 0; i < pipeline->vertexBufferCount; ++i)
        {
            buffers[i] = buffers_.get(handleCast<BufferSlot>(vertexBuffers_[i]));
            if (!buffers[i] || buffers[i]->usage != BufferUsage::Vertex)
            {
                log("draw: a vertex buffer the pipeline needs is not bound");
                return nullptr;
            }
            const VertexBufferLayout& layout = pipeline->vertexBuffers[i];
            const std::uint64_t count =
                    layout.step == VertexStep::Instance ? instanceCount : vertexEnd;
            if (vertexOffsets_[i] + count * layout.stride > buffers[i]->size)
            {
                log("draw: vertex or instance range is outside the vertex buffer");
                return nullptr;
            }
        }

        state_.useProgram(pipeline->program);
        state_.depthTest(pipeline->depthTest);
        state_.depthMask(pipeline->depthWrite);
        if (pipeline->depthTest) state_.depthFunc(pipeline->depthFunc);
        state_.cullFace(pipeline->cull, pipeline->cullFace);
        state_.frontFace(pipeline->frontFace);
        state_.blend(pipeline->blend);
        if (pipeline->blend)
            state_.blendFunc(pipeline->blendFunc[0], pipeline->blendFunc[1], pipeline->blendFunc[2],
                    pipeline->blendFunc[3]);

        state_.bindVertexArray(pipeline->vertexArray);
        if (vertexDirty_)
        {
            for (std::uint32_t i = 0; i < pipeline->attributeCount; ++i)
            {
                const VertexAttribute& attribute = pipeline->attributes[i];
                const VertexBufferLayout& layout = pipeline->vertexBuffers[attribute.buffer];
                const GLVertexFormat format = toGLVertexFormat(attribute.format);
                const std::size_t offset =
                        static_cast<std::size_t>(vertexOffsets_[attribute.buffer]) +
                        attribute.offset;
                state_.bindArrayBuffer(buffers[attribute.buffer]->id);
                if (format.integer)
                    glVertexAttribIPointer(attribute.location, format.components, format.type,
                            static_cast<GLsizei>(layout.stride),
                            reinterpret_cast<const void*>(offset));
                else
                    glVertexAttribPointer(attribute.location, format.components, format.type,
                            format.normalized, static_cast<GLsizei>(layout.stride),
                            reinterpret_cast<const void*>(offset));
            }
            vertexDirty_ = false;
        }
        return pipeline;
    }

    void log(const char* message) const
    {
        if (log_) log_(message);
    }

    void label(GLenum kind, GLuint id, const char* name) const
    {
        if (debug_ && name) glObjectLabel(kind, id, -1, name);
    }

    static void GLAPIENTRY debugMessage(GLenum, GLenum type, GLuint, GLenum, GLsizei,
            const GLchar* message, const void* user)
    {
        const char* kind = "warning";
        if (type == GL_DEBUG_TYPE_ERROR) kind = "error";
        if (type == GL_DEBUG_TYPE_PERFORMANCE) kind = "performance";

        char text[1200];
        snprintf(text, sizeof(text), "GL %s: %s", kind, message);
        static_cast<const GLDriver*>(user)->log(text);
    }

    GLPlatform platform_;
    void (*log_)(const char*);
    bool debug_;
    GLState state_;
    Caps caps_;
    GLuint scratchVertexArray_ = 0;

    ct::SlotMap32<GLBuffer> buffers_;
    ct::SlotMap32<GLShader> shaders_;
    ct::SlotMap32<GLPipeline> pipelines_;
    ct::SlotMap32<GLTexture> textures_;
    ct::SlotMap32<GLSampler> samplers_;
    ct::Vector<GLFramebuffer> framebuffers_;

    bool passActive_ = false;
    TargetFormats passFormats_;
    std::uint32_t passWidth_ = 0;
    std::uint32_t passHeight_ = 0;
    bool passOffscreen_ = false;
    std::uint32_t passColorCount_ = 0;
    bool passHasDepth_ = false;
    bool passStencil_ = false;
    StoreOp passColorStore_ = StoreOp::Store;
    StoreOp passDepthStore_ = StoreOp::Store;

    PipelineHandle pipeline_;
    BufferHandle vertexBuffers_[PipelineDesc::kMaxVertexBuffers];
    std::uint32_t vertexOffsets_[PipelineDesc::kMaxVertexBuffers] = {};
    BufferHandle indexBuffer_;
    bool vertexDirty_ = true;
    bool indexDirty_ = true;
};

} // namespace

Driver* createGLDriver(const DriverDesc& desc, DriverError* error)
{
    const GLPlatform* gl = desc.gl;
    if (!gl || !gl->makeCurrent || !gl->swapBuffers || !gl->framebufferSize)
    {
        *error = DriverError::MissingPlatform;
        return nullptr;
    }
    if (!gl->makeCurrent(gl->user))
    {
        *error = DriverError::ContextFailed;
        return nullptr;
    }
#ifdef PRISMA_GLES
    glesSetProcAddressLoader(gl->getProcAddress);
    const bool loaded = initOpenGLExtensions();
    const int major = glESExt::majorVersion;
    const int minor = glESExt::minorVersion;
    const bool versionOk = major >= 3;
#else
    const bool loaded = initOpenGLExtensions(false);
    const int major = glExt::majorVersion;
    const int minor = glExt::minorVersion;
    const bool versionOk = major > 4 || (major == 4 && minor >= 6);
#endif
    if (!loaded)
    {
        *error = DriverError::LoaderFailed;
        return nullptr;
    }
    if (!versionOk)
    {
        *error = DriverError::VersionTooLow;
        return nullptr;
    }
    *error = DriverError::None;
    return new GLDriver(*gl, desc.log, desc.debug, major, minor);
}

} // namespace prisma
