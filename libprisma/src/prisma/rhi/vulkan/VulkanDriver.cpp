#include "prisma/rhi/Driver.h"

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

    BufferHandle createBuffer(const BufferDesc&) override { return missing<BufferHandle>(); }
    ShaderHandle createShader(const ShaderDesc&) override { return missing<ShaderHandle>(); }
    PipelineHandle createPipeline(const PipelineDesc&) override
    {
        return missing<PipelineHandle>();
    }
    TextureHandle createTexture(const TextureDesc&) override { return missing<TextureHandle>(); }
    SamplerHandle createSampler(const SamplerDesc&) override { return missing<SamplerHandle>(); }
    void updateBuffer(BufferHandle, std::uint32_t, const void*, std::uint32_t) override {}
    void destroy(BufferHandle) override {}
    void destroy(ShaderHandle) override {}
    void destroy(PipelineHandle) override {}
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
    }

    void setViewport(const Viewport&) override {}
    void setScissor(const Rect&) override {}
    void bindPipeline(PipelineHandle) override {}
    void bindVertexBuffer(std::uint32_t, BufferHandle, std::uint32_t) override {}
    void bindIndexBuffer(BufferHandle) override {}
    void bindUniformBuffer(std::uint32_t, BufferHandle, std::uint32_t, std::uint32_t) override {}
    void bindTexture(std::uint32_t, TextureHandle, SamplerHandle) override {}
    void draw(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void drawIndexed(std::uint32_t, std::uint32_t, std::uint32_t) override {}

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
        if (presented == VK_ERROR_OUT_OF_DATE_KHR) createSwapchain();
    }

private:
    template<typename Handle>
    Handle missing() const
    {
        log("Vulkan: resources are not implemented yet");
        return Handle();
    }

    void log(const char* message) const
    {
        if (log_) log_(message);
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
