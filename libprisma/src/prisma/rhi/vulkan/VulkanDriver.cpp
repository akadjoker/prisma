#include "prisma/rhi/Driver.h"
#include "prisma/rhi/HandleCast.h"

#include <ct/vector.hpp>
#include <vulkan/vulkan.h>

#include <stdio.h>
#include <string.h>

namespace prisma
{

namespace
{

const std::uint32_t kFramesInFlight = 2;
const char* const kValidationLayer = "VK_LAYER_KHRONOS_validation";

struct Frame
{
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;
};

struct SwapchainImage
{
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

const std::uint64_t kNeverUsed = UINT64_MAX;

struct BufferVersion
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    std::uint64_t lastUsedFrame = kNeverUsed;
};

struct VulkanBuffer
{
    enum : std::uint32_t
    {
        kMaxVersions = 8
    };

    BufferVersion versions[kMaxVersions];
    std::uint32_t versionCount = 0;
    std::uint32_t current = 0;
    std::uint32_t size = 0;
    VkBufferUsageFlags vkUsage = 0;
    BufferUsage usage = BufferUsage::Vertex;
    IndexFormat indexFormat = IndexFormat::UInt16;
};

struct VulkanShader
{
    VkShaderModule module = VK_NULL_HANDLE;
    ShaderStage stage = ShaderStage::Vertex;
};

struct VulkanPipeline
{
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    TargetFormats targets;
    std::uint32_t vertexBufferCount = 0;
    VertexBufferLayout vertexBuffers[PipelineDesc::kMaxVertexBuffers];
    std::uint32_t uniformCount = 0;
    std::uint32_t uniformSlots[PipelineDesc::kMaxUniformBlocks] = {};
    VkDescriptorSetLayout textureSetLayout = VK_NULL_HANDLE;
    std::uint32_t textureCount = 0;
    std::uint32_t textureSlots[PipelineDesc::kMaxTextures] = {};
};

struct UniformBinding
{
    BufferHandle handle;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct VulkanTexture
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mipLevels = 1;
    TextureFormat format = TextureFormat::RGBA8;
    std::uint32_t usage = 0;
};

struct VulkanSampler
{
    VkSampler sampler = VK_NULL_HANDLE;
};

struct TextureSlot
{
    TextureHandle texture;
    SamplerHandle sampler;
};

struct Garbage
{
    std::uint64_t frame = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout textureSetLayout = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
};

bool isDepthFormat(TextureFormat format)
{
    return format == TextureFormat::Depth32F || format == TextureFormat::Depth24Stencil8;
}

std::uint32_t bytesPerPixel(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::R8:
            return 1;
        case TextureFormat::RG8:
            return 2;
        case TextureFormat::RGBA16F:
            return 8;
        default:
            return 4;
    }
}

std::uint32_t fullMipCount(std::uint32_t width, std::uint32_t height)
{
    std::uint32_t size = width > height ? width : height;
    std::uint32_t levels = 1;
    while (size > 1)
    {
        size >>= 1;
        ++levels;
    }
    return levels;
}

VkSamplerAddressMode toVkAddress(AddressMode mode)
{
    switch (mode)
    {
        case AddressMode::Repeat:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case AddressMode::MirroredRepeat:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case AddressMode::ClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

bool sameTargets(const TargetFormats& a, const TargetFormats& b)
{
    if (a.window != b.window) return false;
    if (a.window) return true;
    if (a.colorCount != b.colorCount || a.depth != b.depth) return false;
    for (std::uint32_t i = 0; i < a.colorCount; ++i)
        if (a.colors[i] != b.colors[i]) return false;
    return true;
}

VkFormat toVkFormat(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::None:
            return VK_FORMAT_UNDEFINED;
        case TextureFormat::R8:
            return VK_FORMAT_R8_UNORM;
        case TextureFormat::RG8:
            return VK_FORMAT_R8G8_UNORM;
        case TextureFormat::RGBA8:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA8Srgb:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case TextureFormat::RGB10A2:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case TextureFormat::RGBA16F:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case TextureFormat::R11G11B10F:
            return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        case TextureFormat::Depth32F:
            return VK_FORMAT_D32_SFLOAT;
        case TextureFormat::Depth24Stencil8:
            return VK_FORMAT_D24_UNORM_S8_UINT;
    }
    return VK_FORMAT_UNDEFINED;
}

VkFormat toVkFormat(VertexFormat format)
{
    switch (format)
    {
        case VertexFormat::Float1:
            return VK_FORMAT_R32_SFLOAT;
        case VertexFormat::Float2:
            return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float3:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float4:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VertexFormat::Half2:
            return VK_FORMAT_R16G16_SFLOAT;
        case VertexFormat::Half4:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case VertexFormat::UByte4Norm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case VertexFormat::Byte4Norm:
            return VK_FORMAT_R8G8B8A8_SNORM;
        case VertexFormat::UShort2Norm:
            return VK_FORMAT_R16G16_UNORM;
        case VertexFormat::UShort4Norm:
            return VK_FORMAT_R16G16B16A16_UNORM;
        case VertexFormat::Short2Norm:
            return VK_FORMAT_R16G16_SNORM;
        case VertexFormat::Short4Norm:
            return VK_FORMAT_R16G16B16A16_SNORM;
        case VertexFormat::Int1010102Norm:
            return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
        case VertexFormat::UByte4:
            return VK_FORMAT_R8G8B8A8_UINT;
        case VertexFormat::UShort4:
            return VK_FORMAT_R16G16B16A16_UINT;
    }
    return VK_FORMAT_UNDEFINED;
}

VkPrimitiveTopology toVkTopology(Topology topology)
{
    switch (topology)
    {
        case Topology::Triangles:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case Topology::TriangleStrip:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case Topology::Lines:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case Topology::LineStrip:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case Topology::Points:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkCompareOp toVkCompare(CompareOp op)
{
    switch (op)
    {
        case CompareOp::Never:
            return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:
            return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:
            return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessEqual:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:
            return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual:
            return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterEqual:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:
            return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_LESS;
}

VkBlendFactor toVkBlendFactor(BlendFactor factor)
{
    switch (factor)
    {
        case BlendFactor::Zero:
            return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One:
            return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcColor:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::SrcAlpha:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstColor:
            return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::DstAlpha:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;
}

class VulkanDriver final : public Driver
{
public:
    VulkanDriver(const VulkanPlatform& platform, void (*log)(const char*))
            : platform_(platform),
              log_(log)
    {
    }

    ~VulkanDriver() override
    {
        if (device_)
        {
            vkDeviceWaitIdle(device_);
            for (VulkanPipeline& pipeline: pipelines_)
            {
                vkDestroyPipeline(device_, pipeline.pipeline, nullptr);
                vkDestroyPipelineLayout(device_, pipeline.layout, nullptr);
                vkDestroyDescriptorSetLayout(device_, pipeline.setLayout, nullptr);
                vkDestroyDescriptorSetLayout(device_, pipeline.textureSetLayout, nullptr);
            }
            for (VulkanTexture& texture: textures_)
            {
                vkDestroyImageView(device_, texture.view, nullptr);
                vkDestroyImage(device_, texture.image, nullptr);
                vkFreeMemory(device_, texture.memory, nullptr);
            }
            for (VulkanSampler& sampler: samplers_)
                vkDestroySampler(device_, sampler.sampler, nullptr);
            for (VulkanShader& shader: shaders_)
                vkDestroyShaderModule(device_, shader.module, nullptr);
            for (VulkanBuffer& buffer: buffers_)
            {
                for (std::uint32_t i = 0; i < buffer.versionCount; ++i)
                {
                    vkDestroyBuffer(device_, buffer.versions[i].buffer, nullptr);
                    vkFreeMemory(device_, buffer.versions[i].memory, nullptr);
                }
            }
            collectGarbage(true);
            destroySwapchain();
            for (std::uint32_t i = 0; i < kFramesInFlight; ++i)
            {
                if (frames_[i].imageAvailable)
                    vkDestroySemaphore(device_, frames_[i].imageAvailable, nullptr);
                if (frames_[i].inFlight) vkDestroyFence(device_, frames_[i].inFlight, nullptr);
            }
            for (std::uint32_t i = 0; i < kFramesInFlight; ++i)
                if (descriptorPools_[i])
                    vkDestroyDescriptorPool(device_, descriptorPools_[i], nullptr);
            if (commandPool_) vkDestroyCommandPool(device_, commandPool_, nullptr);
            vkDestroyDevice(device_, nullptr);
        }
        if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
        if (messenger_) destroyMessenger(instance_, messenger_);
        if (instance_) vkDestroyInstance(instance_, nullptr);
    }

    DriverError init(bool debug)
    {
        if (!createInstance(debug)) return DriverError::ContextFailed;

        std::uint64_t surface = 0;
        if (!platform_.createSurface(platform_.user, instance_, &surface))
        {
            log("Vulkan: the platform could not create a surface");
            return DriverError::ContextFailed;
        }
        surface_ = (VkSurfaceKHR) surface;

        if (!pickPhysicalDevice()) return DriverError::VersionTooLow;
        if (!createDevice()) return DriverError::ContextFailed;
        if (!createFrames() || !createDescriptorPools()) return DriverError::ContextFailed;
        createSwapchain();
        return DriverError::None;
    }

    DriverType type() const override { return DriverType::Vulkan; }
    const Caps& caps() const override { return caps_; }

    BufferHandle createBuffer(const BufferDesc& desc) override
    {
        if (desc.size == 0) return BufferHandle();

        VulkanBuffer buffer;
        buffer.size = desc.size;
        buffer.usage = desc.usage;
        buffer.indexFormat = desc.indexFormat;
        switch (desc.usage)
        {
            case BufferUsage::Vertex:
                buffer.vkUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                break;
            case BufferUsage::Index:
                buffer.vkUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
                break;
            case BufferUsage::Uniform:
                buffer.vkUsage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                break;
        }
        if (!createVersion(buffer, desc.debugName)) return BufferHandle();
        if (desc.data) memcpy(buffer.versions[0].mapped, desc.data, desc.size);
        return handleCast<BufferHandle>(buffers_.insert(buffer));
    }

    void updateBuffer(BufferHandle handle, std::uint32_t offset, const void* data,
            std::uint32_t size) override
    {
        if (passActive_)
        {
            log("updateBuffer: not allowed inside a render pass");
            return;
        }
        VulkanBuffer* buffer = buffers_.get(handleCast<BufferSlot>(handle));
        if (!buffer || !data || static_cast<std::uint64_t>(offset) + size > buffer->size)
        {
            log("updateBuffer: invalid buffer handle or range");
            return;
        }

        const std::uint32_t previous = buffer->current;
        if (buffer->versions[previous].lastUsedFrame != kNeverUsed)
        {
            std::uint32_t next = buffer->versionCount;
            for (std::uint32_t i = 0; i < buffer->versionCount && next == buffer->versionCount; ++i)
            {
                const std::uint64_t used = buffer->versions[i].lastUsedFrame;
                if (i != previous && (used == kNeverUsed || used + kFramesInFlight < frameNumber_))
                    next = i;
            }
            if (next == buffer->versionCount && !createVersion(*buffer, nullptr))
            {
                log("updateBuffer: too many versions of this buffer are still in use");
                next = previous;
            }
            if (next != previous)
            {
                if (offset != 0 || size != buffer->size)
                    memcpy(buffer->versions[next].mapped, buffer->versions[previous].mapped,
                            buffer->size);
                buffer->versions[next].lastUsedFrame = kNeverUsed;
                buffer->current = next;
            }
        }
        memcpy(static_cast<char*>(buffer->versions[buffer->current].mapped) + offset, data, size);
    }

    ShaderHandle createShader(const ShaderDesc& desc) override
    {
        if (!desc.spirv || desc.spirvSize == 0 || desc.spirvSize % 4 != 0)
        {
            log("createShader: the Vulkan backend needs SPIR-V in ShaderDesc::spirv");
            return ShaderHandle();
        }

        VulkanShader shader;
        shader.stage = desc.stage;
        VkShaderModuleCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = desc.spirvSize;
        info.pCode = static_cast<const std::uint32_t*>(desc.spirv);
        if (vkCreateShaderModule(device_, &info, nullptr, &shader.module) != VK_SUCCESS)
        {
            log("createShader: could not create the shader module");
            return ShaderHandle();
        }
        setName(VK_OBJECT_TYPE_SHADER_MODULE, (std::uint64_t) shader.module, desc.debugName);
        return handleCast<ShaderHandle>(shaders_.insert(shader));
    }

    PipelineHandle createPipeline(const PipelineDesc& desc) override
    {
        const VulkanShader* vertex = shaders_.get(handleCast<ShaderSlot>(desc.vertexShader));
        const VulkanShader* fragment = shaders_.get(handleCast<ShaderSlot>(desc.fragmentShader));
        bool valid = vertex && fragment && desc.attributeCount <= PipelineDesc::kMaxAttributes &&
                     desc.vertexBufferCount <= PipelineDesc::kMaxVertexBuffers &&
                     desc.targets.colorCount <= TargetFormats::kMaxColors &&
                     desc.uniformBlockCount <= PipelineDesc::kMaxUniformBlocks &&
                     desc.textureCount <= PipelineDesc::kMaxTextures;
        for (std::uint32_t i = 0; valid && i < desc.textureCount; ++i)
            if (desc.textures[i].slot >= kMaxTextureSlots) valid = false;
        for (std::uint32_t i = 0; valid && i < desc.uniformBlockCount; ++i)
            if (desc.uniformBlocks[i].slot >= kMaxUniformSlots) valid = false;
        for (std::uint32_t i = 0; valid && i < desc.attributeCount; ++i)
            if (desc.attributes[i].buffer >= desc.vertexBufferCount) valid = false;
        if (!valid)
        {
            log("createPipeline: invalid shader handle or vertex input");
            return PipelineHandle();
        }

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex->module;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment->module;
        stages[1].pName = "main";

        VkVertexInputBindingDescription bindings[PipelineDesc::kMaxVertexBuffers] = {};
        for (std::uint32_t i = 0; i < desc.vertexBufferCount; ++i)
        {
            bindings[i].binding = i;
            bindings[i].stride = desc.vertexBuffers[i].stride;
            bindings[i].inputRate = desc.vertexBuffers[i].step == VertexStep::Instance
                                            ? VK_VERTEX_INPUT_RATE_INSTANCE
                                            : VK_VERTEX_INPUT_RATE_VERTEX;
        }
        VkVertexInputAttributeDescription attributes[PipelineDesc::kMaxAttributes] = {};
        for (std::uint32_t i = 0; i < desc.attributeCount; ++i)
        {
            attributes[i].location = desc.attributes[i].location;
            attributes[i].binding = desc.attributes[i].buffer;
            attributes[i].format = toVkFormat(desc.attributes[i].format);
            attributes[i].offset = desc.attributes[i].offset;
        }
        VkPipelineVertexInputStateCreateInfo vertexInput = {};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = desc.vertexBufferCount;
        vertexInput.pVertexBindingDescriptions = bindings;
        vertexInput.vertexAttributeDescriptionCount = desc.attributeCount;
        vertexInput.pVertexAttributeDescriptions = attributes;

        VkPipelineInputAssemblyStateCreateInfo assembly = {};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = toVkTopology(desc.topology);
        assembly.primitiveRestartEnable =
                desc.topology == Topology::TriangleStrip || desc.topology == Topology::LineStrip;

        VkPipelineViewportStateCreateInfo viewport = {};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster = {};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.lineWidth = 1.0f;
        raster.cullMode = VK_CULL_MODE_NONE;
        if (desc.cullMode == CullMode::Front) raster.cullMode = VK_CULL_MODE_FRONT_BIT;
        if (desc.cullMode == CullMode::Back) raster.cullMode = VK_CULL_MODE_BACK_BIT;
        raster.frontFace = desc.frontFace == FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE
                                                                  : VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisample = {};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil = {};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = desc.depthTest;
        depthStencil.depthWriteEnable = desc.depthWrite;
        depthStencil.depthCompareOp = toVkCompare(desc.depthCompare);
        depthStencil.maxDepthBounds = 1.0f;

        const std::uint32_t colorCount = desc.targets.window ? 1 : desc.targets.colorCount;
        VkFormat colorFormats[TargetFormats::kMaxColors] = {};
        VkPipelineColorBlendAttachmentState blends[TargetFormats::kMaxColors] = {};
        for (std::uint32_t i = 0; i < colorCount; ++i)
        {
            colorFormats[i] = desc.targets.window ? format_ : toVkFormat(desc.targets.colors[i]);
            blends[i].blendEnable = desc.blend;
            blends[i].srcColorBlendFactor = toVkBlendFactor(desc.srcColor);
            blends[i].dstColorBlendFactor = toVkBlendFactor(desc.dstColor);
            blends[i].colorBlendOp = VK_BLEND_OP_ADD;
            blends[i].srcAlphaBlendFactor = toVkBlendFactor(desc.srcAlpha);
            blends[i].dstAlphaBlendFactor = toVkBlendFactor(desc.dstAlpha);
            blends[i].alphaBlendOp = VK_BLEND_OP_ADD;
            blends[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        }
        VkPipelineColorBlendStateCreateInfo blend = {};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = colorCount;
        blend.pAttachments = blends;

        const VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic = {};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;

        VkPipelineRenderingCreateInfo rendering = {};
        rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        rendering.colorAttachmentCount = colorCount;
        rendering.pColorAttachmentFormats = colorFormats;
        rendering.depthAttachmentFormat =
                desc.targets.window ? depthFormat_ : textureFormat(desc.targets.depth);

        VulkanPipeline pipeline;
        pipeline.targets = desc.targets;
        pipeline.vertexBufferCount = desc.vertexBufferCount;
        for (std::uint32_t i = 0; i < desc.vertexBufferCount; ++i)
            pipeline.vertexBuffers[i] = desc.vertexBuffers[i];

        pipeline.uniformCount = desc.uniformBlockCount;
        for (std::uint32_t i = 0; i < desc.uniformBlockCount; ++i)
            pipeline.uniformSlots[i] = desc.uniformBlocks[i].slot;
        for (std::uint32_t i = 1; i < pipeline.uniformCount; ++i)
            for (std::uint32_t j = i;
                    j > 0 && pipeline.uniformSlots[j - 1] > pipeline.uniformSlots[j]; --j)
            {
                const std::uint32_t swapped = pipeline.uniformSlots[j];
                pipeline.uniformSlots[j] = pipeline.uniformSlots[j - 1];
                pipeline.uniformSlots[j - 1] = swapped;
            }

        VkDescriptorSetLayoutBinding setBindings[PipelineDesc::kMaxUniformBlocks] = {};
        for (std::uint32_t i = 0; i < pipeline.uniformCount; ++i)
        {
            setBindings[i].binding = pipeline.uniformSlots[i];
            setBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            setBindings[i].descriptorCount = 1;
            setBindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo setLayout = {};
        setLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        setLayout.bindingCount = pipeline.uniformCount;
        setLayout.pBindings = setBindings;
        if (vkCreateDescriptorSetLayout(device_, &setLayout, nullptr, &pipeline.setLayout) !=
                VK_SUCCESS)
        {
            log("createPipeline: could not create the descriptor set layout");
            return PipelineHandle();
        }

        pipeline.textureCount = desc.textureCount;
        for (std::uint32_t i = 0; i < desc.textureCount; ++i)
            pipeline.textureSlots[i] = desc.textures[i].slot;
        for (std::uint32_t i = 1; i < pipeline.textureCount; ++i)
            for (std::uint32_t j = i;
                    j > 0 && pipeline.textureSlots[j - 1] > pipeline.textureSlots[j]; --j)
            {
                const std::uint32_t swapped = pipeline.textureSlots[j];
                pipeline.textureSlots[j] = pipeline.textureSlots[j - 1];
                pipeline.textureSlots[j - 1] = swapped;
            }

        VkDescriptorSetLayoutBinding textureBindings[PipelineDesc::kMaxTextures] = {};
        for (std::uint32_t i = 0; i < pipeline.textureCount; ++i)
        {
            textureBindings[i].binding = pipeline.textureSlots[i];
            textureBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            textureBindings[i].descriptorCount = 1;
            textureBindings[i].stageFlags =
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo textureSetLayout = {};
        textureSetLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        textureSetLayout.bindingCount = pipeline.textureCount;
        textureSetLayout.pBindings = textureBindings;
        if (vkCreateDescriptorSetLayout(device_, &textureSetLayout, nullptr,
                    &pipeline.textureSetLayout) != VK_SUCCESS)
        {
            log("createPipeline: could not create the texture set layout");
            vkDestroyDescriptorSetLayout(device_, pipeline.setLayout, nullptr);
            return PipelineHandle();
        }

        const VkDescriptorSetLayout setLayouts[2] = { pipeline.setLayout,
            pipeline.textureSetLayout };
        VkPipelineLayoutCreateInfo layout = {};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 2;
        layout.pSetLayouts = setLayouts;
        if (vkCreatePipelineLayout(device_, &layout, nullptr, &pipeline.layout) != VK_SUCCESS)
        {
            log("createPipeline: could not create the pipeline layout");
            vkDestroyDescriptorSetLayout(device_, pipeline.setLayout, nullptr);
            vkDestroyDescriptorSetLayout(device_, pipeline.textureSetLayout, nullptr);
            return PipelineHandle();
        }

        VkGraphicsPipelineCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.pNext = &rendering;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depthStencil;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = pipeline.layout;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr,
                    &pipeline.pipeline) != VK_SUCCESS)
        {
            log("createPipeline: could not create the pipeline");
            vkDestroyPipelineLayout(device_, pipeline.layout, nullptr);
            vkDestroyDescriptorSetLayout(device_, pipeline.setLayout, nullptr);
            vkDestroyDescriptorSetLayout(device_, pipeline.textureSetLayout, nullptr);
            return PipelineHandle();
        }
        setName(VK_OBJECT_TYPE_PIPELINE, (std::uint64_t) pipeline.pipeline, desc.debugName);
        return handleCast<PipelineHandle>(pipelines_.insert(pipeline));
    }

    TextureHandle createTexture(const TextureDesc& desc) override
    {
        if (desc.format == TextureFormat::None || desc.width == 0 || desc.height == 0 ||
                desc.width > caps_.maxTextureSize || desc.height > caps_.maxTextureSize)
        {
            log("createTexture: invalid size");
            return TextureHandle();
        }

        const bool depth = isDepthFormat(desc.format);
        const std::uint32_t fullChain = fullMipCount(desc.width, desc.height);
        VulkanTexture texture;
        texture.width = desc.width;
        texture.height = desc.height;
        texture.format = desc.format;
        texture.usage = desc.usage;
        texture.mipLevels = desc.mipLevels == 0 ? fullChain : desc.mipLevels;
        if (texture.mipLevels > fullChain) texture.mipLevels = fullChain;

        VkImageCreateInfo image = {};
        image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = textureFormat(desc.format);
        image.extent.width = desc.width;
        image.extent.height = desc.height;
        image.extent.depth = 1;
        image.mipLevels = texture.mipLevels;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        if (!depth)
            image.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (desc.usage & kTextureRenderTarget)
            image.usage |= depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                 : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device_, &image, nullptr, &texture.image) != VK_SUCCESS)
        {
            log("createTexture: could not create the image");
            return TextureHandle();
        }

        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device_, texture.image, &requirements);
        VkMemoryAllocateInfo allocate = {};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex =
                findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkImageViewCreateInfo view = {};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = texture.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = image.format;
        view.subresourceRange.aspectMask =
                depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = texture.mipLevels;
        view.subresourceRange.layerCount = 1;

        if (allocate.memoryTypeIndex == UINT32_MAX ||
                vkAllocateMemory(device_, &allocate, nullptr, &texture.memory) != VK_SUCCESS ||
                vkBindImageMemory(device_, texture.image, texture.memory, 0) != VK_SUCCESS ||
                vkCreateImageView(device_, &view, nullptr, &texture.view) != VK_SUCCESS)
        {
            log("createTexture: could not allocate memory or create the view");
            vkDestroyImage(device_, texture.image, nullptr);
            if (texture.memory) vkFreeMemory(device_, texture.memory, nullptr);
            return TextureHandle();
        }
        setName(VK_OBJECT_TYPE_IMAGE, (std::uint64_t) texture.image, desc.debugName);

        if (!depth && !uploadTexture(texture, desc.data, desc.generateMipmaps))
        {
            vkDestroyImageView(device_, texture.view, nullptr);
            vkDestroyImage(device_, texture.image, nullptr);
            vkFreeMemory(device_, texture.memory, nullptr);
            return TextureHandle();
        }
        return handleCast<TextureHandle>(textures_.insert(texture));
    }

    SamplerHandle createSampler(const SamplerDesc& desc) override
    {
        VkSamplerCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = desc.magFilter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.minFilter = desc.minFilter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.mipmapMode = desc.mipFilter == MipFilter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                              : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = toVkAddress(desc.addressU);
        info.addressModeV = toVkAddress(desc.addressV);
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.maxAnisotropy = 1.0f;
        info.maxLod = desc.mipFilter == MipFilter::None ? 0.25f : VK_LOD_CLAMP_NONE;

        VulkanSampler sampler;
        if (vkCreateSampler(device_, &info, nullptr, &sampler.sampler) != VK_SUCCESS)
        {
            log("createSampler: could not create the sampler");
            return SamplerHandle();
        }
        setName(VK_OBJECT_TYPE_SAMPLER, (std::uint64_t) sampler.sampler, desc.debugName);
        return handleCast<SamplerHandle>(samplers_.insert(sampler));
    }

    void destroy(BufferHandle handle) override
    {
        const BufferSlot slot = handleCast<BufferSlot>(handle);
        const VulkanBuffer* buffer = buffers_.get(slot);
        if (!buffer) return;
        for (std::uint32_t i = 0; i < buffer->versionCount; ++i)
        {
            Garbage item;
            item.frame = frameNumber_;
            item.buffer = buffer->versions[i].buffer;
            item.memory = buffer->versions[i].memory;
            garbage_.push_back(item);
        }
        buffers_.erase(slot);
    }

    void destroy(ShaderHandle handle) override
    {
        const ShaderSlot slot = handleCast<ShaderSlot>(handle);
        const VulkanShader* shader = shaders_.get(slot);
        if (!shader) return;
        vkDestroyShaderModule(device_, shader->module, nullptr);
        shaders_.erase(slot);
    }

    void destroy(PipelineHandle handle) override
    {
        const PipelineSlot slot = handleCast<PipelineSlot>(handle);
        const VulkanPipeline* pipeline = pipelines_.get(slot);
        if (!pipeline) return;
        Garbage item;
        item.frame = frameNumber_;
        item.pipeline = pipeline->pipeline;
        item.layout = pipeline->layout;
        item.setLayout = pipeline->setLayout;
        item.textureSetLayout = pipeline->textureSetLayout;
        garbage_.push_back(item);
        pipelines_.erase(slot);
    }

    void destroy(TextureHandle handle) override
    {
        const TextureSlotHandle slot = handleCast<TextureSlotHandle>(handle);
        const VulkanTexture* texture = textures_.get(slot);
        if (!texture) return;
        Garbage item;
        item.frame = frameNumber_;
        item.image = texture->image;
        item.view = texture->view;
        item.memory = texture->memory;
        garbage_.push_back(item);
        textures_.erase(slot);
    }

    void destroy(SamplerHandle handle) override
    {
        const SamplerSlotHandle slot = handleCast<SamplerSlotHandle>(handle);
        const VulkanSampler* sampler = samplers_.get(slot);
        if (!sampler) return;
        Garbage item;
        item.frame = frameNumber_;
        item.sampler = sampler->sampler;
        garbage_.push_back(item);
        samplers_.erase(slot);
    }

    void beginFrame() override
    {
        frameReady_ = false;

        std::uint32_t width = 0;
        std::uint32_t height = 0;
        platform_.framebufferSize(platform_.user, &width, &height);
        if (width == 0 || height == 0) return;
        if (!swapchain_ || width != requestedWidth_ || height != requestedHeight_)
        {
            requestedWidth_ = width;
            requestedHeight_ = height;
            if (!createSwapchain()) return;
        }

        Frame& frame = frames_[frameIndex_];
        vkWaitForFences(device_, 1, &frame.inFlight, VK_TRUE, UINT64_MAX);
        collectGarbage(false);
        vkResetDescriptorPool(device_, descriptorPools_[frameIndex_], 0);
        lastSet_ = VK_NULL_HANDLE;
        lastTextureSet_ = VK_NULL_HANDLE;

        const VkResult acquired = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                frame.imageAvailable, VK_NULL_HANDLE, &imageIndex_);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR)
        {
            createSwapchain();
            return;
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
        {
            log("Vulkan: could not acquire a swapchain image");
            return;
        }

        vkResetFences(device_, 1, &frame.inFlight);
        vkResetCommandBuffer(frame.commands, 0);

        VkCommandBufferBeginInfo begin = {};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(frame.commands, &begin);
        frameReady_ = true;
    }

    void beginRenderPass(const RenderPassDesc& desc) override
    {
        passActive_ = false;
        if (!frameReady_) return;
        if (desc.colorCount > 0 || desc.depth.valid())
        {
            log("Vulkan: offscreen render targets are not implemented yet");
            return;
        }

        VkCommandBuffer commands = frames_[frameIndex_].commands;
        SwapchainImage& target = images_[imageIndex_];
        if (target.layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            transition(commands, target,
                    desc.colorLoad == LoadOp::Load ? target.layout : VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        transitionDepth(commands);

        VkRenderingAttachmentInfo color = {};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = target.view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = toLoadOp(desc.colorLoad);
        color.storeOp = desc.colorStore == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE
                                                          : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        for (int i = 0; i < 4; ++i) color.clearValue.color.float32[i] = desc.clearColor[i];

        VkRenderingAttachmentInfo depth = {};
        depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView = depthView_;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp = toLoadOp(desc.depthLoad);
        depth.storeOp = desc.depthStore == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE
                                                          : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.clearValue.depthStencil.depth = desc.clearDepth;

        VkRenderingInfo rendering = {};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = extent_;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        rendering.pDepthAttachment = &depth;
        vkCmdBeginRendering(commands, &rendering);

        passActive_ = true;
        passFormats_ = TargetFormats();
        passWidth_ = extent_.width;
        passHeight_ = extent_.height;
        pipeline_ = PipelineHandle();
        boundPipeline_ = VK_NULL_HANDLE;
        indexBuffer_ = BufferHandle();
        for (std::uint32_t i = 0; i < PipelineDesc::kMaxVertexBuffers; ++i)
        {
            vertexBuffers_[i] = BufferHandle();
            vertexOffsets_[i] = 0;
        }
        for (std::uint32_t i = 0; i < kMaxUniformSlots; ++i) uniformBindings_[i] = UniformBinding();
        vertexDirty_ = true;
        indexDirty_ = true;
        for (std::uint32_t i = 0; i < kMaxTextureSlots; ++i) textureBindings_[i] = TextureSlot();
        uniformDirty_ = true;
        textureDirty_ = true;

        Viewport viewport;
        viewport.width = static_cast<float>(passWidth_);
        viewport.height = static_cast<float>(passHeight_);
        setViewport(viewport);
        Rect scissor;
        scissor.width = passWidth_;
        scissor.height = passHeight_;
        setScissor(scissor);
    }

    void setViewport(const Viewport& viewport) override
    {
        if (!passActive_) return;
        VkViewport flipped;
        flipped.x = viewport.x;
        flipped.y = viewport.y + viewport.height;
        flipped.width = viewport.width;
        flipped.height = -viewport.height;
        flipped.minDepth = viewport.minDepth;
        flipped.maxDepth = viewport.maxDepth;
        vkCmdSetViewport(frames_[frameIndex_].commands, 0, 1, &flipped);
    }

    void setScissor(const Rect& rect) override
    {
        if (!passActive_) return;
        std::int64_t left = rect.x < 0 ? 0 : rect.x;
        std::int64_t top = rect.y < 0 ? 0 : rect.y;
        std::int64_t right = static_cast<std::int64_t>(rect.x) + rect.width;
        std::int64_t bottom = static_cast<std::int64_t>(rect.y) + rect.height;
        if (right > passWidth_) right = passWidth_;
        if (bottom > passHeight_) bottom = passHeight_;
        if (right < left) right = left;
        if (bottom < top) bottom = top;

        VkRect2D scissor;
        scissor.offset.x = static_cast<std::int32_t>(left);
        scissor.offset.y = static_cast<std::int32_t>(top);
        scissor.extent.width = static_cast<std::uint32_t>(right - left);
        scissor.extent.height = static_cast<std::uint32_t>(bottom - top);
        vkCmdSetScissor(frames_[frameIndex_].commands, 0, 1, &scissor);
    }

    void bindPipeline(PipelineHandle handle) override
    {
        pipeline_ = handle;
        vertexDirty_ = true;
    }

    void bindVertexBuffer(std::uint32_t slot, BufferHandle handle, std::uint32_t offset) override
    {
        if (slot >= PipelineDesc::kMaxVertexBuffers)
        {
            log("bindVertexBuffer: slot out of range");
            return;
        }
        vertexBuffers_[slot] = handle;
        vertexOffsets_[slot] = offset;
        vertexDirty_ = true;
    }

    void bindIndexBuffer(BufferHandle handle) override
    {
        indexBuffer_ = handle;
        indexDirty_ = true;
    }

    void bindUniformBuffer(std::uint32_t slot, BufferHandle handle, std::uint32_t offset,
            std::uint32_t size) override
    {
        const VulkanBuffer* buffer = buffers_.get(handleCast<BufferSlot>(handle));
        if (!buffer || buffer->usage != BufferUsage::Uniform || size == 0 ||
                static_cast<std::uint64_t>(offset) + size > buffer->size ||
                slot >= kMaxUniformSlots || offset % caps_.uniformBufferOffsetAlignment != 0)
        {
            log("bindUniformBuffer: invalid buffer handle, range, alignment or slot");
            return;
        }
        uniformBindings_[slot].handle = handle;
        uniformBindings_[slot].offset = offset;
        uniformBindings_[slot].size = size;
        uniformDirty_ = true;
    }

    void bindTexture(std::uint32_t slot, TextureHandle texture, SamplerHandle sampler) override
    {
        if (slot >= kMaxTextureSlots ||
                !textures_.contains(handleCast<TextureSlotHandle>(texture)) ||
                !samplers_.contains(handleCast<SamplerSlotHandle>(sampler)))
        {
            log("bindTexture: invalid texture handle, sampler handle or slot");
            return;
        }
        textureBindings_[slot].texture = texture;
        textureBindings_[slot].sampler = sampler;
        textureDirty_ = true;
    }

    void draw(std::uint32_t vertexCount, std::uint32_t firstVertex,
            std::uint32_t instanceCount) override
    {
        if (!prepareDraw(static_cast<std::uint64_t>(firstVertex) + vertexCount, instanceCount))
            return;
        vkCmdDraw(frames_[frameIndex_].commands, vertexCount, instanceCount, firstVertex, 0);
    }

    void drawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex,
            std::uint32_t instanceCount) override
    {
        VulkanBuffer* indices = buffers_.get(handleCast<BufferSlot>(indexBuffer_));
        if (!indices || indices->usage != BufferUsage::Index)
        {
            log("drawIndexed: no valid index buffer bound");
            return;
        }
        const bool wide = indices->indexFormat == IndexFormat::UInt32;
        const std::uint64_t indexSize = wide ? 4 : 2;
        if ((static_cast<std::uint64_t>(firstIndex) + indexCount) * indexSize > indices->size)
        {
            log("drawIndexed: index range is outside the index buffer");
            return;
        }
        if (!prepareDraw(0, instanceCount)) return;

        VkCommandBuffer commands = frames_[frameIndex_].commands;
        BufferVersion& version = indices->versions[indices->current];
        version.lastUsedFrame = frameNumber_;
        if (indexDirty_)
        {
            vkCmdBindIndexBuffer(commands, version.buffer, 0,
                    wide ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
            indexDirty_ = false;
        }
        vkCmdDrawIndexed(commands, indexCount, instanceCount, firstIndex, 0, 0);
    }

    void endRenderPass() override
    {
        if (!passActive_) return;
        passActive_ = false;
        vkCmdEndRendering(frames_[frameIndex_].commands);
    }

    void endFrame() override
    {
        if (!frameReady_) return;

        Frame& frame = frames_[frameIndex_];
        SwapchainImage& target = images_[imageIndex_];
        transition(frame.commands, target, target.layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        vkEndCommandBuffer(frame.commands);

        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &frame.imageAvailable;
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &frame.commands;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &target.renderFinished;
        if (vkQueueSubmit(queue_, 1, &submit, frame.inFlight) != VK_SUCCESS)
            log("Vulkan: could not submit the frame");
    }

    void present() override
    {
        if (!frameReady_) return;
        frameReady_ = false;

        VkPresentInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &images_[imageIndex_].renderFinished;
        info.swapchainCount = 1;
        info.pSwapchains = &swapchain_;
        info.pImageIndices = &imageIndex_;
        const VkResult presented = vkQueuePresentKHR(queue_, &info);
        frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
        ++frameNumber_;
        if (presented == VK_ERROR_OUT_OF_DATE_KHR) createSwapchain();
    }

private:
    void log(const char* message) const
    {
        if (log_) log_(message);
    }

    using BufferSlot = ct::Handle32<VulkanBuffer>;
    using ShaderSlot = ct::Handle32<VulkanShader>;
    using PipelineSlot = ct::Handle32<VulkanPipeline>;
    using TextureSlotHandle = ct::Handle32<VulkanTexture>;
    using SamplerSlotHandle = ct::Handle32<VulkanSampler>;

    void setName(VkObjectType type, std::uint64_t handle, const char* name) const
    {
        if (!setObjectName_ || !name) return;
        VkDebugUtilsObjectNameInfoEXT info = {};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        info.objectType = type;
        info.objectHandle = handle;
        info.pObjectName = name;
        setObjectName_(device_, &info);
    }

    std::uint32_t findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags wanted) const
    {
        for (std::uint32_t i = 0; i < memoryProperties_.memoryTypeCount; ++i)
            if ((typeBits & (1u << i)) &&
                    (memoryProperties_.memoryTypes[i].propertyFlags & wanted) == wanted)
                return i;
        return UINT32_MAX;
    }

    bool createVersion(VulkanBuffer& buffer, const char* name)
    {
        if (buffer.versionCount >= VulkanBuffer::kMaxVersions) return false;
        BufferVersion version;

        VkBufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = buffer.size;
        info.usage = buffer.vkUsage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device_, &info, nullptr, &version.buffer) != VK_SUCCESS)
        {
            log("Vulkan: could not create a buffer");
            return false;
        }

        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device_, version.buffer, &requirements);
        VkMemoryAllocateInfo allocate = {};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (allocate.memoryTypeIndex == UINT32_MAX ||
                vkAllocateMemory(device_, &allocate, nullptr, &version.memory) != VK_SUCCESS ||
                vkBindBufferMemory(device_, version.buffer, version.memory, 0) != VK_SUCCESS ||
                vkMapMemory(device_, version.memory, 0, VK_WHOLE_SIZE, 0, &version.mapped) !=
                        VK_SUCCESS)
        {
            log("Vulkan: could not allocate or map buffer memory");
            vkDestroyBuffer(device_, version.buffer, nullptr);
            if (version.memory) vkFreeMemory(device_, version.memory, nullptr);
            return false;
        }
        setName(VK_OBJECT_TYPE_BUFFER, (std::uint64_t) version.buffer, name);
        buffer.versions[buffer.versionCount++] = version;
        return true;
    }

    void collectGarbage(bool all)
    {
        std::size_t kept = 0;
        for (std::size_t i = 0; i < garbage_.size(); ++i)
        {
            const Garbage& item = garbage_[i];
            if (!all && item.frame + kFramesInFlight > frameNumber_)
            {
                garbage_[kept++] = item;
                continue;
            }
            if (item.pipeline) vkDestroyPipeline(device_, item.pipeline, nullptr);
            if (item.layout) vkDestroyPipelineLayout(device_, item.layout, nullptr);
            if (item.setLayout) vkDestroyDescriptorSetLayout(device_, item.setLayout, nullptr);
            if (item.textureSetLayout)
                vkDestroyDescriptorSetLayout(device_, item.textureSetLayout, nullptr);
            if (item.buffer) vkDestroyBuffer(device_, item.buffer, nullptr);
            if (item.view) vkDestroyImageView(device_, item.view, nullptr);
            if (item.image) vkDestroyImage(device_, item.image, nullptr);
            if (item.sampler) vkDestroySampler(device_, item.sampler, nullptr);
            if (item.memory) vkFreeMemory(device_, item.memory, nullptr);
        }
        garbage_.resize(kept);
    }

    VkFormat textureFormat(TextureFormat format) const
    {
        return format == TextureFormat::Depth24Stencil8 ? depthStencilFormat_ : toVkFormat(format);
    }

    static void imageBarrier(VkCommandBuffer commands, VkImage image, std::uint32_t firstMip,
            std::uint32_t mipCount, VkImageLayout from, VkImageLayout to)
    {
        VkImageMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        barrier.srcAccessMask =
                from == VK_IMAGE_LAYOUT_UNDEFINED
                        ? 0
                        : (VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
        barrier.dstStageMask = to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                       ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                                 VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                                       : VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        barrier.dstAccessMask =
                to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        ? VK_ACCESS_2_SHADER_READ_BIT
                        : (VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
        barrier.oldLayout = from;
        barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = firstMip;
        barrier.subresourceRange.levelCount = mipCount;
        barrier.subresourceRange.layerCount = 1;

        VkDependencyInfo dependency = {};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commands, &dependency);
    }

    bool uploadTexture(const VulkanTexture& texture, const void* data, bool generateMipmaps)
    {
        if (!uploadCommands_)
        {
            VkCommandBufferAllocateInfo allocate = {};
            allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocate.commandPool = commandPool_;
            allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate.commandBufferCount = 1;
            if (vkAllocateCommandBuffers(device_, &allocate, &uploadCommands_) != VK_SUCCESS)
                return false;
        }

        VulkanBuffer staging;
        if (data)
        {
            staging.size = texture.width * texture.height * bytesPerPixel(texture.format);
            staging.vkUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            if (!createVersion(staging, nullptr))
            {
                log("createTexture: could not create the upload buffer");
                return false;
            }
            memcpy(staging.versions[0].mapped, data, staging.size);
        }

        VkCommandBuffer commands = uploadCommands_;
        vkResetCommandBuffer(commands, 0);
        VkCommandBufferBeginInfo begin = {};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commands, &begin);

        if (!data)
        {
            imageBarrier(commands, texture.image, 0, texture.mipLevels, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        else
        {
            imageBarrier(commands, texture.image, 0, texture.mipLevels, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkBufferImageCopy copy = {};
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent.width = texture.width;
            copy.imageExtent.height = texture.height;
            copy.imageExtent.depth = 1;
            vkCmdCopyBufferToImage(commands, staging.versions[0].buffer, texture.image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

            std::uint32_t filled = 1;
            if (generateMipmaps)
            {
                std::int32_t width = static_cast<std::int32_t>(texture.width);
                std::int32_t height = static_cast<std::int32_t>(texture.height);
                for (std::uint32_t mip = 1; mip < texture.mipLevels; ++mip)
                {
                    imageBarrier(commands, texture.image, mip - 1, 1,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                    const std::int32_t nextWidth = width > 1 ? width / 2 : 1;
                    const std::int32_t nextHeight = height > 1 ? height / 2 : 1;

                    VkImageBlit blit = {};
                    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    blit.srcSubresource.mipLevel = mip - 1;
                    blit.srcSubresource.layerCount = 1;
                    blit.srcOffsets[1].x = width;
                    blit.srcOffsets[1].y = height;
                    blit.srcOffsets[1].z = 1;
                    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    blit.dstSubresource.mipLevel = mip;
                    blit.dstSubresource.layerCount = 1;
                    blit.dstOffsets[1].x = nextWidth;
                    blit.dstOffsets[1].y = nextHeight;
                    blit.dstOffsets[1].z = 1;
                    vkCmdBlitImage(commands, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                            VK_FILTER_LINEAR);
                    imageBarrier(commands, texture.image, mip - 1, 1,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    width = nextWidth;
                    height = nextHeight;
                }
                filled = texture.mipLevels;
            }
            const std::uint32_t firstPending = generateMipmaps ? filled - 1 : 0;
            imageBarrier(commands, texture.image, firstPending, texture.mipLevels - firstPending,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        vkEndCommandBuffer(commands);
        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commands;
        const bool submitted = vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS;
        vkQueueWaitIdle(queue_);

        if (data)
        {
            vkDestroyBuffer(device_, staging.versions[0].buffer, nullptr);
            vkFreeMemory(device_, staging.versions[0].memory, nullptr);
        }
        if (!submitted) log("createTexture: could not submit the upload");
        return submitted;
    }

    bool bindTextures(const VulkanPipeline& pipeline, VkCommandBuffer commands)
    {
        VkImageView views[PipelineDesc::kMaxTextures] = {};
        VkSampler samplers[PipelineDesc::kMaxTextures] = {};
        for (std::uint32_t i = 0; i < pipeline.textureCount; ++i)
        {
            const TextureSlot& binding = textureBindings_[pipeline.textureSlots[i]];
            const VulkanTexture* texture =
                    textures_.get(handleCast<TextureSlotHandle>(binding.texture));
            const VulkanSampler* sampler =
                    samplers_.get(handleCast<SamplerSlotHandle>(binding.sampler));
            if (!texture || !sampler)
            {
                log("draw: a texture the pipeline needs is not bound");
                return false;
            }
            views[i] = texture->view;
            samplers[i] = sampler->sampler;
        }

        bool sameSet = lastTextureSet_ != VK_NULL_HANDLE &&
                       lastTextureSetLayout_ == pipeline.textureSetLayout;
        for (std::uint32_t i = 0; sameSet && i < pipeline.textureCount; ++i)
            if (lastViews_[i] != views[i] || lastSamplers_[i] != samplers[i]) sameSet = false;

        if (!sameSet)
        {
            VkDescriptorSetAllocateInfo allocate = {};
            allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocate.descriptorPool = descriptorPools_[frameIndex_];
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &pipeline.textureSetLayout;
            if (vkAllocateDescriptorSets(device_, &allocate, &lastTextureSet_) != VK_SUCCESS)
            {
                lastTextureSet_ = VK_NULL_HANDLE;
                log("draw: out of descriptor sets for this frame");
                return false;
            }

            VkDescriptorImageInfo infos[PipelineDesc::kMaxTextures] = {};
            VkWriteDescriptorSet writes[PipelineDesc::kMaxTextures] = {};
            for (std::uint32_t i = 0; i < pipeline.textureCount; ++i)
            {
                infos[i].sampler = samplers[i];
                infos[i].imageView = views[i];
                infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = lastTextureSet_;
                writes[i].dstBinding = pipeline.textureSlots[i];
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[i].pImageInfo = &infos[i];
                lastViews_[i] = views[i];
                lastSamplers_[i] = samplers[i];
            }
            vkUpdateDescriptorSets(device_, pipeline.textureCount, writes, 0, nullptr);
            lastTextureSetLayout_ = pipeline.textureSetLayout;
        }
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 1, 1,
                &lastTextureSet_, 0, nullptr);
        return true;
    }

    bool bindUniforms(const VulkanPipeline& pipeline, VkCommandBuffer commands)
    {
        VkBuffer buffers[PipelineDesc::kMaxUniformBlocks] = {};
        std::uint32_t ranges[PipelineDesc::kMaxUniformBlocks] = {};
        std::uint32_t offsets[PipelineDesc::kMaxUniformBlocks] = {};
        for (std::uint32_t i = 0; i < pipeline.uniformCount; ++i)
        {
            const UniformBinding& binding = uniformBindings_[pipeline.uniformSlots[i]];
            VulkanBuffer* buffer = buffers_.get(handleCast<BufferSlot>(binding.handle));
            if (!buffer)
            {
                log("draw: a uniform buffer the pipeline needs is not bound");
                return false;
            }
            BufferVersion& version = buffer->versions[buffer->current];
            version.lastUsedFrame = frameNumber_;
            buffers[i] = version.buffer;
            ranges[i] = binding.size;
            offsets[i] = binding.offset;
        }

        bool sameSet = lastSet_ != VK_NULL_HANDLE && lastSetLayout_ == pipeline.setLayout;
        for (std::uint32_t i = 0; sameSet && i < pipeline.uniformCount; ++i)
            if (lastSetBuffers_[i] != buffers[i] || lastSetRanges_[i] != ranges[i]) sameSet = false;

        if (!sameSet)
        {
            VkDescriptorSetAllocateInfo allocate = {};
            allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocate.descriptorPool = descriptorPools_[frameIndex_];
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &pipeline.setLayout;
            if (vkAllocateDescriptorSets(device_, &allocate, &lastSet_) != VK_SUCCESS)
            {
                lastSet_ = VK_NULL_HANDLE;
                log("draw: out of descriptor sets for this frame");
                return false;
            }

            VkDescriptorBufferInfo infos[PipelineDesc::kMaxUniformBlocks] = {};
            VkWriteDescriptorSet writes[PipelineDesc::kMaxUniformBlocks] = {};
            for (std::uint32_t i = 0; i < pipeline.uniformCount; ++i)
            {
                infos[i].buffer = buffers[i];
                infos[i].range = ranges[i];
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = lastSet_;
                writes[i].dstBinding = pipeline.uniformSlots[i];
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                writes[i].pBufferInfo = &infos[i];
                lastSetBuffers_[i] = buffers[i];
                lastSetRanges_[i] = ranges[i];
            }
            vkUpdateDescriptorSets(device_, pipeline.uniformCount, writes, 0, nullptr);
            lastSetLayout_ = pipeline.setLayout;
        }
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 0, 1,
                &lastSet_, pipeline.uniformCount, offsets);
        return true;
    }

    const VulkanPipeline* prepareDraw(std::uint64_t vertexEnd, std::uint32_t instanceCount)
    {
        const VulkanPipeline* pipeline = pipelines_.get(handleCast<PipelineSlot>(pipeline_));
        if (!passActive_ || !pipeline || instanceCount == 0)
        {
            log("draw: no active render pass, no pipeline bound in this pass or no instances");
            return nullptr;
        }
        if (!sameTargets(pipeline->targets, passFormats_))
        {
            log("draw: the pipeline was created for different render targets than this pass");
            return nullptr;
        }

        VkBuffer buffers[PipelineDesc::kMaxVertexBuffers] = {};
        VkDeviceSize offsets[PipelineDesc::kMaxVertexBuffers] = {};
        for (std::uint32_t i = 0; i < pipeline->vertexBufferCount; ++i)
        {
            VulkanBuffer* buffer = buffers_.get(handleCast<BufferSlot>(vertexBuffers_[i]));
            if (!buffer || buffer->usage != BufferUsage::Vertex)
            {
                log("draw: a vertex buffer the pipeline needs is not bound");
                return nullptr;
            }
            const VertexBufferLayout& layout = pipeline->vertexBuffers[i];
            const std::uint64_t count =
                    layout.step == VertexStep::Instance ? instanceCount : vertexEnd;
            if (vertexOffsets_[i] + count * layout.stride > buffer->size)
            {
                log("draw: vertex or instance range is outside the vertex buffer");
                return nullptr;
            }
            BufferVersion& version = buffer->versions[buffer->current];
            version.lastUsedFrame = frameNumber_;
            buffers[i] = version.buffer;
            offsets[i] = vertexOffsets_[i];
        }

        VkCommandBuffer commands = frames_[frameIndex_].commands;
        if (boundPipeline_ != pipeline->pipeline)
        {
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
            boundPipeline_ = pipeline->pipeline;
            uniformDirty_ = true;
            textureDirty_ = true;
        }
        if (vertexDirty_ && pipeline->vertexBufferCount > 0)
        {
            vkCmdBindVertexBuffers(commands, 0, pipeline->vertexBufferCount, buffers, offsets);
            vertexDirty_ = false;
        }
        if (uniformDirty_ && pipeline->uniformCount > 0)
        {
            if (!bindUniforms(*pipeline, commands)) return nullptr;
            uniformDirty_ = false;
        }
        if (textureDirty_ && pipeline->textureCount > 0)
        {
            if (!bindTextures(*pipeline, commands)) return nullptr;
            textureDirty_ = false;
        }
        return pipeline;
    }

    static VkAttachmentLoadOp toLoadOp(LoadOp op)
    {
        switch (op)
        {
            case LoadOp::Load:
                return VK_ATTACHMENT_LOAD_OP_LOAD;
            case LoadOp::Clear:
                return VK_ATTACHMENT_LOAD_OP_CLEAR;
            case LoadOp::DontCare:
                return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessage(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
            const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
    {
        const bool error = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
        char text[1200];
        snprintf(text, sizeof(text), "Vulkan %s: %s", error ? "error" : "warning", data->pMessage);
        static_cast<const VulkanDriver*>(user)->log(text);
        return VK_FALSE;
    }

    static void destroyMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
    {
        const PFN_vkDestroyDebugUtilsMessengerEXT destroy =
                reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) destroy(instance, messenger, nullptr);
    }

    bool createInstance(bool debug)
    {
        std::uint32_t version = VK_API_VERSION_1_0;
        vkEnumerateInstanceVersion(&version);
        if (version < VK_API_VERSION_1_3)
        {
            log("Vulkan: the loader is older than 1.3");
            return false;
        }

        std::uint32_t platformCount = 0;
        const char* const* platformExtensions =
                platform_.instanceExtensions(platform_.user, &platformCount);
        ct::Vector<const char*> extensions;
        for (std::uint32_t i = 0; i < platformCount; ++i)
            extensions.push_back(platformExtensions[i]);

        bool validation = false;
        if (debug)
        {
            std::uint32_t layerCount = 0;
            vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
            ct::Vector<VkLayerProperties> layers;
            layers.resize(layerCount);
            vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
            for (std::uint32_t i = 0; i < layerCount; ++i)
                if (strcmp(layers[i].layerName, kValidationLayer) == 0) validation = true;
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        VkApplicationInfo application = {};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pEngineName = "prisma";
        application.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &application;
        info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        if (validation)
        {
            info.enabledLayerCount = 1;
            info.ppEnabledLayerNames = &kValidationLayer;
        }
        if (vkCreateInstance(&info, nullptr, &instance_) != VK_SUCCESS)
        {
            log("Vulkan: could not create the instance");
            return false;
        }

        if (debug)
        {
            VkDebugUtilsMessengerCreateInfoEXT messenger = {};
            messenger.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            messenger.pfnUserCallback = &VulkanDriver::debugMessage;
            messenger.pUserData = this;
            const PFN_vkCreateDebugUtilsMessengerEXT create =
                    reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
            if (create && create(instance_, &messenger, nullptr, &messenger_) == VK_SUCCESS)
                caps_.debugOutput = validation;
        }
        return true;
    }

    bool pickPhysicalDevice()
    {
        std::uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance_, &count, nullptr);
        ct::Vector<VkPhysicalDevice> devices;
        devices.resize(count);
        vkEnumeratePhysicalDevices(instance_, &count, devices.data());

        int bestScore = -1;
        for (std::uint32_t d = 0; d < count; ++d)
        {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(devices[d], &properties);
            if (properties.apiVersion < VK_API_VERSION_1_3) continue;

            VkPhysicalDeviceVulkan13Features features13 = {};
            features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            VkPhysicalDeviceFeatures2 features = {};
            features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features.pNext = &features13;
            vkGetPhysicalDeviceFeatures2(devices[d], &features);
            if (!features13.dynamicRendering || !features13.synchronization2) continue;

            std::uint32_t familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &familyCount, nullptr);
            ct::Vector<VkQueueFamilyProperties> families;
            families.resize(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &familyCount, families.data());

            std::uint32_t family = familyCount;
            for (std::uint32_t f = 0; f < familyCount && family == familyCount; ++f)
            {
                VkBool32 presents = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(devices[d], f, surface_, &presents);
                if ((families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presents) family = f;
            }
            if (family == familyCount) continue;

            int score = 0;
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score = 2;
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 1;
            if (score > bestScore)
            {
                bestScore = score;
                physicalDevice_ = devices[d];
                queueFamily_ = family;
                caps_.versionMajor = VK_API_VERSION_MAJOR(properties.apiVersion);
                caps_.versionMinor = VK_API_VERSION_MINOR(properties.apiVersion);
                caps_.maxTextureSize = properties.limits.maxImageDimension2D;
                caps_.maxColorTargets = properties.limits.maxColorAttachments;
                caps_.uniformBufferOffsetAlignment = static_cast<std::uint32_t>(
                        properties.limits.minUniformBufferOffsetAlignment);
            }
        }
        if (bestScore < 0)
        {
            log("Vulkan: no device with Vulkan 1.3, dynamic rendering and presentation");
            return false;
        }
        caps_.compute = true;
        caps_.floatColorTargets = true;
        return true;
    }

    bool createDevice()
    {
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue = {};
        queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex = queueFamily_;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;

        VkPhysicalDeviceVulkan13Features features13 = {};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;

        const char* const extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.pNext = &features13;
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queue;
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = &extension;
        if (vkCreateDevice(physicalDevice_, &info, nullptr, &device_) != VK_SUCCESS)
        {
            log("Vulkan: could not create the device");
            return false;
        }
        vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties_);
        VkFormatProperties depthProperties;
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, VK_FORMAT_D32_SFLOAT,
                &depthProperties);
        depthFormat_ = (depthProperties.optimalTilingFeatures &
                               VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                               ? VK_FORMAT_D32_SFLOAT
                               : VK_FORMAT_X8_D24_UNORM_PACK32;
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, VK_FORMAT_D24_UNORM_S8_UINT,
                &depthProperties);
        depthStencilFormat_ = (depthProperties.optimalTilingFeatures &
                                      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                                      ? VK_FORMAT_D24_UNORM_S8_UINT
                                      : VK_FORMAT_D32_SFLOAT_S8_UINT;
        if (messenger_)
            setObjectName_ = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
                    vkGetDeviceProcAddr(device_, "vkSetDebugUtilsObjectNameEXT"));
        return true;
    }

    bool createFrames()
    {
        VkCommandPoolCreateInfo pool = {};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = queueFamily_;
        if (vkCreateCommandPool(device_, &pool, nullptr, &commandPool_) != VK_SUCCESS) return false;

        VkCommandBuffer commands[kFramesInFlight];
        VkCommandBufferAllocateInfo allocate = {};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = commandPool_;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = kFramesInFlight;
        if (vkAllocateCommandBuffers(device_, &allocate, commands) != VK_SUCCESS) return false;

        VkSemaphoreCreateInfo semaphore = {};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fence = {};
        fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (std::uint32_t i = 0; i < kFramesInFlight; ++i)
        {
            frames_[i].commands = commands[i];
            if (vkCreateSemaphore(device_, &semaphore, nullptr, &frames_[i].imageAvailable) !=
                            VK_SUCCESS ||
                    vkCreateFence(device_, &fence, nullptr, &frames_[i].inFlight) != VK_SUCCESS)
                return false;
        }
        return true;
    }

    bool createDescriptorPools()
    {
        const VkDescriptorPoolSize sizes[2] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 8192 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192 },
        };
        VkDescriptorPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.maxSets = 2048;
        info.poolSizeCount = 2;
        info.pPoolSizes = sizes;
        for (std::uint32_t i = 0; i < kFramesInFlight; ++i)
            if (vkCreateDescriptorPool(device_, &info, nullptr, &descriptorPools_[i]) != VK_SUCCESS)
                return false;
        return true;
    }

    void destroyDepth()
    {
        if (depthView_) vkDestroyImageView(device_, depthView_, nullptr);
        if (depthImage_) vkDestroyImage(device_, depthImage_, nullptr);
        if (depthMemory_) vkFreeMemory(device_, depthMemory_, nullptr);
        depthView_ = VK_NULL_HANDLE;
        depthImage_ = VK_NULL_HANDLE;
        depthMemory_ = VK_NULL_HANDLE;
        depthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    bool createDepth()
    {
        VkImageCreateInfo image = {};
        image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = depthFormat_;
        image.extent.width = extent_.width;
        image.extent.height = extent_.height;
        image.extent.depth = 1;
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device_, &image, nullptr, &depthImage_) != VK_SUCCESS) return false;

        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device_, depthImage_, &requirements);
        VkMemoryAllocateInfo allocate = {};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex =
                findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (allocate.memoryTypeIndex == UINT32_MAX ||
                vkAllocateMemory(device_, &allocate, nullptr, &depthMemory_) != VK_SUCCESS ||
                vkBindImageMemory(device_, depthImage_, depthMemory_, 0) != VK_SUCCESS)
            return false;

        VkImageViewCreateInfo view = {};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = depthImage_;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = depthFormat_;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        return vkCreateImageView(device_, &view, nullptr, &depthView_) == VK_SUCCESS;
    }

    void transitionDepth(VkCommandBuffer commands)
    {
        if (depthLayout_ == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) return;

        VkImageMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstStageMask = barrier.srcStageMask;
        barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = depthImage_;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        VkDependencyInfo dependency = {};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commands, &dependency);
        depthLayout_ = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    void destroySwapchain()
    {
        destroyDepth();
        for (std::size_t i = 0; i < images_.size(); ++i)
        {
            vkDestroyImageView(device_, images_[i].view, nullptr);
            vkDestroySemaphore(device_, images_[i].renderFinished, nullptr);
        }
        images_.clear();
        if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }

    bool createSwapchain()
    {
        vkDeviceWaitIdle(device_);

        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities);

        VkExtent2D extent = capabilities.currentExtent;
        if (extent.width == UINT32_MAX)
        {
            platform_.framebufferSize(platform_.user, &extent.width, &extent.height);
            if (extent.width < capabilities.minImageExtent.width)
                extent.width = capabilities.minImageExtent.width;
            if (extent.width > capabilities.maxImageExtent.width)
                extent.width = capabilities.maxImageExtent.width;
            if (extent.height < capabilities.minImageExtent.height)
                extent.height = capabilities.minImageExtent.height;
            if (extent.height > capabilities.maxImageExtent.height)
                extent.height = capabilities.maxImageExtent.height;
        }
        if (extent.width == 0 || extent.height == 0) return false;

        std::uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
        ct::Vector<VkSurfaceFormatKHR> formats;
        formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount,
                formats.data());
        if (formatCount == 0) return false;
        VkSurfaceFormatKHR chosen = formats[0];
        for (std::uint32_t i = 0; i < formatCount; ++i)
        {
            if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
                    formats[i].format == VK_FORMAT_R8G8B8A8_UNORM)
            {
                chosen = formats[i];
                break;
            }
        }

        std::uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
            imageCount = capabilities.maxImageCount;

        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(capabilities.supportedCompositeAlpha & alpha))
            alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

        const VkSwapchainKHR old = swapchain_;
        VkSwapchainCreateInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = surface_;
        info.minImageCount = imageCount;
        info.imageFormat = chosen.format;
        info.imageColorSpace = chosen.colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = capabilities.currentTransform;
        info.compositeAlpha = alpha;
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        info.oldSwapchain = old;

        VkSwapchainKHR created = VK_NULL_HANDLE;
        const VkResult result = vkCreateSwapchainKHR(device_, &info, nullptr, &created);
        destroySwapchain();
        if (result != VK_SUCCESS)
        {
            log("Vulkan: could not create the swapchain");
            return false;
        }
        swapchain_ = created;
        extent_ = extent;
        format_ = chosen.format;

        vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
        ct::Vector<VkImage> images;
        images.resize(imageCount);
        vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, images.data());

