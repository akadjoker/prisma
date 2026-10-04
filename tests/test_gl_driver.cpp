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

const char* kFlatVertexSource =
        SHADER_HEADER "layout(location = 0) in vec2 aPosition;\n"
                      "layout(std140) uniform Params { vec4 uColor; vec4 uPlace; };\n"
                      "void main() { gl_Position = vec4(aPosition, uPlace.z, 1.0); }\n";

const char* kFlatFragmentSource =
        SHADER_HEADER "layout(std140) uniform Params { vec4 uColor; vec4 uPlace; };\n"
                      "out vec4 oColor;\n"
                      "void main() { oColor = uColor; }\n";

const float kLeftHalfQuad[8] = { -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 1.0f, -1.0f, 1.0f };
const std::uint16_t kQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };

const char* kTexturedVertexSource = SHADER_HEADER
        "layout(location = 0) in vec2 aPosition;\n"
        "out vec2 vUv;\n"
        "void main() { vUv = aPosition * 0.5 + 0.5; gl_Position = vec4(aPosition, 0.0, 1.0); }\n";

const char* kTexturedFragmentSource =
        SHADER_HEADER "in vec2 vUv;\n"
                      "uniform sampler2D uTexture;\n"
                      "out vec4 oColor;\n"
                      "void main() { oColor = texture(uTexture, vUv); }\n";

const char* kScaledFragmentSource =
        SHADER_HEADER "in vec2 vUv;\n"
                      "uniform sampler2D uTexture;\n"
                      "out vec4 oColor;\n"
                      "void main() { oColor = vec4(texture(uTexture, vUv).rgb * 0.25, 1.0); }\n";

const char* kTwoTargetsFragmentSource = SHADER_HEADER
        "layout(location = 0) out vec4 oFirst;\n"
        "layout(location = 1) out vec4 oSecond;\n"
        "void main() { oFirst = vec4(1.0, 0.0, 0.0, 1.0); oSecond = vec4(0.0, 1.0, 0.0, 1.0); }\n";

const unsigned char kFourTexels[16] = {
    255,
    0,
    0,
    255,
    0,
    255,
    0,
    255,
    0,
    0,
    255,
    255,
    255,
    255,
    255,
    255,
};

struct Params
{
    float color[4];
    float place[4];
};

