#pragma once

#include "platform.h"
#include "prisma/rhi/Driver.h"
#include "prisma/rhi/ShaderBlob.h"

#include <stdlib.h>
#include <string.h>

namespace zenapp
{

#ifdef NDEBUG
const bool kDebug = false;
#else
const bool kDebug = true;
#endif

inline bool hasArgument(int argc, char** argv, const char* name)
{
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], name) == 0) return true;
    return false;
}

inline int frameLimit(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (argv[i][0] >= '0' && argv[i][0] <= '9') return atoi(argv[i]);
    return 0;
}

inline prisma::DriverType driverType(int argc, char** argv)
{
    return hasArgument(argc, argv, "vulkan") ? prisma::DriverType::Vulkan
                                             : prisma::DriverType::OpenGL;
}

inline bool makeCurrent(void* user)
{
    window_make_current(static_cast<PlatformWindow*>(user));
    return true;
}

inline void swapBuffers(void* user) { window_swap(static_cast<PlatformWindow*>(user)); }

inline void framebufferSize(void* user, std::uint32_t* width, std::uint32_t* height)
{
    int w = 0;
    int h = 0;
    window_get_framebuffer_size(static_cast<PlatformWindow*>(user), &w, &h);
    *width = w > 0 ? static_cast<std::uint32_t>(w) : 0;
    *height = h > 0 ? static_cast<std::uint32_t>(h) : 0;
}

inline const char* const* instanceExtensions(void*, std::uint32_t* count)
{
    return vulkan_instance_extensions(count);
}

inline bool createSurface(void* user, void* instance, std::uint64_t* surface)
{
    return vulkan_create_surface(static_cast<PlatformWindow*>(user), instance, nullptr, surface);
}

inline void log(const char* message) { log_error("prisma: %s", message); }

inline PlatformWindow* openWindowEx(const char* title, prisma::DriverType type, int width,
        int height, int x, int y, int monitor, bool resizable, bool vsync)
{
    WindowConfig config = {};
    config.title = title;
    config.width = width;
    config.height = height;
    config.x = x;
    config.y = y;
    config.monitor = monitor;
    config.resizable = resizable;
    config.vsync = vsync;

    if (type == prisma::DriverType::Vulkan)
    {
        if (!vulkan_supported()) return nullptr;
        config.render = RENDER_VULKAN;
        return window_create(&config);
    }

    config.render = RENDER_GL;
    config.gl.debug = kDebug;
#ifdef PRISMA_GLES
    config.gl.profile = GL_PROFILE_ES;
    config.gl.major = 3;
    for (int minor = 2; minor >= 0; --minor)
    {
        config.gl.minor = minor;
        PlatformWindow* window = window_create(&config);
        if (window) return window;
    }
    return nullptr;
#else
    config.gl.profile = GL_PROFILE_CORE;
    config.gl.major = 4;
    config.gl.minor = 6;
    return window_create(&config);
#endif
}

inline PlatformWindow* openWindow(const char* title, prisma::DriverType type)
{
#ifdef MONITOR_MOUSE
    const int monitor = MONITOR_MOUSE;
#else
    const int monitor = MONITOR_CURRENT;
#endif
    return openWindowEx(title, type, 1280, 720, WINDOW_POS_CENTERED, WINDOW_POS_CENTERED, monitor,
            true, true);
}

inline prisma::ShaderHandle createShader(prisma::Driver* driver, const prisma::ShaderBlob& blob,
        const char* debugName = nullptr)
{
    prisma::ShaderDesc desc = prisma::shaderDesc(blob, driver->caps());
    desc.debugName = debugName;
    return driver->createShader(desc);
}

inline prisma::Driver* createDriver(PlatformWindow* window, prisma::DriverType type)
{
    prisma::GLPlatform gl;
    gl.user = window;
    gl.makeCurrent = makeCurrent;
    gl.swapBuffers = swapBuffers;
    gl.framebufferSize = framebufferSize;
    gl.getProcAddress = gl_proc_address;

    prisma::VulkanPlatform vulkan;
    vulkan.user = window;
    vulkan.instanceExtensions = instanceExtensions;
    vulkan.createSurface = createSurface;
    vulkan.framebufferSize = framebufferSize;

    prisma::DriverDesc desc;
    desc.type = type;
    desc.gl = &gl;
    desc.vulkan = &vulkan;
    desc.log = log;
    desc.debug = kDebug;

    prisma::DriverError error = prisma::DriverError::None;
    prisma::Driver* driver = prisma::createDriver(desc, &error);
    if (!driver) log_error("driver: %s", prisma::driverErrorText(error));
    return driver;
}

} // namespace zenapp
