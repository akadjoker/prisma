#pragma once

#include "prisma/rhi/Types.h"

#include <cstddef>
#include <cstdint>

namespace prisma
{

enum class FormatFamily : std::uint8_t
{
    Plain,
    BC,
    ETC2,
    ASTC
};

struct FormatBlock
{
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t bytes;
};

inline FormatFamily formatFamily(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::BC1:
        case TextureFormat::BC1Srgb:
        case TextureFormat::BC2:
        case TextureFormat::BC2Srgb:
        case TextureFormat::BC3:
        case TextureFormat::BC3Srgb:
        case TextureFormat::BC4:
        case TextureFormat::BC5:
        case TextureFormat::BC6H:
        case TextureFormat::BC7:
        case TextureFormat::BC7Srgb:
            return FormatFamily::BC;
        case TextureFormat::ETC2RGB8:
        case TextureFormat::ETC2RGB8Srgb:
        case TextureFormat::ETC2RGBA8:
        case TextureFormat::ETC2RGBA8Srgb:
        case TextureFormat::EACR11:
        case TextureFormat::EACRG11:
            return FormatFamily::ETC2;
        case TextureFormat::ASTC4x4:
        case TextureFormat::ASTC4x4Srgb:
        case TextureFormat::ASTC6x6:
        case TextureFormat::ASTC6x6Srgb:
        case TextureFormat::ASTC8x8:
        case TextureFormat::ASTC8x8Srgb:
            return FormatFamily::ASTC;
        default:
            return FormatFamily::Plain;
    }
}

inline bool isCompressedFormat(TextureFormat format)
{
    return formatFamily(format) != FormatFamily::Plain;
}

inline bool isDepthFormat(TextureFormat format)
{
    return format == TextureFormat::Depth32F || format == TextureFormat::Depth24Stencil8;
}

inline FormatBlock formatBlock(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::R8:
            return { 1, 1, 1 };
        case TextureFormat::RG8:
            return { 1, 1, 2 };
        case TextureFormat::RGBA16F:
            return { 1, 1, 8 };
        case TextureFormat::BC1:
        case TextureFormat::BC1Srgb:
        case TextureFormat::BC4:
        case TextureFormat::ETC2RGB8:
        case TextureFormat::ETC2RGB8Srgb:
        case TextureFormat::EACR11:
            return { 4, 4, 8 };
        case TextureFormat::BC2:
        case TextureFormat::BC2Srgb:
        case TextureFormat::BC3:
        case TextureFormat::BC3Srgb:
        case TextureFormat::BC5:
        case TextureFormat::BC6H:
        case TextureFormat::BC7:
        case TextureFormat::BC7Srgb:
        case TextureFormat::ETC2RGBA8:
        case TextureFormat::ETC2RGBA8Srgb:
        case TextureFormat::EACRG11:
        case TextureFormat::ASTC4x4:
        case TextureFormat::ASTC4x4Srgb:
            return { 4, 4, 16 };
        case TextureFormat::ASTC6x6:
        case TextureFormat::ASTC6x6Srgb:
            return { 6, 6, 16 };
        case TextureFormat::ASTC8x8:
        case TextureFormat::ASTC8x8Srgb:
            return { 8, 8, 16 };
        default:
            return { 1, 1, 4 };
    }
}

inline std::size_t levelBytes(TextureFormat format, std::uint32_t width, std::uint32_t height)
{
    const FormatBlock block = formatBlock(format);
    const std::size_t columns = (static_cast<std::size_t>(width) + block.width - 1) / block.width;
    const std::size_t rows = (static_cast<std::size_t>(height) + block.height - 1) / block.height;
    return columns * rows * block.bytes;
}

inline bool validRegion(TextureFormat format, std::uint32_t levelWidth, std::uint32_t levelHeight,
        const TextureRegion& region)
{
    const FormatBlock block = formatBlock(format);
    const std::uint64_t right = static_cast<std::uint64_t>(region.x) + region.width;
    const std::uint64_t bottom = static_cast<std::uint64_t>(region.y) + region.height;
    if (region.width == 0 || region.height == 0 || right > levelWidth || bottom > levelHeight)
        return false;
    if (region.x % block.width != 0 || region.y % block.height != 0) return false;
    if (region.width % block.width != 0 && right != levelWidth) return false;
    if (region.height % block.height != 0 && bottom != levelHeight) return false;
    return true;
}

} // namespace prisma
