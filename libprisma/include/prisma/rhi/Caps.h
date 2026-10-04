#pragma once

#include <cstdint>

namespace prisma
{

struct Caps
{
    bool compute = false;
    std::uint32_t maxTextureSize = 0;
    std::uint32_t maxColorTargets = 0;
};

} // namespace prisma
