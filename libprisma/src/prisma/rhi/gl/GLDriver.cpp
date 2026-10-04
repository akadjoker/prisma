#include "prisma/rhi/Driver.h"
#include "prisma/rhi/HandleCast.h"
#include "prisma/rhi/gl/GL.h"
#include "prisma/rhi/gl/GLState.h"

#include <stdio.h>

namespace prisma
{

namespace
{

struct GLBuffer
{
    GLuint id = 0;
    std::uint32_t size = 0;
    BufferUsage usage = BufferUsage::Vertex;
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
};

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
        case TextureFormat::R8:
            return { GL_R8, GL_RED, GL_UNSIGNED_BYTE };
        case TextureFormat::RG8:
            return { GL_RG8, GL_RG, GL_UNSIGNED_BYTE };
        case TextureFormat::RGBA8:
            return { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE };
        case TextureFormat::RGBA8Srgb:
            return { GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE };
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
    std::uint32_t vertexStride = 0;
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
};

GLint componentCount(VertexFormat format)
{
    switch (format)
    {
        case VertexFormat::Float2:
            return 2;
        case VertexFormat::Float3:
            return 3;
        case VertexFormat::Float4:
            return 4;
    }
    return 0;
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

        glGenVertexArrays(1, &scratchVertexArray_);
    }

    ~GLDriver() override
    {
        for (GLPipeline& pipeline: pipelines_)
        {
            glDeleteProgram(pipeline.program);
            glDeleteVertexArrays(1, &pipeline.vertexArray);
        }
        for (GLShader& shader: shaders_) glDeleteShader(shader.id);
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
        glGenBuffers(1, &buffer.id);
        const GLenum target = bindForEdit(buffer);
        glBufferData(target, desc.size, desc.data, toGLUpdate(desc.update));
        label(GL_BUFFER, buffer.id, desc.debugName);
        return handleCast<BufferHandle>(buffers_.insert(buffer));
    }

    void updateBuffer(BufferHandle handle, std::uint32_t offset, const void* data,
            std::uint32_t size) override
    {
        const GLBuffer* buffer = buffers_.get(handleCast<BufferSlot>(handle));
        if (!buffer || !data || offset + size > buffer->size)
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
        if (desc.width == 0 || desc.height == 0 || desc.width > caps_.maxTextureSize ||
                desc.height > caps_.maxTextureSize)
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
        glGenTextures(1, &texture.id);
        state_.bindTexture(0, GL_TEXTURE_2D, texture.id);
        glTexStorage2D(GL_TEXTURE_2D, static_cast<GLsizei>(levels), format.internal,
                static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height));
        if (desc.data)
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
                desc.textureCount > PipelineDesc::kMaxTextures)
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
        pipeline.vertexStride = desc.vertexStride;
        pipeline.attributeCount = desc.attributeCount;
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
            pipeline.attributes[i] = desc.attributes[i];
            glEnableVertexAttribArray(desc.attributes[i].location);
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
        platform_.framebufferSize(platform_.user, &width, &height);

        state_.bindFramebuffer(0);
        state_.viewport(0, 0, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));

        GLbitfield mask = 0;
        if (desc.colorLoad == LoadOp::Clear)
        {
            state_.clearColor(desc.clearColor);
            mask |= GL_COLOR_BUFFER_BIT;
        }
        if (desc.depthLoad == LoadOp::Clear)
        {
            state_.depthMask(true);
            state_.clearDepth(desc.clearDepth);
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (mask) glClear(mask);
    }

    void bindPipeline(PipelineHandle handle) override
    {
        pipeline_ = handle;
        vertexDirty_ = true;
        indexDirty_ = true;
    }

    void bindVertexBuffer(BufferHandle handle, std::uint32_t offset) override
    {
        vertexBuffer_ = handle;
        vertexOffset_ = offset;
        vertexDirty_ = true;
    }

    void bindIndexBuffer(BufferHandle handle, IndexFormat format) override
    {
        indexBuffer_ = handle;
        indexFormat_ = format;
        indexDirty_ = true;
    }

    void bindUniformBuffer(std::uint32_t slot, BufferHandle handle, std::uint32_t offset,
            std::uint32_t size) override
    {
        const GLBuffer* buffer = buffers_.get(handleCast<BufferSlot>(handle));
        if (!buffer || buffer->usage != BufferUsage::Uniform || offset + size > buffer->size ||
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

    void draw(std::uint32_t vertexCount, std::uint32_t firstVertex) override
    {
        const GLPipeline* pipeline = prepareDraw();
        if (!pipeline) return;
        glDrawArrays(pipeline->topology, static_cast<GLint>(firstVertex),
                static_cast<GLsizei>(vertexCount));
    }

    void drawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex) override
    {
        const GLPipeline* pipeline = prepareDraw();
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

        const bool wide = indexFormat_ == IndexFormat::UInt32;
        const std::size_t offset = static_cast<std::size_t>(firstIndex) * (wide ? 4 : 2);
        glDrawElements(pipeline->topology, static_cast<GLsizei>(indexCount),
                wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, reinterpret_cast<const void*>(offset));
    }

    void endRenderPass() override {}
    void endFrame() override {}

    void present() override { platform_.swapBuffers(platform_.user); }

private:
    using BufferSlot = ct::Handle32<GLBuffer>;
    using ShaderSlot = ct::Handle32<GLShader>;
    using PipelineSlot = ct::Handle32<GLPipeline>;
    using TextureSlot = ct::Handle32<GLTexture>;
    using SamplerSlot = ct::Handle32<GLSampler>;

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

    const GLPipeline* prepareDraw()
    {
        const GLPipeline* pipeline = pipelines_.get(handleCast<PipelineSlot>(pipeline_));
        const GLBuffer* buffer = buffers_.get(handleCast<BufferSlot>(vertexBuffer_));
        if (!pipeline || !buffer || buffer->usage != BufferUsage::Vertex)
        {
            log("draw: no valid pipeline or vertex buffer bound");
            return nullptr;
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
            state_.bindArrayBuffer(buffer->id);
            for (std::uint32_t i = 0; i < pipeline->attributeCount; ++i)
            {
                const VertexAttribute& attribute = pipeline->attributes[i];
                const std::size_t offset =
                        static_cast<std::size_t>(vertexOffset_) + attribute.offset;
                glVertexAttribPointer(attribute.location, componentCount(attribute.format),
                        GL_FLOAT, GL_FALSE, static_cast<GLsizei>(pipeline->vertexStride),
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

    PipelineHandle pipeline_;
    BufferHandle vertexBuffer_;
    BufferHandle indexBuffer_;
    IndexFormat indexFormat_ = IndexFormat::UInt16;
    std::uint32_t vertexOffset_ = 0;
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