bool pixelIs(int x, int y, int r, int g, int b, int tolerance = 1)
{
    unsigned char pixel[4] = { 0, 0, 0, 0 };
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    const int dr = pixel[0] - r;
    const int dg = pixel[1] - g;
    const int db = pixel[2] - b;
    const bool same = dr >= -tolerance && dr <= tolerance && dg >= -tolerance && dg <= tolerance &&
                      db >= -tolerance && db <= tolerance;
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

        BufferDesc quadDesc;
        quadDesc.size = sizeof(kLeftHalfQuad);
        quadDesc.data = kLeftHalfQuad;
        const BufferHandle quad = driver->createBuffer(quadDesc);

        BufferDesc indexDesc;
        indexDesc.usage = BufferUsage::Index;
        indexDesc.size = sizeof(kQuadIndices);
        indexDesc.data = kQuadIndices;
        const BufferHandle quadIndices = driver->createBuffer(indexDesc);

        BufferDesc paramsDesc;
        paramsDesc.usage = BufferUsage::Uniform;
        paramsDesc.size = sizeof(Params);
        paramsDesc.update = BufferUpdate::Dynamic;
        const BufferHandle params = driver->createBuffer(paramsDesc);
        CHECK(quad.valid());
        CHECK(quadIndices.valid());
        CHECK(params.valid());
        CHECK(driver->caps().uniformBufferOffsetAlignment >= 1);

        ShaderDesc flatDesc;
        flatDesc.source = kFlatVertexSource;
        const ShaderHandle flatVertex = driver->createShader(flatDesc);
        flatDesc.stage = ShaderStage::Fragment;
        flatDesc.source = kFlatFragmentSource;
        const ShaderHandle flatFragment = driver->createShader(flatDesc);

        PipelineDesc flatPipelineDesc;
        flatPipelineDesc.vertexShader = flatVertex;
        flatPipelineDesc.fragmentShader = flatFragment;
        flatPipelineDesc.vertexStride = sizeof(float) * 2;
        flatPipelineDesc.attributeCount = 1;
        flatPipelineDesc.attributes[0].format = VertexFormat::Float2;
        flatPipelineDesc.uniformBlockCount = 1;
        flatPipelineDesc.uniformBlocks[0].name = "Params";
        flatPipelineDesc.uniformBlocks[0].slot = 2;
        const PipelineHandle flat = driver->createPipeline(flatPipelineDesc);
        flatPipelineDesc.depthTest = true;
        const PipelineHandle flatDepth = driver->createPipeline(flatPipelineDesc);
        flatPipelineDesc.depthTest = false;
        flatPipelineDesc.blend = true;
        flatPipelineDesc.srcColor = BlendFactor::SrcAlpha;
        flatPipelineDesc.dstColor = BlendFactor::OneMinusSrcAlpha;
        const PipelineHandle flatBlend = driver->createPipeline(flatPipelineDesc);
        CHECK(flat.valid());
        CHECK(flatDepth.valid());
        CHECK(flatBlend.valid());

        RenderPassDesc black;
        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindUniformBuffer(2, params, 0, sizeof(Params));

        const Params green = { { 0.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        driver->updateBuffer(params, 0, &green, sizeof(green));
        driver->bindPipeline(flat);
        driver->bindVertexBuffer(quad, 0);
        driver->bindIndexBuffer(quadIndices, IndexFormat::UInt16);
        driver->drawIndexed(6, 0);
        CHECK(pixelIs(80, 120, 0, 255, 0));
        CHECK(pixelIs(240, 120, 0, 0, 0));

        const Params nearBlue = { { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, -0.5f, 0.0f } };
        driver->updateBuffer(params, 0, &nearBlue, sizeof(nearBlue));
        driver->bindPipeline(flatDepth);
        driver->bindVertexBuffer(buffer, 0);
        driver->draw(3, 0);
        const Params farRed = { { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.5f, 0.0f } };
        driver->updateBuffer(params, 0, &farRed, sizeof(farRed));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 0, 0, 255));

        const Params halfWhite = { { 1.0f, 1.0f, 1.0f, 0.5f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        driver->updateBuffer(params, 0, &halfWhite, sizeof(halfWhite));
        driver->bindPipeline(flatBlend);
        driver->bindVertexBuffer(buffer, 0);
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 128, 128, 255, 2));

        driver->endRenderPass();
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        TextureDesc textureDesc;
        textureDesc.width = 2;
        textureDesc.height = 2;
        textureDesc.data = kFourTexels;
        textureDesc.debugName = "test texture";
        const TextureHandle texture = driver->createTexture(textureDesc);
        CHECK(texture.valid());
        messages = 0;
        CHECK(!driver->createTexture(TextureDesc()).valid());
        CHECK(messages == 1);

        static unsigned char grey[16 * 16 * 4];
        TextureDesc mippedDesc;
        mippedDesc.width = 16;
        mippedDesc.height = 16;
        mippedDesc.mipLevels = 0;
        mippedDesc.data = grey;
        mippedDesc.generateMipmaps = true;
        const TextureHandle mipped = driver->createTexture(mippedDesc);
        CHECK(mipped.valid());

        SamplerDesc samplerDesc;
        samplerDesc.minFilter = Filter::Nearest;
        samplerDesc.magFilter = Filter::Nearest;
        samplerDesc.mipFilter = MipFilter::None;
        samplerDesc.addressU = AddressMode::ClampToEdge;
        samplerDesc.addressV = AddressMode::ClampToEdge;
        const SamplerHandle nearest = driver->createSampler(samplerDesc);
        CHECK(nearest.valid());

        ShaderDesc texturedDesc;
        texturedDesc.source = kTexturedVertexSource;
        const ShaderHandle texturedVertex = driver->createShader(texturedDesc);
        texturedDesc.stage = ShaderStage::Fragment;
        texturedDesc.source = kTexturedFragmentSource;
        const ShaderHandle texturedFragment = driver->createShader(texturedDesc);

        PipelineDesc texturedPipelineDesc;
        texturedPipelineDesc.vertexShader = texturedVertex;
        texturedPipelineDesc.fragmentShader = texturedFragment;
        texturedPipelineDesc.vertexStride = sizeof(float) * 2;
        texturedPipelineDesc.attributeCount = 1;
        texturedPipelineDesc.attributes[0].format = VertexFormat::Float2;
        texturedPipelineDesc.textureCount = 1;
        texturedPipelineDesc.textures[0].name = "uTexture";
        texturedPipelineDesc.textures[0].slot = 3;
        const PipelineHandle textured = driver->createPipeline(texturedPipelineDesc);
        CHECK(textured.valid());

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindPipeline(textured);
        driver->bindVertexBuffer(buffer, 0);
        driver->bindTexture(3, texture, nearest);
        driver->draw(3, 0);
        CHECK(pixelIs(80, 60, 255, 0, 0));
        CHECK(pixelIs(240, 60, 0, 255, 0));
        CHECK(pixelIs(80, 180, 0, 0, 255));
        CHECK(pixelIs(240, 180, 255, 255, 255));
        driver->endRenderPass();
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        if (driver->caps().floatColorTargets)
        {
            TextureDesc hdrDesc;
            hdrDesc.format = TextureFormat::RGBA16F;
            hdrDesc.width = 64;
            hdrDesc.height = 64;
            hdrDesc.usage = kTextureSampled | kTextureRenderTarget;
            const TextureHandle hdr = driver->createTexture(hdrDesc);
            hdrDesc.format = TextureFormat::Depth32F;
            hdrDesc.usage = kTextureRenderTarget;
            const TextureHandle hdrDepth = driver->createTexture(hdrDesc);
            CHECK(hdr.valid());
            CHECK(hdrDepth.valid());

            ShaderDesc scaledDesc;
            scaledDesc.stage = ShaderStage::Fragment;
            scaledDesc.source = kScaledFragmentSource;
            const ShaderHandle scaledFragment = driver->createShader(scaledDesc);
            texturedPipelineDesc.fragmentShader = scaledFragment;
            const PipelineHandle scaled = driver->createPipeline(texturedPipelineDesc);
            texturedPipelineDesc.fragmentShader = texturedFragment;
            CHECK(scaled.valid());

            RenderPassDesc offscreen;
            offscreen.colors[0] = hdr;
            offscreen.colorCount = 1;
            offscreen.depth = hdrDepth;
            offscreen.depthStore = StoreOp::Discard;

            messages = 0;
            window_begin_frame(window);
            driver->beginFrame();
            driver->beginRenderPass(offscreen);
            driver->bindUniformBuffer(2, params, 0, sizeof(Params));
            const Params nearBright = { { 4.0f, 2.0f, 0.5f, 1.0f }, { 0.0f, 0.0f, -0.5f, 0.0f } };
            driver->updateBuffer(params, 0, &nearBright, sizeof(nearBright));
            driver->bindPipeline(flatDepth);
            driver->bindVertexBuffer(buffer, 0);
            driver->draw(3, 0);
            const Params farGreen = { { 0.0f, 9.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.5f, 0.0f } };
            driver->updateBuffer(params, 0, &farGreen, sizeof(farGreen));
            driver->draw(3, 0);
            driver->endRenderPass();

            driver->beginRenderPass(black);
            driver->bindPipeline(scaled);
            driver->bindVertexBuffer(buffer, 0);
            driver->bindTexture(3, hdr, nearest);
            driver->draw(3, 0);
            CHECK(pixelIs(160, 120, 255, 128, 32, 2));
            driver->endRenderPass();
            driver->endFrame();
            driver->present();
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);

            driver->destroy(scaled);
            driver->destroy(scaledFragment);
            driver->destroy(hdrDepth);
            driver->destroy(hdr);
        }

        TextureDesc targetDesc;
        targetDesc.width = 32;
        targetDesc.height = 32;
        targetDesc.usage = kTextureSampled | kTextureRenderTarget;
        const TextureHandle first = driver->createTexture(targetDesc);
        const TextureHandle second = driver->createTexture(targetDesc);
        CHECK(first.valid());
        CHECK(second.valid());

        ShaderDesc twoTargetsDesc;
        twoTargetsDesc.stage = ShaderStage::Fragment;
        twoTargetsDesc.source = kTwoTargetsFragmentSource;
        const ShaderHandle twoTargetsFragment = driver->createShader(twoTargetsDesc);
        PipelineDesc twoTargetsPipelineDesc;
        twoTargetsPipelineDesc.vertexShader = vertexShader;
        twoTargetsPipelineDesc.fragmentShader = twoTargetsFragment;
        twoTargetsPipelineDesc.vertexStride = sizeof(float) * 2;
        twoTargetsPipelineDesc.attributeCount = 1;
        twoTargetsPipelineDesc.attributes[0].format = VertexFormat::Float2;
        const PipelineHandle twoTargets = driver->createPipeline(twoTargetsPipelineDesc);
        CHECK(twoTargets.valid());

        RenderPassDesc twoTargetsPass;
        twoTargetsPass.colors[0] = first;
        twoTargetsPass.colors[1] = second;
        twoTargetsPass.colorCount = 2;

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(twoTargetsPass);
        driver->bindPipeline(twoTargets);
        driver->bindVertexBuffer(buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();

        driver->beginRenderPass(black);
        driver->bindPipeline(textured);
        driver->bindVertexBuffer(buffer, 0);
        driver->bindTexture(3, first, nearest);
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 255, 0, 0));
        driver->bindTexture(3, second, nearest);
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 0, 255, 0));
        driver->endRenderPass();
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        messages = 0;
        RenderPassDesc invalidPass;
        invalidPass.colors[0] = texture;
        invalidPass.colorCount = 1;
        driver->beginRenderPass(invalidPass);
        driver->endRenderPass();
        CHECK(messages == 1);

        driver->destroy(twoTargets);
        driver->destroy(twoTargetsFragment);
        driver->destroy(second);
        driver->destroy(first);

        driver->destroy(textured);
        driver->destroy(texturedVertex);
        driver->destroy(texturedFragment);
        driver->destroy(nearest);
        driver->destroy(mipped);
        driver->destroy(texture);

        driver->destroy(flatBlend);
        driver->destroy(flatDepth);
        driver->destroy(flat);
        driver->destroy(flatVertex);
        driver->destroy(flatFragment);
        driver->destroy(params);
        driver->destroy(quadIndices);
        driver->destroy(quad);

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
