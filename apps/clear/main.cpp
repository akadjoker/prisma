#include "platform.h"
#include "prisma/rhi/Driver.h"

#include <stdio.h>
#include <stdlib.h>

namespace
{

bool makeCurrent(void* user)
{
    window_make_current(static_cast<PlatformWindow*>(user));
    return true;
}

void swapBuffers(void* user) { window_swap(static_cast<PlatformWindow*>(user)); }

void framebufferSize(void* user, std::uint32_t* width, std::uint32_t* height)
{
    int w = 0;
    int h = 0;
    window_get_framebuffer_size(static_cast<PlatformWindow*>(user), &w, &h);
    *width = w > 0 ? static_cast<std::uint32_t>(w) : 0;
    *height = h > 0 ? static_cast<std::uint32_t>(h) : 0;
}

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = argc > 1 ? atoi(argv[1]) : 0;

    if (!platform_init())
    {
        printf("platform: %s\n", platform_get_error());
        return 1;
    }

    WindowConfig config = {};
    config.title = "prisma clear";
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

    PlatformWindow* window = window_create(&config);
    if (!window)
    {
        printf("window: %s\n", platform_get_error());
        platform_shutdown();
        return 1;
    }

    prisma::GLPlatform gl;
    gl.user = window;
    gl.makeCurrent = makeCurrent;
    gl.swapBuffers = swapBuffers;
    gl.framebufferSize = framebufferSize;

    prisma::DriverDesc desc;
    desc.type = prisma::DriverType::OpenGL;
    desc.gl = &gl;

    prisma::DriverError error = prisma::DriverError::None;
    prisma::Driver* driver = prisma::createDriver(desc, &error);
    if (!driver)
    {
        printf("driver: %s\n", prisma::driverErrorText(error));
        window_destroy(window);
        platform_shutdown();
        return 1;
    }

    printf("OpenGL driver: max texture %u, max color targets %u\n", driver->caps().maxTextureSize,
            driver->caps().maxColorTargets);

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.10f;
    pass.clearColor[1] = 0.25f;
    pass.clearColor[2] = 0.45f;

    int frames = 0;
    while (!window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);

        driver->beginFrame();
        driver->beginRenderPass(pass);
        driver->endRenderPass();
        driver->endFrame();
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return 0;
}
