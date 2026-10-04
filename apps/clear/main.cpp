#include "common/ZenGL.h"

#include <stdlib.h>

int main(int argc, char** argv)
{
    const int maxFrames = argc > 1 ? atoi(argv[1]) : 0;

    if (!platform_init())
    {
        printf("platform: %s\n", platform_get_error());
        return 1;
    }

    PlatformWindow* window = zengl::openWindow("prisma clear");
    if (!window)
    {
        printf("window: %s\n", platform_get_error());
        platform_shutdown();
        return 1;
    }

    prisma::Driver* driver = zengl::createDriver(window);
    if (!driver)
    {
        window_destroy(window);
        platform_shutdown();
        return 1;
    }

    const prisma::Caps& caps = driver->caps();
    printf("%s %u.%u: max texture %u, max color targets %u, compute %d, debug %d\n",
            caps.gles ? "OpenGL ES" : "OpenGL", caps.versionMajor, caps.versionMinor,
            caps.maxTextureSize, caps.maxColorTargets, caps.compute, caps.debugOutput);

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
