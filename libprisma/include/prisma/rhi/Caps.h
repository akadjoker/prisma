#pragma once

#include <cstdint>

namespace prisma
{

struct Caps
{
    bool gles = false;
    std::uint32_t versionMajor = 0;
    std::uint32_t versionMinor = 0;
    bool compute = false;
    bool debugOutput = false;
    bool floatColorTargets = false;
    std::uint32_t maxTextureSize = 0;
    std::uint32_t maxColorTargets = 0;
    std::uint32_t uniformBufferOffsetAlignment = 1;
};

} // namespace prisma
