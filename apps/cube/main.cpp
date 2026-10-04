#include "common/ZenGL.h"
#include "mathc.h"

#include <stdlib.h>

namespace
{

const char* kVertexSource =
        ZENGL_SHADER_HEADER "layout(location = 0) in vec3 aPosition;\n"
                            "layout(location = 1) in vec3 aColor;\n"
                            "layout(std140) uniform Frame\n"
                            "{\n"
                            "    mat4 uModelViewProjection;\n"
                            "};\n"
                            "out vec3 vColor;\n"
                            "void main()\n"
                            "{\n"
                            "    vColor = aColor;\n"
                            "    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);\n"
                            "}\n";

const char* kFragmentSource = ZENGL_SHADER_HEADER "in vec3 vColor;\n"
                                                  "out vec4 oColor;\n"
                                                  "void main()\n"
                                                  "{\n"
                                                  "    oColor = vec4(vColor, 1.0);\n"
                                                  "}\n";

struct Vertex
{
    float position[3];
    float color[3];
};

const Vertex kVertices[24] = {
    { { -1, -1, 1 }, { 0.9f, 0.2f, 0.2f } },
    { { 1, -1, 1 }, { 0.9f, 0.2f, 0.2f } },
    { { 1, 1, 1 }, { 0.9f, 0.2f, 0.2f } },
    { { -1, 1, 1 }, { 0.9f, 0.2f, 0.2f } },
    { { 1, -1, -1 }, { 0.2f, 0.8f, 0.3f } },
    { { -1, -1, -1 }, { 0.2f, 0.8f, 0.3f } },
    { { -1, 1, -1 }, { 0.2f, 0.8f, 0.3f } },
    { { 1, 1, -1 }, { 0.2f, 0.8f, 0.3f } },
    { { 1, -1, 1 }, { 0.2f, 0.4f, 0.9f } },
    { { 1, -1, -1 }, { 0.2f, 0.4f, 0.9f } },
    { { 1, 1, -1 }, { 0.2f, 0.4f, 0.9f } },
    { { 1, 1, 1 }, { 0.2f, 0.4f, 0.9f } },
    { { -1, -1, -1 }, { 0.9f, 0.8f, 0.2f } },
    { { -1, -1, 1 }, { 0.9f, 0.8f, 0.2f } },
    { { -1, 1, 1 }, { 0.9f, 0.8f, 0.2f } },
    { { -1, 1, -1 }, { 0.9f, 0.8f, 0.2f } },
    { { -1, 1, 1 }, { 0.8f, 0.3f, 0.8f } },
    { { 1, 1, 1 }, { 0.8f, 0.3f, 0.8f } },
    { { 1, 1, -1 }, { 0.8f, 0.3f, 0.8f } },
    { { -1, 1, -1 }, { 0.8f, 0.3f, 0.8f } },
    { { -1, -1, -1 }, { 0.2f, 0.8f, 0.8f } },
    { { 1, -1, -1 }, { 0.2f, 0.8f, 0.8f } },
    { { 1, -1, 1 }, { 0.2f, 0.8f, 0.8f } },
    { { -1, -1, 1 }, { 0.2f, 0.8f, 0.8f } },
};

const std::uint16_t kIndices[36] = {
    0,
    1,
    2,
    0,
    2,
    3,
    4,
    5,
    6,
    4,
    6,
    7,
    8,
    9,
    10,
    8,
    10,
    11,
    12,
    13,
    14,
    12,
    14,
    15,
    16,
    17,
    18,
    16,
    18,
    19,
    20,
    21,
    22,
    20,
    22,
    23,
};

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = argc > 1 ? atoi(argv[1]) : 0;

    if (!platform_init())
    {
        printf("platform: %s\n", platform_get_error());
        return 1;
    }

    PlatformWindow* window = zengl::openWindow("prisma cube");
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

    prisma::BufferDesc bufferDesc;
    bufferDesc.size = sizeof(kVertices);
    bufferDesc.data = kVertices;
    bufferDesc.debugName = "cube vertices";
    const prisma::BufferHandle vertexBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Index;
    bufferDesc.size = sizeof(kIndices);
    bufferDesc.data = kIndices;
    bufferDesc.debugName = "cube indices";
    const prisma::BufferHandle indexBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(Math::Mat4);
    bufferDesc.data = nullptr;
    bufferDesc.dynamic = true;
    bufferDesc.debugName = "cube frame uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    prisma::ShaderDesc shaderDesc;
    shaderDesc.stage = prisma::ShaderStage::Vertex;
    shaderDesc.source = kVertexSource;
    const prisma::ShaderHandle vertexShader = driver->createShader(shaderDesc);
    shaderDesc.stage = prisma::ShaderStage::Fragment;
    shaderDesc.source = kFragmentSource;
    const prisma::ShaderHandle fragmentShader = driver->createShader(shaderDesc);

    prisma::PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = sizeof(Vertex);
    pipelineDesc.attributeCount = 2;
    pipelineDesc.attributes[0].location = 0;
    pipelineDesc.attributes[0].format = prisma::VertexFormat::Float3;
    pipelineDesc.attributes[0].offset = 0;
    pipelineDesc.attributes[1].location = 1;
    pipelineDesc.attributes[1].format = prisma::VertexFormat::Float3;
    pipelineDesc.attributes[1].offset = sizeof(float) * 3;
    pipelineDesc.uniformBlockCount = 1;
    pipelineDesc.uniformBlocks[0].name = "Frame";
    pipelineDesc.uniformBlocks[0].slot = 0;
    pipelineDesc.depthTest = true;
    pipelineDesc.cullMode = prisma::CullMode::Back;
    pipelineDesc.debugName = "cube pipeline";
    const prisma::PipelineHandle pipeline = driver->createPipeline(pipelineDesc);

    driver->destroy(vertexShader);
    driver->destroy(fragmentShader);

    const bool ready = vertexBuffer.valid() && indexBuffer.valid() && uniformBuffer.valid() &&
                       pipeline.valid();
    if (!ready) printf("cube: resource creation failed\n");

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.08f;
    pass.clearColor[1] = 0.08f;
    pass.clearColor[2] = 0.10f;

    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        const float aspect =
                height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        const float angle = static_cast<float>(time_seconds()) + 0.6f;

        const Math::Mat4 projection = Math::Mat4::Perspective(1.0f, aspect, 0.1f, 100.0f);
        const Math::Mat4 view = Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -5.0f));
        const Math::Mat4 model = Math::Mat4::RotationY(angle) * Math::Mat4::RotationX(angle * 0.7f);
        const Math::Mat4 modelViewProjection = projection * view * model;

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, &modelViewProjection, sizeof(Math::Mat4));
        driver->beginRenderPass(pass);
        driver->bindPipeline(pipeline);
        driver->bindVertexBuffer(vertexBuffer, 0);
        driver->bindIndexBuffer(indexBuffer, prisma::IndexFormat::UInt16);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(Math::Mat4));
        driver->drawIndexed(36, 0);
        driver->endRenderPass();
        driver->endFrame();
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(pipeline);
    driver->destroy(uniformBuffer);
    driver->destroy(indexBuffer);
    driver->destroy(vertexBuffer);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