        VkSemaphoreCreateInfo semaphore = {};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (std::uint32_t i = 0; i < imageCount; ++i)
        {
            SwapchainImage image;
            image.image = images[i];

            VkImageViewCreateInfo view = {};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = images[i];
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = format_;
            view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view.subresourceRange.levelCount = 1;
            view.subresourceRange.layerCount = 1;
            vkCreateImageView(device_, &view, nullptr, &image.view);
            vkCreateSemaphore(device_, &semaphore, nullptr, &image.renderFinished);
            images_.push_back(image);
        }
        if (!createDepth())
        {
            log("Vulkan: could not create the depth buffer of the window");
            return false;
        }
        return true;
    }

    static void transition(VkCommandBuffer commands, SwapchainImage& target, VkImageLayout from,
            VkImageLayout to)
    {
        if (from == to) return;

        VkImageMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = from == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                        ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                                        : 0;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = to == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                        ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                                        : 0;
        barrier.oldLayout = from;
        barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = target.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        VkDependencyInfo dependency = {};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commands, &dependency);
        target.layout = to;
    }

    VulkanPlatform platform_;
    void (*log_)(const char*);
    Caps caps_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkExtent2D extent_ = { 0, 0 };
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    ct::Vector<SwapchainImage> images_;

    Frame frames_[kFramesInFlight];
    std::uint32_t frameIndex_ = 0;
    std::uint32_t imageIndex_ = 0;
    std::uint32_t requestedWidth_ = 0;
    std::uint32_t requestedHeight_ = 0;
    bool frameReady_ = false;
    bool passActive_ = false;
    TargetFormats passFormats_;
    std::uint32_t passWidth_ = 0;
    std::uint32_t passHeight_ = 0;
    std::uint64_t frameNumber_ = 0;

    VkPhysicalDeviceMemoryProperties memoryProperties_ = {};
    PFN_vkSetDebugUtilsObjectNameEXT setObjectName_ = nullptr;

    ct::SlotMap32<VulkanBuffer> buffers_;
    ct::SlotMap32<VulkanShader> shaders_;
    ct::SlotMap32<VulkanPipeline> pipelines_;
    ct::SlotMap32<VulkanTexture> textures_;
    ct::SlotMap32<VulkanSampler> samplers_;
    ct::Vector<Garbage> garbage_;

    PipelineHandle pipeline_;
    VkPipeline boundPipeline_ = VK_NULL_HANDLE;
    BufferHandle vertexBuffers_[PipelineDesc::kMaxVertexBuffers];
    std::uint32_t vertexOffsets_[PipelineDesc::kMaxVertexBuffers] = {};
    BufferHandle indexBuffer_;
    bool vertexDirty_ = true;
    bool indexDirty_ = true;
    bool uniformDirty_ = true;

    enum : std::uint32_t
    {
        kMaxUniformSlots = 16
    };
    UniformBinding uniformBindings_[kMaxUniformSlots];

    VkDescriptorPool descriptorPools_[kFramesInFlight] = {};
    VkDescriptorSet lastSet_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout lastSetLayout_ = VK_NULL_HANDLE;
    VkBuffer lastSetBuffers_[PipelineDesc::kMaxUniformBlocks] = {};
    std::uint32_t lastSetRanges_[PipelineDesc::kMaxUniformBlocks] = {};

    enum : std::uint32_t
    {
        kMaxTextureSlots = 16
    };
    TextureSlot textureBindings_[kMaxTextureSlots];
    bool textureDirty_ = true;
    VkDescriptorSet lastTextureSet_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout lastTextureSetLayout_ = VK_NULL_HANDLE;
    VkImageView lastViews_[PipelineDesc::kMaxTextures] = {};
    VkSampler lastSamplers_[PipelineDesc::kMaxTextures] = {};
    VkCommandBuffer uploadCommands_ = VK_NULL_HANDLE;
    VkFormat depthStencilFormat_ = VK_FORMAT_D24_UNORM_S8_UINT;

    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
    VkImageLayout depthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
};

} // namespace

Driver* createVulkanDriver(const DriverDesc& desc, DriverError* error)
{
    const VulkanPlatform* vulkan = desc.vulkan;
    if (!vulkan || !vulkan->instanceExtensions || !vulkan->createSurface ||
            !vulkan->framebufferSize)
    {
        *error = DriverError::MissingPlatform;
        return nullptr;
    }

    VulkanDriver* driver = new VulkanDriver(*vulkan, desc.log);
    *error = driver->init(desc.debug);
    if (*error != DriverError::None)
    {
        delete driver;
        return nullptr;
    }
    return driver;
}

} // namespace prisma
