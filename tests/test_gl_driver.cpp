#include "Check.h"
#include "platform.h"
#include "prisma/rhi/Driver.h"
#include "prisma/rhi/gl/GL.h"

#include <string.h>

#ifdef PRISMA_GLES
#define SHADER_HEADER "#version 300 es\nprecision highp float;\n"
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
                      "void main() { gl_Position = vec4(aPosition, 0.5, 1.0); }\n";

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
        "void main() { vUv = aPosition * 0.5 + 0.5; gl_Position = vec4(aPosition, 0.5, 1.0); }\n";

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

const char* kInstancedVertexSource =
        SHADER_HEADER "layout(location = 0) in vec2 aPosition;\n"
                      "layout(location = 1) in vec4 aPlace;\n"
                      "layout(location = 2) in vec4 aColor;\n"
                      "out vec4 vColor;\n"
                      "void main()\n"
                      "{\n"
                      "    vColor = aColor;\n"
                      "    gl_Position = vec4(aPosition * aPlace.z + aPlace.xy, 0.5, 1.0);\n"
                      "}\n";

const char* kInstancedFragmentSource = SHADER_HEADER "in vec4 vColor;\n"
                                                     "out vec4 oColor;\n"
                                                     "void main() { oColor = vColor; }\n";

struct Instance
{
    float place[4];
    unsigned char color[4];
};

const Instance kInstances[2] = {
    { { -0.5f, 0.0f, 0.25f, 0.0f }, { 255, 0, 0, 255 } },
    { { 0.5f, 0.0f, 0.25f, 0.0f }, { 0, 0, 255, 255 } },
};

const char* kNoBufferVertexSource = SHADER_HEADER
        "void main()\n"
        "{\n"
        "    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
        "    gl_Position = vec4(corner * 2.0 - 1.0, 0.5, 1.0);\n"
        "}\n";

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

const char* kArrayFragmentSource =
        SHADER_HEADER "in vec2 vUv;\n"
                      "layout(std140) uniform Params { vec4 uColor; vec4 uPlace; };\n"
                      "uniform highp sampler2DArray uTexture;\n"
                      "out vec4 oColor;\n"
                      "void main() { oColor = texture(uTexture, vec3(vUv, uPlace.w)); }\n";

const char* kCubeFragmentSource =
        SHADER_HEADER "in vec2 vUv;\n"
                      "layout(std140) uniform Params { vec4 uColor; vec4 uPlace; };\n"
                      "uniform samplerCube uTexture;\n"
                      "out vec4 oColor;\n"
                      "void main() { oColor = texture(uTexture, uPlace.xyz); }\n";

const char* kVolumeFragmentSource =
        SHADER_HEADER "in vec2 vUv;\n"
                      "layout(std140) uniform Params { vec4 uColor; vec4 uPlace; };\n"
                      "uniform highp sampler3D uTexture;\n"
                      "out vec4 oColor;\n"
                      "void main() { oColor = texture(uTexture, vec3(vUv, uPlace.w)); }\n";

const char* kLodFragmentSource =
        SHADER_HEADER "in vec2 vUv;\n"
                      "layout(std140) uniform Params { vec4 uColor; vec4 uPlace; };\n"
                      "uniform sampler2D uTexture;\n"
                      "out vec4 oColor;\n"
                      "void main() { oColor = textureLod(uTexture, vUv, uPlace.w); }\n";

const unsigned char kBlueTexels[16] = { 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
    255 };

struct Params
{
    float color[4];
    float place[4];
};

enum ParamIndex
{
    kParamGreen,
    kParamNearBlue,
    kParamFarRed,
    kParamBehindNear,
    kParamBeyondFar,
    kParamHalfWhite,
    kParamRed,
    kParamNearBright,
    kParamFarGreen,
    kParamHalf,
    kParamSliceZero,
    kParamSliceOne,
    kParamSliceQuarter,
    kParamSliceThreeQuarters,
    kParamCubePositiveX,
    kParamCubeNegativeY,
    kParamCubeNegativeZ,
    kParamCount
};

enum
{
    kMaxParamStride = 1024
};

