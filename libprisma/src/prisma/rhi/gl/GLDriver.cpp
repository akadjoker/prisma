#include "prisma/rhi/Driver.h"
#include "prisma/rhi/HandleCast.h"
#include "prisma/rhi/gl/GLState.h"

#include "OpenGL.h"

namespace prisma
{

namespace
{

struct GLBuffer
{
    GLuint id = 0;
    std::uint32_t size = 0;
};

struct GLShader
{
    GLuint id = 0;
};

struct GLPipeline
{
    GLuint program = 0;
    GLuint vertexArray = 0;
    GLenum topology = GL_TRIANGLES;
    std::uint32_t vertexStride = 0;
    std::uint32_t attributeCount = 0;
    VertexAttribute attributes[PipelineDesc::kMaxAttributes];
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

class GLDriver final : public Driver
{
public:
    GLDriver(const GLPlatform& platform, void (*log)(const char*))
            : platform_(platform),
              log_(log)
    {
        GLint value = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
        caps_.maxTextureSize = static_cast<std::uint32_t>(value);
        glGetIntegerv(GL_MAX_DRAW_BUFFERS, &value);
        caps_.maxColorTargets = static_cast<std::uint32_t>(value);
        caps_.compute = true;
    }

    ~GLDriver() override
    {
        for (GLPipeline& pipeline: pipelines_)
        {
            glDeleteProgram(pipeline.program);
            glDeleteVertexArrays(1, &pipeline.vertexArray);
        }
        for (GLShader& shader: shaders_) glDeleteShader(shader.id);
        for (GLBuffer& buffer: buffers_) glDeleteBuffers(1, &buffer.id);
    }

    DriverType type() const override { return DriverType::OpenGL; }
    const Caps& caps() const override { return caps_; }

    BufferHandle createBuffer(const BufferDesc& desc) override
    {
        if (desc.size == 0) return BufferHandle();

        GLBuffer buffer;
        buffer.size = desc.size;
        glGenBuffers(1, &buffer.id);
        state_.bindArrayBuffer(buffer.id);
        glBufferData(GL_ARRAY_BUFFER, desc.size, desc.data, GL_STATIC_DRAW);
        return handleCast<BufferHandle>(buffers_.insert(buffer));
    }

    ShaderHandle createShader(const ShaderDesc& desc) override
    {
        if (!desc.source) return ShaderHandle();

        GLShader shader;
        shader.id = glCreateShader(
                desc.stage == ShaderStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
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

    PipelineHandle createPipeline(const PipelineDesc& desc) override
    {
        const GLShader* vertex = shaders_.get(handleCast<ShaderSlot>(desc.vertexShader));
        const GLShader* fragment = shaders_.get(handleCast<ShaderSlot>(desc.fragmentShader));
        if (!vertex || !fragment || desc.attributeCount > PipelineDesc::kMaxAttributes)
        {
            log("createPipeline: invalid shader handle or too many attributes");
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
        glGenVertexArrays(1, &pipeline.vertexArray);
        state_.bindVertexArray(pipeline.vertexArray);
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
        state_.arrayBufferDeleted(buffer->id);
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
            state_.clearDepth(desc.clearDepth);
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (mask) glClear(mask);
    }

    void bindPipeline(PipelineHandle handle) override
    {
        pipeline_ = handle;
        vertexDirty_ = true;
    }

    void bindVertexBuffer(BufferHandle handle, std::uint32_t offset) override
    {
        vertexBuffer_ = handle;
        vertexOffset_ = offset;
        vertexDirty_ = true;
    }

    void draw(std::uint32_t vertexCount, std::uint32_t firstVertex) override
    {
        const GLPipeline* pipeline = pipelines_.get(handleCast<PipelineSlot>(pipeline_));
        const GLBuffer* buffer = buffers_.get(handleCast<BufferSlot>(vertexBuffer_));
        if (!pipeline || !buffer)
        {
            log("draw: no valid pipeline or vertex buffer bound");
            return;
        }

        state_.useProgram(pipeline->program);
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
        glDrawArrays(pipeline->topology, static_cast<GLint>(firstVertex),
                static_cast<GLsizei>(vertexCount));
    }

    void endRenderPass() override {}
    void endFrame() override {}

    void present() override { platform_.swapBuffers(platform_.user); }

private:
    using BufferSlot = ct::Handle32<GLBuffer>;
    using ShaderSlot = ct::Handle32<GLShader>;
    using PipelineSlot = ct::Handle32<GLPipeline>;

    void log(const char* message) const
    {
        if (log_) log_(message);
    }

    GLPlatform platform_;
    void (*log_)(const char*);
    GLState state_;
    Caps caps_;

    ct::SlotMap32<GLBuffer> buffers_;
    ct::SlotMap32<GLShader> shaders_;
    ct::SlotMap32<GLPipeline> pipelines_;

    PipelineHandle pipeline_;
    BufferHandle vertexBuffer_;
    std::uint32_t vertexOffset_ = 0;
    bool vertexDirty_ = true;
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
    if (!initOpenGLExtensions(false))
    {
        *error = DriverError::LoaderFailed;
        return nullptr;
    }
    if (glExt::majorVersion < 4 || (glExt::majorVersion == 4 && glExt::minorVersion < 6))
    {
        *error = DriverError::VersionTooLow;
        return nullptr;
    }
    *error = DriverError::None;
    return new GLDriver(*gl, desc.log);
}

} // namespace prisma
