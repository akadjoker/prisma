#include "prisma/rhi/gl/GLState.h"

#include "OpenGL.h"

namespace prisma
{

void GLState::reset() { known_ = 0; }

void GLState::bindFramebuffer(std::uint32_t framebuffer)
{
    if ((known_ & kFramebuffer) && framebuffer_ == framebuffer) return;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    framebuffer_ = framebuffer;
    known_ |= kFramebuffer;
}

void GLState::viewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
{
    if ((known_ & kViewport) && viewport_[0] == x && viewport_[1] == y && viewport_[2] == width &&
            viewport_[3] == height)
        return;
    glViewport(x, y, width, height);
    viewport_[0] = x;
    viewport_[1] = y;
    viewport_[2] = width;
    viewport_[3] = height;
    known_ |= kViewport;
}

void GLState::clearColor(const float color[4])
{
    if ((known_ & kClearColor) && clearColor_[0] == color[0] && clearColor_[1] == color[1] &&
            clearColor_[2] == color[2] && clearColor_[3] == color[3])
        return;
    glClearColor(color[0], color[1], color[2], color[3]);
    for (int i = 0; i < 4; ++i) clearColor_[i] = color[i];
    known_ |= kClearColor;
}

void GLState::clearDepth(float depth)
{
    if ((known_ & kClearDepth) && clearDepth_ == depth) return;
    glClearDepth(depth);
    clearDepth_ = depth;
    known_ |= kClearDepth;
}

} // namespace prisma
