#pragma once

#include "platform.h"
#include "prisma/rhi/Driver.h"

#include <stdio.h>

namespace zengl
{

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
    config.gl.profile = GL_PROFILE_CORE;
    config.gl.major = 4;
    config.gl.minor = 6;
    config.resizable = true;
    config.vsync = true;
    return window_create(&config);
}

inline prisma::Driver* createDriver(PlatformWindow* window)
{
    prisma::GLPlatform gl;
    gl.user = window;
    gl.makeCurrent = makeCurrent;
    gl.swapBuffers = swapBuffers;
    gl.framebufferSize = framebufferSize;

    prisma::DriverDesc desc;
    desc.type = prisma::DriverType::OpenGL;
    desc.gl = &gl;
    desc.log = log;

    prisma::DriverError error = prisma::DriverError::None;
    prisma::Driver* driver = prisma::createDriver(desc, &error);
    if (!driver) printf("driver: %s\n", prisma::driverErrorText(error));
    return driver;
}

} // namespace zengl
