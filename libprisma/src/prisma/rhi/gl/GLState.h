#pragma once

#include <cstdint>

namespace prisma
{

class GLState
{
public:
    enum : std::uint32_t
    {
        kMaxUniformSlots = 16
    };

    void reset();

    void bindFramebuffer(std::uint32_t framebuffer);
    void viewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
    void clearColor(const float color[4]);
    void clearDepth(float depth);

    void useProgram(std::uint32_t program);
    void bindVertexArray(std::uint32_t vertexArray);
    void bindArrayBuffer(std::uint32_t buffer);
    void bindUniformBuffer(std::uint32_t buffer);
    void bindUniformBufferRange(std::uint32_t slot, std::uint32_t buffer, std::uint32_t offset,
            std::uint32_t size);

    void depthTest(bool enabled);
    void depthMask(bool enabled);
    void depthFunc(std::uint32_t func);
    void cullFace(bool enabled, std::uint32_t face);
    void frontFace(std::uint32_t mode);
    void blend(bool enabled);
    void blendFunc(std::uint32_t srcColor, std::uint32_t dstColor, std::uint32_t srcAlpha,
            std::uint32_t dstAlpha);

    void programDeleted(std::uint32_t program);
    void vertexArrayDeleted(std::uint32_t vertexArray);
    void bufferDeleted(std::uint32_t buffer);

private:
    enum : std::uint32_t
    {
        kFramebuffer = 1u << 0,
        kViewport = 1u << 1,
        kClearColor = 1u << 2,
        kClearDepth = 1u << 3,
        kProgram = 1u << 4,
        kVertexArray = 1u << 5,
        kArrayBuffer = 1u << 6,
        kUniformBuffer = 1u << 7,
        kDepthTest = 1u << 8,
        kDepthMask = 1u << 9,
        kDepthFunc = 1u << 10,
        kCullEnabled = 1u << 11,
        kCullFace = 1u << 12,
        kFrontFace = 1u << 13,
        kBlend = 1u << 14,
        kBlendFunc = 1u << 15
    };

    struct UniformRange
    {
        std::uint32_t buffer = 0;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };

    bool same(std::uint32_t bit, bool& stored, bool value);
    bool same(std::uint32_t bit, std::uint32_t& stored, std::uint32_t value);

    std::uint32_t known_ = 0;
    std::uint32_t knownUniformSlots_ = 0;

    std::uint32_t framebuffer_ = 0;
    std::int32_t viewport_[4] = { 0, 0, 0, 0 };
    float clearColor_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float clearDepth_ = 1.0f;

    std::uint32_t program_ = 0;
    std::uint32_t vertexArray_ = 0;
    std::uint32_t arrayBuffer_ = 0;
    std::uint32_t uniformBuffer_ = 0;
    UniformRange uniformSlots_[kMaxUniformSlots];

    bool depthTest_ = false;
    bool depthMask_ = true;
    std::uint32_t depthFunc_ = 0;
    bool cullEnabled_ = false;
    std::uint32_t cullFace_ = 0;
    std::uint32_t frontFace_ = 0;
    bool blend_ = false;
    std::uint32_t blendFunc_[4] = { 0, 0, 0, 0 };
};

} // namespace prisma
