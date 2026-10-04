#pragma once

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
};

struct DriverDesc
{
    DriverType type = DriverType::Null;
    const GLPlatform* gl = nullptr;
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
