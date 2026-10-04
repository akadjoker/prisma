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
    void useProgram(std::uint32_t program);
    void bindVertexArray(std::uint32_t vertexArray);
    void bindArrayBuffer(std::uint32_t buffer);

    void programDeleted(std::uint32_t program);
    void vertexArrayDeleted(std::uint32_t vertexArray);
    void arrayBufferDeleted(std::uint32_t buffer);

private:
    enum : std::uint32_t
    {
        kFramebuffer = 1u << 0,
        kViewport = 1u << 1,
        kClearColor = 1u << 2,
        kClearDepth = 1u << 3,
        kProgram = 1u << 4,
        kVertexArray = 1u << 5,
        kArrayBuffer = 1u << 6
    };

    std::uint32_t known_ = 0;
    std::uint32_t framebuffer_ = 0;
    std::int32_t viewport_[4] = { 0, 0, 0, 0 };
    float clearColor_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float clearDepth_ = 1.0f;
    std::uint32_t program_ = 0;
    std::uint32_t vertexArray_ = 0;
    std::uint32_t arrayBuffer_ = 0;
};

} // namespace prisma
