#include "common/ZenApp.h"

#include <stdlib.h>

namespace
{

const char* kVertexSource = ZENAPP_SHADER_HEADER "layout(location = 0) in vec2 aPosition;\n"
                                                 "layout(location = 1) in vec3 aColor;\n"
                                                 "out vec3 vColor;\n"
                                                 "void main()\n"
                                                 "{\n"
                                                 "    vColor = aColor;\n"
                                                 "    gl_Position = vec4(aPosition, 0.5, 1.0);\n"
                                                 "}\n";

const char* kFragmentSource = ZENAPP_SHADER_HEADER "in vec3 vColor;\n"
                                                   "out vec4 oColor;\n"
                                                   "void main()\n"
                                                   "{\n"
                                                   "    oColor = vec4(vColor, 1.0);\n"
                                                   "}\n";

struct Vertex
{
    float position[2];
    float color[3];
};

const Vertex kVertices[3] = {
    { { 0.0f, 0.6f }, { 1.0f, 0.0f, 0.0f } },
    { { -0.6f, -0.6f }, { 0.0f, 1.0f, 0.0f } },
    { { 0.6f, -0.6f }, { 0.0f, 0.0f, 1.0f } },
};

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = zenapp::frameLimit(argc, argv);
    const prisma::DriverType driverType = zenapp::driverType(argc, argv);

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }

    PlatformWindow* window = zenapp::openWindow("prisma triangle", driverType);
    if (!window)
    {
        log_error("window: %s", platform_get_error());
        platform_shutdown();
        return 1;
    }

    prisma::Driver* driver = zenapp::createDriver(window, driverType);
    if (!driver)
    {
        window_destroy(window);
        platform_shutdown();
        return 1;
    }

    prisma::BufferDesc bufferDesc;
    bufferDesc.size = sizeof(kVertices);
    bufferDesc.data = kVertices;
    bufferDesc.debugName = "triangle vertices";
    const prisma::BufferHandle vertexBuffer = driver->createBuffer(bufferDesc);

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
    pipelineDesc.debugName = "triangle pipeline";
    pipelineDesc.attributeCount = 2;
    pipelineDesc.attributes[0].location = 0;
    pipelineDesc.attributes[0].format = prisma::VertexFormat::Float2;
    pipelineDesc.attributes[0].offset = 0;
    pipelineDesc.attributes[1].location = 1;
    pipelineDesc.attributes[1].format = prisma::VertexFormat::Float3;
    pipelineDesc.attributes[1].offset = sizeof(float) * 2;
    const prisma::PipelineHandle pipeline = driver->createPipeline(pipelineDesc);

    driver->destroy(vertexShader);
    driver->destroy(fragmentShader);

    const bool ready = vertexBuffer.valid() && pipeline.valid();
    if (!ready) log_error("triangle: resource creation failed");

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.08f;
    pass.clearColor[1] = 0.08f;
    pass.clearColor[2] = 0.10f;

    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);

        driver->beginFrame();
        driver->beginRenderPass(pass);
        driver->bindPipeline(pipeline);
        driver->bindVertexBuffer(vertexBuffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        driver->endFrame();
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(pipeline);
    driver->destroy(vertexBuffer);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
