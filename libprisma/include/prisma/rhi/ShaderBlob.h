#pragma once

#include "prisma/rhi/Caps.h"
#include "prisma/rhi/Types.h"

namespace prisma
{

struct ShaderBlob
{
    enum : std::uint32_t
    {
        kPackedSpirv = 0,
        kPackedGlsl = 1,
        kPackedEssl300 = 2,
        kPackedEssl310 = 3,
        kPackedEssl320 = 4,
        kPackedEssl320Inner = 5,
        kPackedCount = 6
    };

    ShaderStage stage;
    const std::uint32_t* spirv;
    std::uint32_t spirvSize;
    const char* glsl;
    const char* essl300;
    const char* essl310;
    const char* essl320;
    const char* essl320Inner;
    const ShaderBinding* bindings;
    std::uint32_t bindingCount;
    const std::uint8_t* packed = nullptr;
    std::uint32_t packedSize = 0;
    std::uint32_t unpackedSizes[kPackedCount] = { 0, 0, 0, 0, 0, 0 };
};

inline bool shaderBlobPacked(const ShaderBlob& blob)
{
    return blob.packed != nullptr && blob.packedSize != 0;
}

inline std::uint32_t shaderBlobUnpackedSize(const ShaderBlob& blob)
{
    std::uint32_t total = 0;
    for (std::uint32_t i = 0; i < ShaderBlob::kPackedCount; ++i) total += blob.unpackedSizes[i];
    return total;
}

inline ShaderDesc shaderDesc(const ShaderBlob& blob, const Caps& caps)
{
    ShaderDesc desc;
    desc.stage = blob.stage;
    desc.spirv = blob.spirv;
    desc.spirvSize = blob.spirvSize;
    desc.bindings = blob.bindings;
    desc.bindingCount = blob.bindingCount;
    if (!caps.gles) desc.source = blob.glsl;
    else if (caps.versionMajor > 3 || caps.versionMinor >= 2)
    {
        desc.source = blob.essl320;
        desc.innerSource = blob.essl320Inner;
    }
    else if (caps.versionMinor == 1)
        desc.source = blob.essl310;
    else
        desc.source = blob.essl300;
    return desc;
}

} // namespace prisma
