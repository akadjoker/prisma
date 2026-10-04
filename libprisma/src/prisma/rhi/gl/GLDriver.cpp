#include "prisma/rhi/Driver.h"
#include "prisma/rhi/gl/GLState.h"

#include "OpenGL.h"

namespace prisma
{

namespace
{

class GLDriver final : public Driver
{
public:
    explicit GLDriver(const GLPlatform& platform)
            : platform_(platform)
    {
        GLint value = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
        caps_.maxTextureSize = static_cast<std::uint32_t>(value);
        glGetIntegerv(GL_MAX_DRAW_BUFFERS, &value);
        caps_.maxColorTargets = static_cast<std::uint32_t>(value);
        caps_.compute = true;
    }

    DriverType type() const override { return DriverType::OpenGL; }
    const Caps& caps() const override { return caps_; }

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

    void endRenderPass() override {}
    void endFrame() override {}

    void present() override { platform_.swapBuffers(platform_.user); }

private:
    GLPlatform platform_;
    GLState state_;
    Caps caps_;
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
    return new GLDriver(*gl);
}

} // namespace prisma