void setParams(unsigned char* bytes, unsigned int stride, int index, const Params& value)
{
    memcpy(bytes + stride * index, &value, sizeof(Params));
}

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

    static unsigned char paramBytes[kMaxParamStride * kParamCount];

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
        pipelineDesc.vertexBuffers[0].stride = sizeof(float) * 2;
        pipelineDesc.vertexBufferCount = 1;
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
        driver->bindVertexBuffer(0, buffer, 0);
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

        const unsigned int alignment = driver->caps().uniformBufferOffsetAlignment;
        const unsigned int stride = (sizeof(Params) + alignment - 1) / alignment * alignment;
        CHECK(alignment >= 1);
        CHECK(stride * kParamCount <= sizeof(paramBytes));

        const Params green = { { 0.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamGreen, green);
        const Params nearBlue = { { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.2f, 0.0f } };
        setParams(paramBytes, stride, kParamNearBlue, nearBlue);
        const Params farRed = { { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.8f, 0.0f } };
        setParams(paramBytes, stride, kParamFarRed, farRed);
        const Params behindNear = { { 1.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -0.5f, 0.0f } };
        setParams(paramBytes, stride, kParamBehindNear, behindNear);
        const Params beyondFar = { { 1.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.5f, 0.0f } };
        setParams(paramBytes, stride, kParamBeyondFar, beyondFar);
        const Params halfWhite = { { 1.0f, 1.0f, 1.0f, 0.5f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamHalfWhite, halfWhite);
        const Params red = { { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamRed, red);
        const Params nearBright = { { 4.0f, 2.0f, 0.5f, 1.0f }, { 0.0f, 0.0f, 0.2f, 0.0f } };
        setParams(paramBytes, stride, kParamNearBright, nearBright);
        const Params farGreen = { { 0.0f, 9.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.8f, 0.0f } };
        setParams(paramBytes, stride, kParamFarGreen, farGreen);
        const Params half = { { 0.5f, 0.5f, 0.5f, 1.0f }, { 0.0f, 0.0f, 0.5f, 0.0f } };
        setParams(paramBytes, stride, kParamHalf, half);

        const Params sliceZero = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamSliceZero, sliceZero);
        const Params sliceOne = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f } };
        setParams(paramBytes, stride, kParamSliceOne, sliceOne);
        const Params sliceQuarter = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.25f } };
        setParams(paramBytes, stride, kParamSliceQuarter, sliceQuarter);
        const Params sliceThreeQuarters = { { 0.0f, 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f, 0.75f } };
        setParams(paramBytes, stride, kParamSliceThreeQuarters, sliceThreeQuarters);
        const Params cubePositiveX = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamCubePositiveX, cubePositiveX);
        const Params cubeNegativeY = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamCubeNegativeY, cubeNegativeY);
        const Params cubeNegativeZ = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f, 0.0f } };
        setParams(paramBytes, stride, kParamCubeNegativeZ, cubeNegativeZ);

        BufferDesc paramsDesc;
        paramsDesc.usage = BufferUsage::Uniform;
        paramsDesc.size = stride * kParamCount;
        paramsDesc.data = paramBytes;
        paramsDesc.update = BufferUpdate::Dynamic;
        const BufferHandle params = driver->createBuffer(paramsDesc);
        CHECK(quad.valid());
        CHECK(quadIndices.valid());
        CHECK(params.valid());

        ShaderDesc flatDesc;
        flatDesc.source = kFlatVertexSource;
        const ShaderHandle flatVertex = driver->createShader(flatDesc);
        flatDesc.stage = ShaderStage::Fragment;
        flatDesc.source = kFlatFragmentSource;
        const ShaderHandle flatFragment = driver->createShader(flatDesc);

        PipelineDesc flatPipelineDesc;
        flatPipelineDesc.vertexShader = flatVertex;
        flatPipelineDesc.fragmentShader = flatFragment;
        flatPipelineDesc.vertexBuffers[0].stride = sizeof(float) * 2;
        flatPipelineDesc.vertexBufferCount = 1;
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
        driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
        driver->bindPipeline(flat);
        driver->bindVertexBuffer(0, quad, 0);
        driver->bindIndexBuffer(quadIndices);
        driver->drawIndexed(12, 0);
        CHECK(messages == 1);
        messages = 0;
        driver->drawIndexed(6, 0);
        CHECK(pixelIs(80, 120, 0, 255, 0));
        CHECK(pixelIs(240, 120, 0, 0, 0));

        driver->bindUniformBuffer(2, params, kParamNearBlue * stride, sizeof(Params));
        driver->bindPipeline(flatDepth);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->bindUniformBuffer(2, params, kParamFarRed * stride, sizeof(Params));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 0, 0, 255));

        driver->bindUniformBuffer(2, params, kParamBehindNear * stride, sizeof(Params));
        driver->bindPipeline(flat);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 0, 0, 255));
        driver->bindUniformBuffer(2, params, kParamBeyondFar * stride, sizeof(Params));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 0, 0, 255));

        driver->bindUniformBuffer(2, params, kParamHalfWhite * stride, sizeof(Params));
        driver->bindPipeline(flatBlend);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 128, 128, 255, 2));

        driver->endRenderPass();
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindUniformBuffer(2, params, kParamRed * stride, sizeof(Params));
        driver->bindPipeline(flat);
        driver->bindVertexBuffer(0, buffer, 0);

        Rect topLeft;
        topLeft.width = 160;
        topLeft.height = 120;
        driver->setScissor(topLeft);
        driver->draw(3, 0);
        CHECK(pixelIs(80, 180, 255, 0, 0));
        CHECK(pixelIs(80, 60, 0, 0, 0));
        CHECK(pixelIs(240, 180, 0, 0, 0));

        Rect whole;
        whole.width = 320;
        whole.height = 240;
        driver->setScissor(whole);
        driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
        Viewport bottomRight;
        bottomRight.x = 160.0f;
        bottomRight.y = 120.0f;
        bottomRight.width = 160.0f;
        bottomRight.height = 120.0f;
        driver->setViewport(bottomRight);
        driver->draw(3, 0);
        CHECK(pixelIs(240, 60, 0, 255, 0));
        CHECK(pixelIs(80, 180, 255, 0, 0));
        CHECK(pixelIs(240, 180, 0, 0, 0));
        CHECK(pixelIs(80, 60, 0, 0, 0));
        driver->endRenderPass();

        driver->beginRenderPass(black);
        CHECK(pixelIs(80, 180, 0, 0, 0));
        CHECK(pixelIs(240, 60, 0, 0, 0));
        driver->endRenderPass();
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        BufferDesc instanceDesc;
        instanceDesc.size = sizeof(kInstances);
        instanceDesc.data = kInstances;
        const BufferHandle instanceBuffer = driver->createBuffer(instanceDesc);

        ShaderDesc instancedDesc;
        instancedDesc.source = kInstancedVertexSource;
        const ShaderHandle instancedVertex = driver->createShader(instancedDesc);
        instancedDesc.stage = ShaderStage::Fragment;
        instancedDesc.source = kInstancedFragmentSource;
        const ShaderHandle instancedFragment = driver->createShader(instancedDesc);

        PipelineDesc instancedPipelineDesc;
        instancedPipelineDesc.vertexShader = instancedVertex;
        instancedPipelineDesc.fragmentShader = instancedFragment;
        instancedPipelineDesc.vertexBufferCount = 2;
        instancedPipelineDesc.vertexBuffers[0].stride = sizeof(float) * 2;
        instancedPipelineDesc.vertexBuffers[1].stride = sizeof(Instance);
        instancedPipelineDesc.vertexBuffers[1].step = VertexStep::Instance;
        instancedPipelineDesc.attributeCount = 3;
        instancedPipelineDesc.attributes[0].location = 0;
        instancedPipelineDesc.attributes[0].format = VertexFormat::Float2;
        instancedPipelineDesc.attributes[1].location = 1;
        instancedPipelineDesc.attributes[1].format = VertexFormat::Float4;
        instancedPipelineDesc.attributes[1].buffer = 1;
        instancedPipelineDesc.attributes[2].location = 2;
        instancedPipelineDesc.attributes[2].format = VertexFormat::UByte4Norm;
        instancedPipelineDesc.attributes[2].offset = sizeof(float) * 4;
        instancedPipelineDesc.attributes[2].buffer = 1;
        const PipelineHandle instanced = driver->createPipeline(instancedPipelineDesc);
        CHECK(instanceBuffer.valid());
        CHECK(instanced.valid());

        messages = 0;
        instancedPipelineDesc.attributes[2].buffer = 2;
        CHECK(!driver->createPipeline(instancedPipelineDesc).valid());
        CHECK(messages == 1);

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindPipeline(instanced);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindVertexBuffer(1, instanceBuffer, 0);
        driver->draw(3, 0, 2);
        CHECK(pixelIs(64, 108, 255, 0, 0));
        CHECK(pixelIs(224, 108, 0, 0, 255));
        driver->endRenderPass();

        driver->beginRenderPass(black);
        driver->bindPipeline(instanced);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindVertexBuffer(1, instanceBuffer, sizeof(Instance));
        driver->draw(3, 0, 1);
        CHECK(pixelIs(224, 108, 0, 0, 255));
        CHECK(pixelIs(64, 108, 0, 0, 0));
        CHECK(messages == 0);
        driver->draw(3, 0, 2);
        CHECK(messages == 1);
        driver->endRenderPass();
        driver->endFrame();
        driver->present();

        driver->destroy(instanced);
        driver->destroy(instancedVertex);
        driver->destroy(instancedFragment);
        driver->destroy(instanceBuffer);

        ShaderDesc noBufferDesc;
        noBufferDesc.source = kNoBufferVertexSource;
        const ShaderHandle noBufferVertex = driver->createShader(noBufferDesc);
        PipelineDesc noBufferPipelineDesc;
        noBufferPipelineDesc.vertexShader = noBufferVertex;
        noBufferPipelineDesc.fragmentShader = fragmentShader;
        const PipelineHandle noBuffer = driver->createPipeline(noBufferPipelineDesc);
        CHECK(noBuffer.valid());

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->draw(3, 0);
        CHECK(messages == 1);
        messages = 0;
        driver->bindPipeline(noBuffer);
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 255, 0, 0));
        CHECK(pixelIs(2, 2, 255, 0, 0));
        CHECK(messages == 0);
        driver->bindPipeline(pipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(1000, 0);
        CHECK(messages == 1);
        messages = 0;
        driver->updateBuffer(params, 0, &green, sizeof(green));
        CHECK(messages == 1);
        messages = 0;
        driver->endRenderPass();
        driver->endFrame();
        driver->present();
        driver->updateBuffer(params, 0xFFFFFFF0u, &green, 0x20u);
        CHECK(messages == 1);
        driver->destroy(noBuffer);
        driver->destroy(noBufferVertex);

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
        texturedPipelineDesc.vertexBuffers[0].stride = sizeof(float) * 2;
        texturedPipelineDesc.vertexBufferCount = 1;
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
        driver->bindVertexBuffer(0, buffer, 0);
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

            flatPipelineDesc.blend = false;
            flatPipelineDesc.depthTest = true;
            flatPipelineDesc.targets.window = false;
            flatPipelineDesc.targets.colorCount = 1;
            flatPipelineDesc.targets.colors[0] = TextureFormat::RGBA16F;
            flatPipelineDesc.targets.depth = TextureFormat::Depth32F;
            const PipelineHandle flatHdr = driver->createPipeline(flatPipelineDesc);
            CHECK(flatHdr.valid());

            RenderPassDesc offscreen;
            offscreen.colors[0].texture = hdr;
            offscreen.colorCount = 1;
            offscreen.depth.texture = hdrDepth;
            offscreen.depthStore = StoreOp::Discard;

            messages = 0;
            window_begin_frame(window);
            driver->beginFrame();
            driver->beginRenderPass(offscreen);
            driver->bindUniformBuffer(2, params, kParamNearBright * stride, sizeof(Params));
            driver->bindPipeline(flatHdr);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->draw(3, 0);
            driver->bindUniformBuffer(2, params, kParamFarGreen * stride, sizeof(Params));
            driver->draw(3, 0);
            driver->endRenderPass();

            driver->beginRenderPass(black);
            driver->bindPipeline(scaled);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->bindTexture(3, hdr, nearest);
            driver->draw(3, 0);
            CHECK(pixelIs(160, 120, 255, 128, 32, 2));
            driver->endRenderPass();
            driver->endFrame();
            driver->present();
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);

            driver->destroy(flatHdr);
            driver->destroy(scaled);
            driver->destroy(scaledFragment);
            driver->destroy(hdrDepth);
            driver->destroy(hdr);
        }

        TextureDesc srgbDesc;
        srgbDesc.format = TextureFormat::RGBA8Srgb;
        srgbDesc.width = 16;
        srgbDesc.height = 16;
        srgbDesc.usage = kTextureSampled | kTextureRenderTarget;
        const TextureHandle srgbTarget = driver->createTexture(srgbDesc);
        srgbDesc.format = TextureFormat::RGB10A2;
        const TextureHandle tenBitTarget = driver->createTexture(srgbDesc);
        CHECK(srgbTarget.valid());
        CHECK(tenBitTarget.valid());

        flatPipelineDesc.blend = false;
        flatPipelineDesc.depthTest = false;
        flatPipelineDesc.targets.window = false;
        flatPipelineDesc.targets.colorCount = 1;
        flatPipelineDesc.targets.colors[0] = TextureFormat::RGBA8Srgb;
        flatPipelineDesc.targets.depth = TextureFormat::None;
        const PipelineHandle flatSrgb = driver->createPipeline(flatPipelineDesc);
        flatPipelineDesc.targets.colors[0] = TextureFormat::RGB10A2;
        const PipelineHandle flatTenBit = driver->createPipeline(flatPipelineDesc);
        CHECK(flatSrgb.valid());
        CHECK(flatTenBit.valid());

        RenderPassDesc srgbPass;
        srgbPass.colors[0].texture = srgbTarget;
        srgbPass.colorCount = 1;
        RenderPassDesc tenBitPass;
        tenBitPass.colors[0].texture = tenBitTarget;
        tenBitPass.colorCount = 1;

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(srgbPass);
        driver->bindUniformBuffer(2, params, kParamHalf * stride, sizeof(Params));
        driver->bindPipeline(flatSrgb);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        driver->beginRenderPass(tenBitPass);
        driver->bindUniformBuffer(2, params, kParamHalf * stride, sizeof(Params));
        driver->bindPipeline(flatTenBit);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
        driver->bindPipeline(flat);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        CHECK(messages == 1);
        messages = 0;
        driver->endRenderPass();

        driver->beginRenderPass(black);
        driver->bindPipeline(textured);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, srgbTarget, nearest);
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 128, 128, 128, 2));
        driver->bindTexture(3, tenBitTarget, nearest);
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 128, 128, 128, 2));
        driver->endRenderPass();
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);
        driver->destroy(flatTenBit);
        driver->destroy(flatSrgb);
        driver->destroy(tenBitTarget);
        driver->destroy(srgbTarget);

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
        twoTargetsPipelineDesc.vertexBuffers[0].stride = sizeof(float) * 2;
        twoTargetsPipelineDesc.vertexBufferCount = 1;
        twoTargetsPipelineDesc.attributeCount = 1;
        twoTargetsPipelineDesc.attributes[0].format = VertexFormat::Float2;
        twoTargetsPipelineDesc.targets.window = false;
        twoTargetsPipelineDesc.targets.colorCount = 2;
        twoTargetsPipelineDesc.targets.colors[0] = TextureFormat::RGBA8;
        twoTargetsPipelineDesc.targets.colors[1] = TextureFormat::RGBA8;
        const PipelineHandle twoTargets = driver->createPipeline(twoTargetsPipelineDesc);
        CHECK(twoTargets.valid());

        RenderPassDesc twoTargetsPass;
        twoTargetsPass.colors[0].texture = first;
        twoTargetsPass.colors[1].texture = second;
        twoTargetsPass.colorCount = 2;

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(twoTargetsPass);
        driver->bindPipeline(twoTargets);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();

        driver->beginRenderPass(black);
        driver->bindPipeline(textured);
        driver->bindVertexBuffer(0, buffer, 0);
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
        invalidPass.colors[0].texture = texture;
        invalidPass.colorCount = 1;
        driver->beginRenderPass(invalidPass);
        driver->endRenderPass();
        CHECK(messages == 1);

        CHECK(driver->caps().maxAnisotropy >= 1.0f);
        SamplerDesc anisotropicDesc;
        anisotropicDesc.maxAnisotropy = 4.0f;
        const SamplerHandle anisotropic = driver->createSampler(anisotropicDesc);
        CHECK(anisotropic.valid());

        ShaderDesc layeredDesc;
        layeredDesc.stage = ShaderStage::Fragment;
        layeredDesc.source = kArrayFragmentSource;
        const ShaderHandle arrayFragment = driver->createShader(layeredDesc);
        layeredDesc.source = kCubeFragmentSource;
        const ShaderHandle cubeFragment = driver->createShader(layeredDesc);
        layeredDesc.source = kVolumeFragmentSource;
        const ShaderHandle volumeFragment = driver->createShader(layeredDesc);
        layeredDesc.source = kLodFragmentSource;
        const ShaderHandle lodFragment = driver->createShader(layeredDesc);

        texturedPipelineDesc.uniformBlockCount = 1;
        texturedPipelineDesc.uniformBlocks[0].name = "Params";
        texturedPipelineDesc.uniformBlocks[0].slot = 2;
        texturedPipelineDesc.fragmentShader = arrayFragment;
        const PipelineHandle arrayPipeline = driver->createPipeline(texturedPipelineDesc);
        texturedPipelineDesc.fragmentShader = cubeFragment;
        const PipelineHandle cubePipeline = driver->createPipeline(texturedPipelineDesc);
        texturedPipelineDesc.fragmentShader = volumeFragment;
        const PipelineHandle volumePipeline = driver->createPipeline(texturedPipelineDesc);
        texturedPipelineDesc.fragmentShader = lodFragment;
        const PipelineHandle lodPipeline = driver->createPipeline(texturedPipelineDesc);
        CHECK(arrayPipeline.valid());
        CHECK(cubePipeline.valid());
        CHECK(volumePipeline.valid());
        CHECK(lodPipeline.valid());

        SamplerDesc volumeSamplerDesc;
        volumeSamplerDesc.minFilter = Filter::Nearest;
        volumeSamplerDesc.magFilter = Filter::Nearest;
        volumeSamplerDesc.mipFilter = MipFilter::None;
        volumeSamplerDesc.addressU = AddressMode::ClampToEdge;
        volumeSamplerDesc.addressV = AddressMode::ClampToEdge;
        volumeSamplerDesc.addressW = AddressMode::ClampToEdge;
        const SamplerHandle volumeSampler = driver->createSampler(volumeSamplerDesc);
        SamplerDesc lodSamplerDesc = volumeSamplerDesc;
        lodSamplerDesc.mipFilter = MipFilter::Nearest;
        const SamplerHandle lodSampler = driver->createSampler(lodSamplerDesc);
        CHECK(volumeSampler.valid());
        CHECK(lodSampler.valid());

        unsigned char arrayTexels[32];
        for (int i = 0; i < 8; ++i)
        {
            const unsigned char color[4] = { static_cast<unsigned char>(i < 4 ? 255 : 0),
                static_cast<unsigned char>(i < 4 ? 0 : 255), 0, 255 };
            memcpy(arrayTexels + i * 4, color, 4);
        }
        TextureDesc arrayDesc;
        arrayDesc.type = TextureType::Texture2DArray;
        arrayDesc.width = 2;
        arrayDesc.height = 2;
        arrayDesc.depth = 2;
        arrayDesc.data = arrayTexels;
        const TextureHandle arrayTexture = driver->createTexture(arrayDesc);
        CHECK(arrayTexture.valid());

        static const unsigned char faceColors[6][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 },
            { 255, 255, 0 }, { 0, 255, 255 }, { 255, 0, 255 } };
        unsigned char cubeTexels[96];
        for (int i = 0; i < 24; ++i)
        {
            const unsigned char* face = faceColors[i / 4];
            const unsigned char color[4] = { face[0], face[1], face[2], 255 };
            memcpy(cubeTexels + i * 4, color, 4);
        }
        TextureDesc cubeDesc;
        cubeDesc.type = TextureType::TextureCube;
        cubeDesc.width = 2;
        cubeDesc.height = 2;
        cubeDesc.data = cubeTexels;
        const TextureHandle cubeTexture = driver->createTexture(cubeDesc);
        CHECK(cubeTexture.valid());

        unsigned char volumeTexels[32];
        for (int i = 0; i < 8; ++i)
        {
            const unsigned char color[4] = { static_cast<unsigned char>(i < 4 ? 255 : 0), 0,
                static_cast<unsigned char>(i < 4 ? 0 : 255), 255 };
            memcpy(volumeTexels + i * 4, color, 4);
        }
        TextureDesc volumeDesc;
        volumeDesc.type = TextureType::Texture3D;
        volumeDesc.width = 2;
        volumeDesc.height = 2;
        volumeDesc.depth = 2;
        volumeDesc.data = volumeTexels;
        const TextureHandle volumeTexture = driver->createTexture(volumeDesc);
        CHECK(volumeTexture.valid());

        unsigned char redTexels[16];
        for (int i = 0; i < 4; ++i)
        {
            const unsigned char color[4] = { 255, 0, 0, 255 };
            memcpy(redTexels + i * 4, color, 4);
        }
        TextureDesc explicitDesc;
        explicitDesc.width = 2;
        explicitDesc.height = 2;
        explicitDesc.mipLevels = 0;
        explicitDesc.data = redTexels;
        const TextureHandle explicitMips = driver->createTexture(explicitDesc);
        CHECK(explicitMips.valid());

        TextureDesc layersDesc;
        layersDesc.type = TextureType::Texture2DArray;
        layersDesc.width = 4;
        layersDesc.height = 4;
        layersDesc.depth = 2;
        layersDesc.usage = kTextureSampled | kTextureRenderTarget;
        const TextureHandle layers = driver->createTexture(layersDesc);
        CHECK(layers.valid());

        TextureDesc mipTargetDesc;
        mipTargetDesc.width = 4;
        mipTargetDesc.height = 4;
        mipTargetDesc.mipLevels = 0;
        mipTargetDesc.usage = kTextureSampled | kTextureRenderTarget;
        const TextureHandle mipTarget = driver->createTexture(mipTargetDesc);
        CHECK(mipTarget.valid());

        flatPipelineDesc.blend = false;
        flatPipelineDesc.depthTest = false;
        flatPipelineDesc.targets.window = false;
        flatPipelineDesc.targets.colorCount = 1;
        flatPipelineDesc.targets.colors[0] = TextureFormat::RGBA8;
        flatPipelineDesc.targets.depth = TextureFormat::None;
        const PipelineHandle flatLayer = driver->createPipeline(flatPipelineDesc);
        CHECK(flatLayer.valid());

        RenderPassDesc layerZeroPass;
        layerZeroPass.colors[0].texture = layers;
        layerZeroPass.colors[0].layer = 0;
        layerZeroPass.colorCount = 1;
        layerZeroPass.clearColor[0] = 0.0f;
        layerZeroPass.clearColor[2] = 1.0f;
        RenderPassDesc layerOnePass;
        layerOnePass.colors[0].texture = layers;
        layerOnePass.colors[0].layer = 1;
        layerOnePass.colorCount = 1;
        RenderPassDesc mipPass;
        mipPass.colors[0].texture = mipTarget;
        mipPass.colors[0].mip = 1;
        mipPass.colorCount = 1;
        mipPass.clearColor[0] = 1.0f;

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindPipeline(arrayPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, arrayTexture, nearest);
        driver->bindUniformBuffer(2, params, kParamSliceZero * stride, sizeof(Params));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 255, 0, 0));
        driver->bindUniformBuffer(2, params, kParamSliceOne * stride, sizeof(Params));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 0, 255, 0));
        driver->endRenderPass();

        driver->updateTexture(arrayTexture, 0, 1, kBlueTexels);
        driver->beginRenderPass(black);
        driver->bindPipeline(arrayPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, arrayTexture, nearest);
        driver->bindUniformBuffer(2, params, kParamSliceOne * stride, sizeof(Params));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 0, 0, 255));
        driver->bindUniformBuffer(2, params, kParamSliceZero * stride, sizeof(Params));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 255, 0, 0));
        messages = 0;
        driver->updateTexture(arrayTexture, 0, 1, kBlueTexels);
        CHECK(messages == 1);
        messages = 0;
        driver->endRenderPass();

        driver->beginRenderPass(black);
        driver->bindPipeline(cubePipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, cubeTexture, nearest);
        driver->bindUniformBuffer(2, params, kParamCubePositiveX * stride, sizeof(Params));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 255, 0, 0));
        driver->bindUniformBuffer(2, params, kParamCubeNegativeY * stride, sizeof(Params));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 255, 255, 0));
        driver->bindUniformBuffer(2, params, kParamCubeNegativeZ * stride, sizeof(Params));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 255, 0, 255));
        driver->endRenderPass();

        driver->beginRenderPass(black);
        driver->bindPipeline(volumePipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, volumeTexture, volumeSampler);
        driver->bindUniformBuffer(2, params, kParamSliceQuarter * stride, sizeof(Params));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 255, 0, 0));
        driver->bindUniformBuffer(2, params, kParamSliceThreeQuarters * stride, sizeof(Params));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 0, 0, 255));
        driver->endRenderPass();
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        messages = 0;
        driver->updateTexture(explicitMips, 1, 0, kBlueTexels);
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindPipeline(lodPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, explicitMips, lodSampler);
        driver->bindUniformBuffer(2, params, kParamSliceZero * stride, sizeof(Params));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 255, 0, 0));
        driver->bindUniformBuffer(2, params, kParamSliceOne * stride, sizeof(Params));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 0, 0, 255));
        driver->endRenderPass();

        unsigned char halfTexels[16];
        for (int i = 0; i < 4; ++i)
        {
            const unsigned char red[4] = { 255, 0, 0, 255 };
            const unsigned char blue[4] = { 0, 0, 255, 255 };
            memcpy(halfTexels + i * 4, i < 2 ? red : blue, 4);
        }
        driver->updateTexture(explicitMips, 0, 0, halfTexels);
        driver->generateMipmaps(explicitMips);
        driver->beginRenderPass(black);
        driver->bindPipeline(lodPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, explicitMips, lodSampler);
        driver->bindUniformBuffer(2, params, kParamSliceOne * stride, sizeof(Params));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 128, 0, 128, 3));
        messages = 0;
        driver->generateMipmaps(explicitMips);
        CHECK(messages == 1);
        messages = 0;
        driver->endRenderPass();
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(layerZeroPass);
        driver->endRenderPass();
        driver->beginRenderPass(layerOnePass);
        driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
        driver->bindPipeline(flatLayer);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();

        driver->beginRenderPass(black);
        driver->bindPipeline(arrayPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, layers, nearest);
        driver->bindUniformBuffer(2, params, kParamSliceZero * stride, sizeof(Params));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 0, 0, 255));
        driver->bindUniformBuffer(2, params, kParamSliceOne * stride, sizeof(Params));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 0, 255, 0));
        driver->endRenderPass();

        driver->beginRenderPass(mipPass);
        driver->endRenderPass();
        driver->beginRenderPass(black);
        driver->bindPipeline(lodPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, mipTarget, lodSampler);
        driver->bindUniformBuffer(2, params, kParamSliceOne * stride, sizeof(Params));
        driver->draw(3, 0);
        CHECK(pixelIs(160, 120, 255, 0, 0));
        driver->endRenderPass();
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        messages = 0;
        RenderPassDesc badLayerPass;
        badLayerPass.colors[0].texture = layers;
        badLayerPass.colors[0].layer = 5;
        badLayerPass.colorCount = 1;
        driver->beginRenderPass(badLayerPass);
        driver->endRenderPass();
        CHECK(messages == 1);
        messages = 0;

        driver->destroy(flatLayer);
        driver->destroy(mipTarget);
        driver->destroy(layers);
        driver->destroy(explicitMips);
        driver->destroy(volumeTexture);
        driver->destroy(cubeTexture);
        driver->destroy(arrayTexture);
        driver->destroy(lodSampler);
        driver->destroy(volumeSampler);
        driver->destroy(lodPipeline);
        driver->destroy(volumePipeline);
        driver->destroy(cubePipeline);
        driver->destroy(arrayPipeline);
        driver->destroy(lodFragment);
        driver->destroy(volumeFragment);
        driver->destroy(cubeFragment);
        driver->destroy(arrayFragment);
        driver->destroy(anisotropic);

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
