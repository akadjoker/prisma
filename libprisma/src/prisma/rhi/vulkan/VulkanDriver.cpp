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

struct VulkanBuffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    std::uint32_t size = 0;
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
    TargetFormats targets;
    std::uint32_t vertexBufferCount = 0;
    VertexBufferLayout vertexBuffers[PipelineDesc::kMaxVertexBuffers];
};

struct Garbage
{
    std::uint64_t frame = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

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
            }
            for (VulkanShader& shader: shaders_)
                vkDestroyShaderModule(device_, shader.module, nullptr);
            for (VulkanBuffer& buffer: buffers_)
            {
                vkDestroyBuffer(device_, buffer.buffer, nullptr);
                vkFreeMemory(device_, buffer.memory, nullptr);
            }
            collectGarbage(true);
            destroySwapchain();
            for (std::uint32_t i = 0; i < kFramesInFlight; ++i)
            {
                if (frames_[i].imageAvailable)
                    vkDestroySemaphore(device_, frames_[i].imageAvailable, nullptr);
                if (frames_[i].inFlight) vkDestroyFence(device_, frames_[i].inFlight, nullptr);
            }
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
        if (!createFrames()) return DriverError::ContextFailed;
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

        VkBufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = desc.size;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        switch (desc.usage)
        {
            case BufferUsage::Vertex:
                info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                break;
            case BufferUsage::Index:
                info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
                break;
            case BufferUsage::Uniform:
                info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                break;
        }
        if (vkCreateBuffer(device_, &info, nullptr, &buffer.buffer) != VK_SUCCESS)
        {
            log("createBuffer: could not create the buffer");
            return BufferHandle();
        }

        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device_, buffer.buffer, &requirements);
        VkMemoryAllocateInfo allocate = {};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (allocate.memoryTypeIndex == UINT32_MAX ||
                vkAllocateMemory(device_, &allocate, nullptr, &buffer.memory) != VK_SUCCESS)
        {
            log("createBuffer: could not allocate memory");
            vkDestroyBuffer(device_, buffer.buffer, nullptr);
            return BufferHandle();
        }
        vkBindBufferMemory(device_, buffer.buffer, buffer.memory, 0);

        if (desc.data) writeBuffer(buffer, 0, desc.data, desc.size);
        setName(VK_OBJECT_TYPE_BUFFER, (std::uint64_t) buffer.buffer, desc.debugName);
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
        const VulkanBuffer* buffer = buffers_.get(handleCast<BufferSlot>(handle));
        if (!buffer || !data || static_cast<std::uint64_t>(offset) + size > buffer->size)
        {
            log("updateBuffer: invalid buffer handle or range");
            return;
        }
        writeBuffer(*buffer, offset, data, size);
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
                     desc.targets.colorCount <= TargetFormats::kMaxColors;
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
                desc.targets.window ? VK_FORMAT_UNDEFINED : toVkFormat(desc.targets.depth);

        VulkanPipeline pipeline;
        pipeline.targets = desc.targets;
        pipeline.vertexBufferCount = desc.vertexBufferCount;
        for (std::uint32_t i = 0; i < desc.vertexBufferCount; ++i)
            pipeline.vertexBuffers[i] = desc.vertexBuffers[i];

        VkPipelineLayoutCreateInfo layout = {};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        if (vkCreatePipelineLayout(device_, &layout, nullptr, &pipeline.layout) != VK_SUCCESS)
        {
            log("createPipeline: could not create the pipeline layout");
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
            return PipelineHandle();
        }
        setName(VK_OBJECT_TYPE_PIPELINE, (std::uint64_t) pipeline.pipeline, desc.debugName);
        return handleCast<PipelineHandle>(pipelines_.insert(pipeline));
    }

    TextureHandle createTexture(const TextureDesc&) override { return missing<TextureHandle>(); }
    SamplerHandle createSampler(const SamplerDesc&) override { return missing<SamplerHandle>(); }

    void destroy(BufferHandle handle) override
    {
        const BufferSlot slot = handleCast<BufferSlot>(handle);
        const VulkanBuffer* buffer = buffers_.get(slot);
        if (!buffer) return;
        Garbage item;
        item.frame = frameNumber_;
        item.buffer = buffer->buffer;
        item.memory = buffer->memory;
        garbage_.push_back(item);
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
        garbage_.push_back(item);
        pipelines_.erase(slot);
    }

    void destroy(TextureHandle) override {}
    void destroy(SamplerHandle) override {}

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

        VkRenderingAttachmentInfo color = {};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = target.view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = toLoadOp(desc.colorLoad);
        color.storeOp = desc.colorStore == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE
                                                          : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        for (int i = 0; i < 4; ++i) color.clearValue.color.float32[i] = desc.clearColor[i];

        VkRenderingInfo rendering = {};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = extent_;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
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
        vertexDirty_ = true;
        indexDirty_ = true;

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

    void bindUniformBuffer(std::uint32_t, BufferHandle, std::uint32_t, std::uint32_t) override
    {
        log("Vulkan: uniform buffers are not implemented yet");
    }

    void bindTexture(std::uint32_t, TextureHandle, SamplerHandle) override
    {
        log("Vulkan: textures are not implemented yet");
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
        if (!prepareDraw(0, instanceCount)) return;

        const VulkanBuffer* indices = buffers_.get(handleCast<BufferSlot>(indexBuffer_));
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

        VkCommandBuffer commands = frames_[frameIndex_].commands;
        if (indexDirty_)
        {
            vkCmdBindIndexBuffer(commands, indices->buffer, 0,
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
    template<typename Handle>
    Handle missing() const
    {
        log("Vulkan: resources are not implemented yet");
        return Handle();
    }

    void writeBuffer(const VulkanBuffer& buffer, std::uint32_t offset, const void* data,
            std::uint32_t size)
    {
        void* mapped = nullptr;
        if (vkMapMemory(device_, buffer.memory, offset, size, 0, &mapped) != VK_SUCCESS)
        {
            log("Vulkan: could not map the buffer memory");
            return;
        }
        memcpy(mapped, data, size);
        vkUnmapMemory(device_, buffer.memory);
    }

    void log(const char* message) const
    {
        if (log_) log_(message);
    }

    using BufferSlot = ct::Handle32<VulkanBuffer>;
    using ShaderSlot = ct::Handle32<VulkanShader>;
    using PipelineSlot = ct::Handle32<VulkanPipeline>;

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
            if (item.buffer) vkDestroyBuffer(device_, item.buffer, nullptr);
            if (item.memory) vkFreeMemory(device_, item.memory, nullptr);
        }
        garbage_.resize(kept);
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
            const VulkanBuffer* buffer = buffers_.get(handleCast<BufferSlot>(vertexBuffers_[i]));
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
            buffers[i] = buffer->buffer;
            offsets[i] = vertexOffsets_[i];
        }

        VkCommandBuffer commands = frames_[frameIndex_].commands;
        if (boundPipeline_ != pipeline->pipeline)
        {
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
            boundPipeline_ = pipeline->pipeline;
        }
        if (vertexDirty_ && pipeline->vertexBufferCount > 0)
        {
            vkCmdBindVertexBuffers(commands, 0, pipeline->vertexBufferCount, buffers, offsets);
            vertexDirty_ = false;
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

    void destroySwapchain()
    {
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
    ct::Vector<Garbage> garbage_;

    PipelineHandle pipeline_;
    VkPipeline boundPipeline_ = VK_NULL_HANDLE;
    BufferHandle vertexBuffers_[PipelineDesc::kMaxVertexBuffers];
    std::uint32_t vertexOffsets_[PipelineDesc::kMaxVertexBuffers] = {};
    BufferHandle indexBuffer_;
    bool vertexDirty_ = true;
    bool indexDirty_ = true;
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
