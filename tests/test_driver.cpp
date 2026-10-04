#include "Check.h"
#include "platform.h"
#include "prisma/rhi/Driver.h"
#include "prisma/rhi/gl/GL.h"

#include <string.h>

#ifdef PRISMA_APP_SPIRV
#include "array.frag.h"
#include "cube.frag.h"
#include "flat.frag.h"
#include "flat.vert.h"
#include "instanced.frag.h"
#include "instanced.vert.h"
#include "lod.frag.h"
#include "no_buffer.vert.h"
#include "plain.vert.h"
#include "red.frag.h"
#include "scaled.frag.h"
#include "shadow.frag.h"
#include "textured.frag.h"
#include "textured.vert.h"
#include "two_targets.frag.h"
#include "volume.frag.h"
#define SPIRV(name) name, static_cast<std::uint32_t>(sizeof(name))
#else
#define SPIRV(name) nullptr, 0u
#endif

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

const char* const* instanceExtensions(void*, std::uint32_t* count)
{
    return vulkan_instance_extensions(count);
}

bool createSurface(void* user, void* instance, std::uint64_t* surface)
{
    return vulkan_create_surface(static_cast<PlatformWindow*>(user), instance, nullptr, surface);
}

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

const char* kShadowFragmentSource =
        SHADER_HEADER "in vec2 vUv;\n"
                      "layout(std140) uniform Params { vec4 uColor; vec4 uPlace; };\n"
                      "uniform highp sampler2DShadow uTexture;\n"
                      "out vec4 oColor;\n"
                      "void main()\n"
                      "{\n"
                      "    float lit = texture(uTexture, vec3(vUv, uPlace.w));\n"
                      "    oColor = vec4(lit, lit, lit, 1.0);\n"
                      "}\n";

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
    kParamWhite,
    kParamMaxColor,
    kParamQuarterGrey,
    kParamBlueMid,
    kParamRedMid,
    kParamGreenMid,
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

prisma::Driver* readDriver = nullptr;

prisma::RenderPassDesc keep(const prisma::RenderPassDesc& pass)
{
    prisma::RenderPassDesc next = pass;
    next.colorLoad = prisma::LoadOp::Load;
    next.depthLoad = prisma::LoadOp::Load;
    return next;
}

prisma::ShaderHandle makeShader(prisma::Driver* driver, prisma::ShaderStage stage,
        const char* source, const void* spirv, std::uint32_t spirvSize)
{
    prisma::ShaderDesc desc;
    desc.stage = stage;
    desc.source = source;
    desc.spirv = spirv;
    desc.spirvSize = spirvSize;
    return driver->createShader(desc);
}

bool pixelIs(int x, int y, int r, int g, int b, int tolerance = 1)
{
    unsigned char pixel[4] = { 0, 0, 0, 0 };
    prisma::Rect rect;
    rect.x = x;
    rect.y = 240 - 1 - y;
    rect.width = 1;
    rect.height = 1;
    if (!readDriver->readPixels(prisma::RenderTarget(), rect, pixel))
    {
        printf("pixel (%d,%d) could not be read\n", x, y);
        return false;
    }
    const int dr = pixel[0] - r;
    const int dg = pixel[1] - g;
    const int db = pixel[2] - b;
    const bool same = dr >= -tolerance && dr <= tolerance && dg >= -tolerance && dg <= tolerance &&
                      db >= -tolerance && db <= tolerance;
    if (!same) printf("pixel (%d,%d) is %d,%d,%d\n", x, y, pixel[0], pixel[1], pixel[2]);
    return same;
}

} // namespace

