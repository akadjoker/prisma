#pragma once

#include <cstdint>

namespace prisma
{

class GLState
{
public:
    void reset();

    void bindFramebuffer(std::uint32_t framebuffer);
    void viewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
    void clearColor(const float color[4]);
    void clearDepth(float depth);

private:
    enum : std::uint32_t
    {
        kFramebuffer = 1u << 0,
        kViewport = 1u << 1,
        kClearColor = 1u << 2,
        kClearDepth = 1u << 3
    };

    std::uint32_t known_ = 0;
    std::uint32_t framebuffer_ = 0;
    std::int32_t viewport_[4] = { 0, 0, 0, 0 };
    float clearColor_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float clearDepth_ = 1.0f;
};

} // namespace prisma
