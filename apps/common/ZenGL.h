#pragma once

#include "platform.h"
#include "prisma/rhi/Driver.h"

#include <stdio.h>

namespace zengl
{

#ifdef PRISMA_GLES
#define ZENGL_SHADER_HEADER "#version 300 es\nprecision mediump float;\n"
#else
#define ZENGL_SHADER_HEADER "#version 460 core\n"
#endif

#ifdef NDEBUG
const bool kDebug = false;
#else
const bool kDebug = true;
#endif

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

inline void log(const char* message) { printf("prisma: %s\n", message); }

inline PlatformWindow* openWindow(const char* title)
{
    WindowConfig config = {};
    config.title = title;
    config.width = 1280;
    config.height = 720;
    config.x = WINDOW_POS_CENTERED;
    config.y = WINDOW_POS_CENTERED;
    config.monitor = MONITOR_CURRENT;
    config.render = RENDER_GL;
    config.gl.debug = kDebug;
    config.resizable = true;
    config.vsync = true;
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

inline prisma::Driver* createDriver(PlatformWindow* window)
{
    prisma::GLPlatform gl;
    gl.user = window;
    gl.makeCurrent = makeCurrent;
    gl.swapBuffers = swapBuffers;
    gl.framebufferSize = framebufferSize;
    gl.getProcAddress = gl_proc_address;

    prisma::DriverDesc desc;
    desc.type = prisma::DriverType::OpenGL;
    desc.gl = &gl;
    desc.log = log;
    desc.debug = kDebug;

    prisma::DriverError error = prisma::DriverError::None;
    prisma::Driver* driver = prisma::createDriver(desc, &error);
    if (!driver) printf("driver: %s\n", prisma::driverErrorText(error));
    return driver;
}

} // namespace zengl