int main(int argc, char** argv)
{
    using namespace prisma;

    static unsigned char paramBytes[kMaxParamStride * kParamCount];

    const bool useVulkan = argc > 1 && strcmp(argv[1], "vulkan") == 0;

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

    PlatformWindow* window = nullptr;
    if (useVulkan)
    {
        if (!vulkan_supported())
        {
            printf("vulkan is not supported\n");
            platform_shutdown();
            return 1;
        }
        config.render = RENDER_VULKAN;
        window = window_create(&config);
    }
    else
    {
        config.render = RENDER_GL;
        config.gl.debug = true;
#ifdef PRISMA_GLES
        config.gl.profile = GL_PROFILE_ES;
        config.gl.major = 3;
        for (int minor = 2; minor >= 0 && !window; --minor)
        {
            config.gl.minor = minor;
            window = window_create(&config);
        }
#else
        config.gl.profile = GL_PROFILE_CORE;
        config.gl.major = 4;
        config.gl.minor = 6;
        window = window_create(&config);
#endif
    }
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

    VulkanPlatform vulkan;
    vulkan.user = window;
    vulkan.instanceExtensions = instanceExtensions;
    vulkan.createSurface = createSurface;
    vulkan.framebufferSize = framebufferSize;

    DriverDesc desc;
    desc.type = useVulkan ? DriverType::Vulkan : DriverType::OpenGL;
    desc.gl = &gl;
    desc.vulkan = &vulkan;
    desc.log = captureLog;
    desc.debug = true;

    DriverError error = DriverError::None;
    Driver* driver = createDriver(desc, &error);
    CHECK(driver != nullptr);
    CHECK(error == DriverError::None);

    if (driver)
    {
        readDriver = driver;
        CHECK(driver->type() == desc.type);
        CHECK(driver->caps().maxTextureSize >= 2048);
        CHECK(driver->caps().maxColorTargets >= 4);

        if (driver->type() == DriverType::OpenGL)
        {
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
        }
        else
        {
            CHECK(!driver->caps().gles);
            CHECK(driver->caps().versionMajor == 1 && driver->caps().versionMinor >= 3);
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

        const ShaderHandle vertexShader =
                makeShader(driver, ShaderStage::Vertex, kVertexSource, SPIRV(plain_vert));
        const ShaderHandle fragmentShader =
                makeShader(driver, ShaderStage::Fragment, kFragmentSource, SPIRV(red_frag));

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
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 26, 64, 115));
        driver->beginRenderPass(keep(pass));
        driver->bindPipeline(pipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));
        CHECK(pixelIs(2, 2, 255, 0, 0));
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

        const Params white = { { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamWhite, white);
        const Params maxColor = { { 0.2f, 0.8f, 0.2f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamMaxColor, maxColor);
        const Params quarterGrey = { { 0.25f, 0.25f, 0.25f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamQuarterGrey, quarterGrey);
        const Params blueMid = { { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.5f, 0.0f } };
        setParams(paramBytes, stride, kParamBlueMid, blueMid);
        const Params redMid = { { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.5f, 0.0f } };
        setParams(paramBytes, stride, kParamRedMid, redMid);
        const Params greenMid = { { 0.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.5f, 0.0f } };
        setParams(paramBytes, stride, kParamGreenMid, greenMid);

        BufferDesc paramsDesc;
        paramsDesc.usage = BufferUsage::Uniform;
        paramsDesc.size = stride * kParamCount;
        paramsDesc.data = paramBytes;
        paramsDesc.update = BufferUpdate::Dynamic;
        const BufferHandle params = driver->createBuffer(paramsDesc);
        CHECK(quad.valid());
        CHECK(quadIndices.valid());
        CHECK(params.valid());

        const ShaderHandle flatVertex =
                makeShader(driver, ShaderStage::Vertex, kFlatVertexSource, SPIRV(flat_vert));
        const ShaderHandle flatFragment =
                makeShader(driver, ShaderStage::Fragment, kFlatFragmentSource, SPIRV(flat_frag));

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
        driver->endRenderPass();
        CHECK(pixelIs(80, 120, 0, 255, 0));
        CHECK(pixelIs(240, 120, 0, 0, 0));

        driver->beginRenderPass(keep(black));
        driver->bindUniformBuffer(2, params, kParamNearBlue * stride, sizeof(Params));
        driver->bindPipeline(flatDepth);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->bindUniformBuffer(2, params, kParamFarRed * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 255));

        driver->beginRenderPass(keep(black));
        driver->bindUniformBuffer(2, params, kParamBehindNear * stride, sizeof(Params));
        driver->bindPipeline(flat);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 255));

        driver->beginRenderPass(keep(black));
        driver->bindUniformBuffer(2, params, kParamBeyondFar * stride, sizeof(Params));
        driver->bindPipeline(flat);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 255));

        driver->beginRenderPass(keep(black));
        driver->bindUniformBuffer(2, params, kParamHalfWhite * stride, sizeof(Params));
        driver->bindPipeline(flatBlend);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 128, 128, 255, 2));

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
        driver->endRenderPass();
        CHECK(pixelIs(80, 180, 255, 0, 0));
        CHECK(pixelIs(80, 60, 0, 0, 0));
        CHECK(pixelIs(240, 180, 0, 0, 0));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(flat);
        driver->bindVertexBuffer(0, buffer, 0);
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
        driver->endRenderPass();
        CHECK(pixelIs(240, 60, 0, 255, 0));
        CHECK(pixelIs(80, 180, 255, 0, 0));
        CHECK(pixelIs(240, 180, 0, 0, 0));
        CHECK(pixelIs(80, 60, 0, 0, 0));

        driver->beginRenderPass(black);
        driver->endRenderPass();
        CHECK(pixelIs(80, 180, 0, 0, 0));
        CHECK(pixelIs(240, 60, 0, 0, 0));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        BufferDesc instanceDesc;
        instanceDesc.size = sizeof(kInstances);
        instanceDesc.data = kInstances;
        const BufferHandle instanceBuffer = driver->createBuffer(instanceDesc);

        const ShaderHandle instancedVertex = makeShader(driver, ShaderStage::Vertex,
                kInstancedVertexSource, SPIRV(instanced_vert));
        const ShaderHandle instancedFragment = makeShader(driver, ShaderStage::Fragment,
                kInstancedFragmentSource, SPIRV(instanced_frag));

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
        driver->endRenderPass();
        CHECK(pixelIs(64, 108, 255, 0, 0));
        CHECK(pixelIs(224, 108, 0, 0, 255));

        driver->beginRenderPass(black);
        driver->bindPipeline(instanced);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindVertexBuffer(1, instanceBuffer, sizeof(Instance));
        driver->draw(3, 0, 1);
        driver->endRenderPass();
        CHECK(pixelIs(224, 108, 0, 0, 255));
        CHECK(pixelIs(64, 108, 0, 0, 0));
        CHECK(messages == 0);

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(instanced);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindVertexBuffer(1, instanceBuffer, sizeof(Instance));
        driver->draw(3, 0, 2);
        CHECK(messages == 1);
        driver->endRenderPass();
        driver->endFrame();
        driver->present();

        driver->destroy(instanced);
        driver->destroy(instancedVertex);
        driver->destroy(instancedFragment);
        driver->destroy(instanceBuffer);

        const ShaderHandle noBufferVertex = makeShader(driver, ShaderStage::Vertex,
                kNoBufferVertexSource, SPIRV(no_buffer_vert));
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
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));
        CHECK(pixelIs(2, 2, 255, 0, 0));
        CHECK(messages == 0);
        driver->beginRenderPass(keep(black));
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

        const ShaderHandle texturedVertex = makeShader(driver, ShaderStage::Vertex,
                kTexturedVertexSource, SPIRV(textured_vert));
        const ShaderHandle texturedFragment = makeShader(driver, ShaderStage::Fragment,
                kTexturedFragmentSource, SPIRV(textured_frag));

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
        driver->endRenderPass();
        CHECK(pixelIs(80, 60, 255, 0, 0));
        CHECK(pixelIs(240, 60, 0, 255, 0));
        CHECK(pixelIs(80, 180, 0, 0, 255));
        CHECK(pixelIs(240, 180, 255, 255, 255));
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

            const ShaderHandle scaledFragment = makeShader(driver, ShaderStage::Fragment,
                    kScaledFragmentSource, SPIRV(scaled_frag));
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
            driver->endRenderPass();
            CHECK(pixelIs(160, 120, 255, 128, 32, 2));
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
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 128, 128, 128, 2));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(textured);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, tenBitTarget, nearest);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 128, 128, 128, 2));
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

        const ShaderHandle twoTargetsFragment = makeShader(driver, ShaderStage::Fragment,
                kTwoTargetsFragmentSource, SPIRV(two_targets_frag));
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
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(textured);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, second, nearest);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 255, 0));
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

        const ShaderHandle arrayFragment =
                makeShader(driver, ShaderStage::Fragment, kArrayFragmentSource, SPIRV(array_frag));
        const ShaderHandle cubeFragment =
                makeShader(driver, ShaderStage::Fragment, kCubeFragmentSource, SPIRV(cube_frag));
        const ShaderHandle volumeFragment = makeShader(driver, ShaderStage::Fragment,
                kVolumeFragmentSource, SPIRV(volume_frag));
        const ShaderHandle lodFragment =
                makeShader(driver, ShaderStage::Fragment, kLodFragmentSource, SPIRV(lod_frag));

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
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(arrayPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, arrayTexture, nearest);
        driver->bindUniformBuffer(2, params, kParamSliceOne * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 255, 0));

        driver->updateTexture(arrayTexture, 0, 1, kBlueTexels);
        driver->beginRenderPass(black);
        driver->bindPipeline(arrayPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, arrayTexture, nearest);
        driver->bindUniformBuffer(2, params, kParamSliceOne * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 255));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(arrayPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, arrayTexture, nearest);
        driver->bindUniformBuffer(2, params, kParamSliceZero * stride, sizeof(Params));
        driver->draw(3, 0);
        messages = 0;
        driver->updateTexture(arrayTexture, 0, 1, kBlueTexels);
        CHECK(messages == 1);
        messages = 0;
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));

        driver->beginRenderPass(black);
        driver->bindPipeline(cubePipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, cubeTexture, nearest);
        driver->bindUniformBuffer(2, params, kParamCubePositiveX * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(cubePipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, cubeTexture, nearest);
        driver->bindUniformBuffer(2, params, kParamCubeNegativeY * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 255, 0));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(cubePipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, cubeTexture, nearest);
        driver->bindUniformBuffer(2, params, kParamCubeNegativeZ * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 255));

        driver->beginRenderPass(black);
        driver->bindPipeline(volumePipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, volumeTexture, volumeSampler);
        driver->bindUniformBuffer(2, params, kParamSliceQuarter * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(volumePipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, volumeTexture, volumeSampler);
        driver->bindUniformBuffer(2, params, kParamSliceThreeQuarters * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 255));
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
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(lodPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, explicitMips, lodSampler);
        driver->bindUniformBuffer(2, params, kParamSliceOne * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 255));

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
        messages = 0;
        driver->generateMipmaps(explicitMips);
        CHECK(messages == 1);
        messages = 0;
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 128, 0, 128, 3));
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
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 255));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(arrayPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, layers, nearest);
        driver->bindUniformBuffer(2, params, kParamSliceOne * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 255, 0));

        driver->beginRenderPass(mipPass);
        driver->endRenderPass();
        driver->beginRenderPass(black);
        driver->bindPipeline(lodPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, mipTarget, lodSampler);
        driver->bindUniformBuffer(2, params, kParamSliceOne * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));
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

        PipelineDesc stateDesc = flatPipelineDesc;
        stateDesc.blend = false;
        stateDesc.depthTest = false;
        stateDesc.targets = TargetFormats();

        PipelineDesc stencilWriteDesc = stateDesc;
        stencilWriteDesc.stencilTest = true;
        stencilWriteDesc.stencilFront.compare = CompareOp::Always;
        stencilWriteDesc.stencilFront.passOp = StencilOp::Replace;
        stencilWriteDesc.stencilBack = stencilWriteDesc.stencilFront;
        stencilWriteDesc.colorMask = 0;
        PipelineDesc stencilEqualDesc = stateDesc;
        stencilEqualDesc.stencilTest = true;
        stencilEqualDesc.stencilFront.compare = CompareOp::Equal;
        stencilEqualDesc.stencilBack = stencilEqualDesc.stencilFront;
        PipelineDesc stencilNotEqualDesc = stencilEqualDesc;
        stencilNotEqualDesc.stencilFront.compare = CompareOp::NotEqual;
        stencilNotEqualDesc.stencilBack = stencilNotEqualDesc.stencilFront;
        const PipelineHandle stencilWrite = driver->createPipeline(stencilWriteDesc);
        const PipelineHandle stencilEqual = driver->createPipeline(stencilEqualDesc);
        const PipelineHandle stencilNotEqual = driver->createPipeline(stencilNotEqualDesc);
        CHECK(stencilWrite.valid());
        CHECK(stencilEqual.valid());
        CHECK(stencilNotEqual.valid());

        RenderPassDesc keepStencil = keep(black);
        keepStencil.stencilLoad = LoadOp::Load;

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindUniformBuffer(2, params, kParamRed * stride, sizeof(Params));
        driver->setStencilReference(1);
        driver->bindPipeline(stencilWrite);
        driver->bindVertexBuffer(0, quad, 0);
        driver->bindIndexBuffer(quadIndices);
        driver->drawIndexed(6, 0);
        driver->bindPipeline(stencilEqual);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(80, 120, 255, 0, 0));
        CHECK(pixelIs(240, 120, 0, 0, 0));

        driver->beginRenderPass(keepStencil);
        driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
        driver->setStencilReference(1);
        driver->bindPipeline(stencilNotEqual);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(80, 120, 255, 0, 0));
        CHECK(pixelIs(240, 120, 0, 255, 0));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        PipelineDesc maskedDesc = stateDesc;
        maskedDesc.colorMask = kColorRed | kColorAlpha;
        const PipelineHandle masked = driver->createPipeline(maskedDesc);
        CHECK(masked.valid());

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindUniformBuffer(2, params, kParamWhite * stride, sizeof(Params));
        driver->bindPipeline(masked);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        PipelineDesc maxDesc = stateDesc;
        maxDesc.blend = true;
        maxDesc.srcColor = BlendFactor::One;
        maxDesc.dstColor = BlendFactor::One;
        maxDesc.srcAlpha = BlendFactor::One;
        maxDesc.dstAlpha = BlendFactor::One;
        maxDesc.colorBlendOp = BlendOp::Max;
        maxDesc.alphaBlendOp = BlendOp::Max;
        PipelineDesc reverseDesc = maxDesc;
        reverseDesc.colorBlendOp = BlendOp::ReverseSubtract;
        reverseDesc.alphaBlendOp = BlendOp::ReverseSubtract;
        const PipelineHandle blendMax = driver->createPipeline(maxDesc);
        const PipelineHandle blendReverse = driver->createPipeline(reverseDesc);
        CHECK(blendMax.valid());
        CHECK(blendReverse.valid());

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindUniformBuffer(2, params, kParamHalf * stride, sizeof(Params));
        driver->bindPipeline(flat);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->bindUniformBuffer(2, params, kParamMaxColor * stride, sizeof(Params));
        driver->bindPipeline(blendMax);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 128, 204, 128, 2));

        driver->beginRenderPass(keep(black));
        driver->bindUniformBuffer(2, params, kParamQuarterGrey * stride, sizeof(Params));
        driver->bindPipeline(blendReverse);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 64, 140, 64, 2));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        PipelineDesc biasDepthDesc = stateDesc;
        biasDepthDesc.depthTest = true;
        biasDepthDesc.depthCompare = CompareOp::Less;
        PipelineDesc biasedDesc = biasDepthDesc;
        biasedDesc.depthBiasConstant = -1000.0f;
        const PipelineHandle unbiased = driver->createPipeline(biasDepthDesc);
        const PipelineHandle biased = driver->createPipeline(biasedDesc);
        CHECK(unbiased.valid());
        CHECK(biased.valid());

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindPipeline(unbiased);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindUniformBuffer(2, params, kParamBlueMid * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->bindUniformBuffer(2, params, kParamRedMid * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 255));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(biased);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindUniformBuffer(2, params, kParamGreenMid * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 255, 0));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        TextureDesc shadowDesc;
        shadowDesc.format = TextureFormat::Depth32F;
        shadowDesc.width = 16;
        shadowDesc.height = 16;
        shadowDesc.usage = kTextureSampled | kTextureRenderTarget;
        const TextureHandle shadowMap = driver->createTexture(shadowDesc);
        CHECK(shadowMap.valid());

        SamplerDesc comparisonDesc;
        comparisonDesc.minFilter = Filter::Linear;
        comparisonDesc.magFilter = Filter::Linear;
        comparisonDesc.mipFilter = MipFilter::None;
        comparisonDesc.addressU = AddressMode::ClampToEdge;
        comparisonDesc.addressV = AddressMode::ClampToEdge;
        comparisonDesc.compare = true;
        comparisonDesc.compareOp = CompareOp::LessEqual;
        const SamplerHandle comparison = driver->createSampler(comparisonDesc);
        CHECK(comparison.valid());

        const ShaderHandle shadowFragment = makeShader(driver, ShaderStage::Fragment,
                kShadowFragmentSource, SPIRV(shadow_frag));
        texturedPipelineDesc.fragmentShader = shadowFragment;
        const PipelineHandle shadowPipeline = driver->createPipeline(texturedPipelineDesc);
        CHECK(shadowPipeline.valid());

        RenderPassDesc shadowPass;
        shadowPass.depth.texture = shadowMap;
        shadowPass.clearDepth = 0.5f;

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(shadowPass);
        driver->endRenderPass();

        driver->beginRenderPass(black);
        driver->bindPipeline(shadowPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, shadowMap, comparison);
        driver->bindUniformBuffer(2, params, kParamSliceQuarter * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 255, 255));

        driver->beginRenderPass(black);
        driver->bindPipeline(shadowPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, shadowMap, comparison);
        driver->bindUniformBuffer(2, params, kParamSliceThreeQuarters * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 0));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        PipelineDesc wireDesc = stateDesc;
        wireDesc.wireframe = true;
        const PipelineHandle wire = driver->createPipeline(wireDesc);
        CHECK(wire.valid());

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindUniformBuffer(2, params, kParamRed * stride, sizeof(Params));
        driver->bindPipeline(wire);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        if (driver->caps().wireframe)
        {
            CHECK(pixelIs(160, 120, 0, 0, 0));
        }
        else
        {
            CHECK(pixelIs(160, 120, 255, 0, 0));
        }
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        TextureDesc stencilColorDesc;
        stencilColorDesc.width = 32;
        stencilColorDesc.height = 32;
        stencilColorDesc.usage = kTextureSampled | kTextureRenderTarget;
        const TextureHandle stencilColor = driver->createTexture(stencilColorDesc);
        TextureDesc stencilDepthDesc;
        stencilDepthDesc.format = TextureFormat::Depth24Stencil8;
        stencilDepthDesc.width = 32;
        stencilDepthDesc.height = 32;
        stencilDepthDesc.usage = kTextureRenderTarget;
        const TextureHandle stencilDepth = driver->createTexture(stencilDepthDesc);
        CHECK(stencilColor.valid());
        CHECK(stencilDepth.valid());

        stencilWriteDesc.targets.window = false;
        stencilWriteDesc.targets.colorCount = 1;
        stencilWriteDesc.targets.colors[0] = TextureFormat::RGBA8;
        stencilWriteDesc.targets.depth = TextureFormat::Depth24Stencil8;
        stencilEqualDesc.targets = stencilWriteDesc.targets;
        const PipelineHandle stencilWriteOffscreen = driver->createPipeline(stencilWriteDesc);
        const PipelineHandle stencilEqualOffscreen = driver->createPipeline(stencilEqualDesc);
        CHECK(stencilWriteOffscreen.valid());
        CHECK(stencilEqualOffscreen.valid());

        RenderPassDesc stencilOffscreen;
        stencilOffscreen.colors[0].texture = stencilColor;
        stencilOffscreen.colorCount = 1;
        stencilOffscreen.depth.texture = stencilDepth;

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginRenderPass(stencilOffscreen);
        driver->bindUniformBuffer(2, params, kParamRed * stride, sizeof(Params));
        driver->setStencilReference(1);
        driver->bindPipeline(stencilWriteOffscreen);
        driver->bindVertexBuffer(0, quad, 0);
        driver->bindIndexBuffer(quadIndices);
        driver->drawIndexed(6, 0);
        driver->bindPipeline(stencilEqualOffscreen);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();

        driver->beginRenderPass(black);
        driver->bindPipeline(textured);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, stencilColor, nearest);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(80, 120, 255, 0, 0));
        CHECK(pixelIs(240, 120, 0, 0, 0));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        CHECK(driver->caps().occlusionQueries);
        const QueryHandle visibleQuery = driver->createQuery(QueryType::Occlusion);
        const QueryHandle hiddenQuery = driver->createQuery(QueryType::Occlusion);
        const QueryHandle timeQuery = driver->createQuery(QueryType::Time);
        CHECK(visibleQuery.valid());
        CHECK(hiddenQuery.valid());
        CHECK(timeQuery.valid() == driver->caps().timerQueries);

        std::uint64_t visibleResult = 7;
        std::uint64_t hiddenResult = 7;
        std::uint64_t timeResult = 0;
        CHECK(!driver->queryResult(visibleQuery, &visibleResult));
        CHECK(!driver->queryResult(QueryHandle(), &visibleResult));

        messages = 0;
        window_begin_frame(window);
        driver->beginFrame();
        driver->beginQuery(visibleQuery);
        CHECK(messages == 1);
        driver->endQuery(visibleQuery);
        CHECK(messages == 2);
        driver->beginRenderPass(black);
        driver->beginQuery(visibleQuery);
        driver->beginQuery(hiddenQuery);
        CHECK(messages == 3);
        driver->beginQuery(visibleQuery);
        CHECK(messages == 4);
        driver->endQuery(visibleQuery);
        driver->endRenderPass();
        driver->endFrame();
        driver->present();
        CHECK(messages == 4);

        messages = 0;
        bool visibleReady = false;
        bool hiddenReady = false;
        bool timeReady = !timeQuery.valid();
        int queryFrames = 0;
        for (; queryFrames < 60 && !(visibleReady && hiddenReady && timeReady && queryFrames >= 6);
                ++queryFrames)
        {
            window_begin_frame(window);
            driver->beginFrame();
            if (timeQuery.valid()) driver->beginQuery(timeQuery);
            driver->beginRenderPass(black);
            driver->bindPipeline(flatDepth);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->bindUniformBuffer(2, params, kParamNearBlue * stride, sizeof(Params));
            driver->beginQuery(visibleQuery);
            driver->draw(3, 0);
            driver->endQuery(visibleQuery);
            driver->bindUniformBuffer(2, params, kParamFarRed * stride, sizeof(Params));
            driver->beginQuery(hiddenQuery);
            driver->draw(3, 0);
            driver->endQuery(hiddenQuery);
            driver->endRenderPass();
            if (timeQuery.valid()) driver->endQuery(timeQuery);
            driver->endFrame();
            driver->present();

            visibleReady = driver->queryResult(visibleQuery, &visibleResult);
            hiddenReady = driver->queryResult(hiddenQuery, &hiddenResult);
            if (timeQuery.valid()) timeReady = driver->queryResult(timeQuery, &timeResult);
        }
        CHECK(visibleReady);
        CHECK(hiddenReady);
        CHECK(timeReady);
        CHECK(visibleResult == 1);
        CHECK(hiddenResult == 0);
        if (timeQuery.valid())
        {
            CHECK(timeResult > 0);
            CHECK(timeResult < 1000000000ull);
        }
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);
        printf("queries: ready after %d frames, gpu time %llu ns\n", queryFrames,
                static_cast<unsigned long long>(timeResult));

        driver->destroy(timeQuery);
        driver->destroy(hiddenQuery);
        driver->destroy(visibleQuery);
        CHECK(!driver->queryResult(visibleQuery, &visibleResult));

        driver->destroy(stencilEqualOffscreen);
        driver->destroy(stencilWriteOffscreen);
        driver->destroy(stencilDepth);
        driver->destroy(stencilColor);
        driver->destroy(wire);
        driver->destroy(shadowPipeline);
        driver->destroy(shadowFragment);
        driver->destroy(comparison);
        driver->destroy(shadowMap);
        driver->destroy(biased);
        driver->destroy(unbiased);
        driver->destroy(blendReverse);
        driver->destroy(blendMax);
        driver->destroy(masked);
        driver->destroy(stencilNotEqual);
        driver->destroy(stencilEqual);
        driver->destroy(stencilWrite);

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
