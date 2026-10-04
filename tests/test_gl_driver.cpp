#include "Check.h"
#include "platform.h"
#include "prisma/rhi/Driver.h"
#include "prisma/rhi/gl/GL.h"

#include <string.h>

#ifdef PRISMA_GLES
#define SHADER_HEADER "#version 300 es\nprecision mediump float;\n"
#else
#define SHADER_HEADER "#version 460 core\n"
#endif

namespace
{

int messages = 0;
char lastMessage[256];

void captureLog(const char* message)
{
    ++messages;
    strncpy(lastMessage, message, sizeof(lastMessage) - 1);
    lastMessage[sizeof(lastMessage) - 1] = '\0';
}

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
    *width = static_cast<std::uint32_t>(w);
    *height = static_cast<std::uint32_t>(h);
}

const char* kVertexSource =
        SHADER_HEADER "layout(location = 0) in vec2 aPosition;\n"
                      "void main() { gl_Position = vec4(aPosition, 0.0, 1.0); }\n";

const char* kFragmentSource = SHADER_HEADER "out vec4 oColor;\n"
                                            "void main() { oColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";

const float kCoveringTriangle[6] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };

bool pixelIs(int x, int y, int r, int g, int b)
{
    unsigned char pixel[4] = { 0, 0, 0, 0 };
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    const int dr = pixel[0] - r;
    const int dg = pixel[1] - g;
    const int db = pixel[2] - b;
    const bool same = dr >= -1 && dr <= 1 && dg >= -1 && dg <= 1 && db >= -1 && db <= 1;
    if (!same) printf("pixel (%d,%d) is %d,%d,%d\n", x, y, pixel[0], pixel[1], pixel[2]);
    return same;
}

} // namespace

int main()
{
    using namespace prisma;

    if (!platform_init())
    {
        printf("platform: %s\n", platform_get_error());
        return 1;
    }

    WindowConfig config = {};
    config.title = "prisma test";
    config.width = 320;
    config.height = 240;
    config.x = WINDOW_POS_CENTERED;
    config.y = WINDOW_POS_CENTERED;
    config.monitor = MONITOR_CURRENT;
    config.render = RENDER_GL;
    config.gl.debug = true;

#ifdef PRISMA_GLES
    config.gl.profile = GL_PROFILE_ES;
    config.gl.major = 3;
    PlatformWindow* window = nullptr;
    for (int minor = 2; minor >= 0 && !window; --minor)
    {
        config.gl.minor = minor;
        window = window_create(&config);
    }
#else
    config.gl.profile = GL_PROFILE_CORE;
    config.gl.major = 4;
    config.gl.minor = 6;
    PlatformWindow* window = window_create(&config);
#endif
    if (!window)
    {
        printf("window: %s\n", platform_get_error());
        platform_shutdown();
        return 1;
    }

    GLPlatform gl;
    gl.user = window;
    gl.makeCurrent = makeCurrent;
    gl.swapBuffers = swapBuffers;
    gl.framebufferSize = framebufferSize;
    gl.getProcAddress = gl_proc_address;

    DriverDesc desc;
    desc.type = DriverType::OpenGL;
    desc.gl = &gl;
    desc.log = captureLog;
    desc.debug = true;

    DriverError error = DriverError::None;
    Driver* driver = createDriver(desc, &error);
    CHECK(driver != nullptr);
    CHECK(error == DriverError::None);

    if (driver)
    {
        CHECK(driver->type() == DriverType::OpenGL);
        CHECK(driver->caps().maxTextureSize >= 2048);
        CHECK(driver->caps().maxColorTargets >= 4);

#ifdef PRISMA_GLES
        CHECK(driver->caps().gles);
        CHECK(driver->caps().versionMajor == 3);
#else
        CHECK(!driver->caps().gles);
        CHECK(driver->caps().versionMajor == 4 && driver->caps().versionMinor == 6);
#endif
        if (driver->caps().debugOutput)
        {
            messages = 0;
            glEnable(0xDEAD);
            CHECK(messages == 1);
            CHECK(strstr(lastMessage, "GL error") != nullptr);
        }

        messages = 0;
        ShaderDesc broken;
        broken.source = "this is not a shader";
        CHECK(!driver->createShader(broken).valid());
        CHECK(messages >= 1);

        BufferDesc bufferDesc;
        bufferDesc.size = sizeof(kCoveringTriangle);
        bufferDesc.data = kCoveringTriangle;
        bufferDesc.debugName = "test vertices";
        const BufferHandle buffer = driver->createBuffer(bufferDesc);

        ShaderDesc shaderDesc;
        shaderDesc.source = kVertexSource;
        const ShaderHandle vertexShader = driver->createShader(shaderDesc);
        shaderDesc.stage = ShaderStage::Fragment;
        shaderDesc.source = kFragmentSource;
        const ShaderHandle fragmentShader = driver->createShader(shaderDesc);

        PipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = vertexShader;
        pipelineDesc.fragmentShader = fragmentShader;
        pipelineDesc.vertexStride = sizeof(float) * 2;
        pipelineDesc.attributeCount = 1;
        pipelineDesc.attributes[0].format = VertexFormat::Float2;
        pipelineDesc.debugName = "test pipeline";
        const PipelineHandle pipeline = driver->createPipeline(pipelineDesc);
        CHECK(buffer.valid());
        CHECK(pipeline.valid());

        RenderPassDesc pass;
        pass.clearColor[0] = 0.10f;
        pass.clearColor[1] = 0.25f;
        pass.clearColor[2] = 0.45f;

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(pass);
        CHECK(pixelIs(160, 120, 26, 64, 115));
        driver->bindPipeline(pipeline);
        driver->bindVertexBuffer(buffer, 0);
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 255, 0, 0));
        CHECK(pixelIs(2, 2, 255, 0, 0));
        driver->endRenderPass();
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        driver->destroy(pipeline);
        driver->destroy(vertexShader);
        driver->destroy(fragmentShader);
        driver->destroy(buffer);
        destroyDriver(driver);
    }

    window_destroy(window);
    platform_shutdown();

    if (failures)
    {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
