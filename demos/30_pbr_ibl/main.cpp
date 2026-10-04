#include "common/Ibl.h"
#include "common/Projection.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <math.h>
#include <string.h>

#include "pbr.frag.h"
#include "pbr.vert.h"
#include "sky.frag.h"
#include "sky.vert.h"

namespace
{

struct Vertex
{
    float position[3];
    float normal[3];
};

struct FrameUniforms
{
    Math::Mat4 viewProjection;
    Math::Mat4 inverseViewProjection;
    float camera[4];
    float exposure[4];
};

struct ObjectUniforms
{
    Math::Mat4 model;
    float baseColor[4];
    float material[4];
};

const unsigned kColumns = 7;
const unsigned kRows = 3;
const unsigned kSegments = 64;
const unsigned kRings = 32;
const float kSpacing = 1.1f;

const float kRowColors[kRows][4] = { { 0.80f, 0.08f, 0.06f, 1.0f }, { 1.00f, 0.71f, 0.29f, 1.0f },
    { 0.95f, 0.95f, 0.96f, 1.0f } };
const float kRowMetallic[kRows] = { 0.0f, 1.0f, 1.0f };

const char* probeFile(int argc, char** argv)
{
    if (zenapp::hasArgument(argc, argv, "grace")) return "Light Probes/grace_cross.dds";
    if (zenapp::hasArgument(argc, argv, "stpeters")) return "Light Probes/stpeters_cross.dds";
    if (zenapp::hasArgument(argc, argv, "galileo")) return "Light Probes/galileo_cross.dds";
    if (zenapp::hasArgument(argc, argv, "rnl")) return "Light Probes/rnl_cross.dds";
    return "Light Probes/uffizi_cross.dds";
}

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = zenapp::frameLimit(argc, argv);
    const prisma::DriverType driverType = zenapp::driverType(argc, argv);
    const bool still = zenapp::hasArgument(argc, argv, "still");

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }

    PlatformWindow* window = zenapp::openWindow("prisma 30 pbr ibl", driverType);
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

    if (!driver->caps().floatColorTargets)
    {
        log_error("pbr ibl: this GPU cannot render to float textures");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    char path[1024];
    zenapp::mediaPath(probeFile(argc, argv), path, sizeof(path));
    zenapp::EnvironmentFaces faces;
    if (!zenapp::loadEnvironmentFaces(path, &faces))
    {
        log_error("pbr ibl: cannot read %s", path);
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 1;
    }

    zenapp::Ibl ibl;
    const bool iblReady = zenapp::createIbl(driver, faces, &ibl);
    if (!iblReady) log_error("pbr ibl: cannot build the image based lighting resources");

    ct::Vector<Vertex> vertices;
    ct::Vector<std::uint16_t> indices;
    for (unsigned ring = 0; ring <= kRings; ++ring)
    {
        const float theta = 3.14159265f * static_cast<float>(ring) / static_cast<float>(kRings);
        for (unsigned segment = 0; segment <= kSegments; ++segment)
        {
            const float phi = 6.28318531f * static_cast<float>(segment) /
                              static_cast<float>(kSegments);
            Vertex vertex;
            vertex.normal[0] = sinf(theta) * cosf(phi);
            vertex.normal[1] = cosf(theta);
            vertex.normal[2] = sinf(theta) * sinf(phi);
            for (int c = 0; c < 3; ++c) vertex.position[c] = vertex.normal[c];
            vertices.push_back(vertex);
        }
    }
    for (unsigned ring = 0; ring < kRings; ++ring)
    {
        for (unsigned segment = 0; segment < kSegments; ++segment)
        {
            const std::uint16_t a = static_cast<std::uint16_t>(ring * (kSegments + 1) + segment);
            const std::uint16_t b = static_cast<std::uint16_t>(a + kSegments + 1);
            const std::uint16_t order[6] = { a, static_cast<std::uint16_t>(a + 1), b,
                static_cast<std::uint16_t>(a + 1), static_cast<std::uint16_t>(b + 1), b };
            for (int i = 0; i < 6; ++i) indices.push_back(order[i]);
        }
    }

    prisma::BufferDesc bufferDesc;
    bufferDesc.size = static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex));
    bufferDesc.data = vertices.data();
    bufferDesc.debugName = "sphere vertices";
    const prisma::BufferHandle vertexBuffer = driver->createBuffer(bufferDesc);
    bufferDesc.usage = prisma::BufferUsage::Index;
    bufferDesc.size = static_cast<std::uint32_t>(indices.size() * sizeof(std::uint16_t));
    bufferDesc.data = indices.data();
    bufferDesc.debugName = "sphere indices";
    const prisma::BufferHandle indexBuffer = driver->createBuffer(bufferDesc);

    const unsigned alignment = driver->caps().uniformBufferOffsetAlignment;
    const unsigned frameStride = (sizeof(FrameUniforms) + alignment - 1) / alignment * alignment;
    const unsigned objectStride = (sizeof(ObjectUniforms) + alignment - 1) / alignment * alignment;
    const unsigned objectCount = kColumns * kRows;
    ct::Vector<unsigned char> uniformBytes;
    uniformBytes.resize(static_cast<size_t>(frameStride) + objectStride * objectCount);
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = static_cast<std::uint32_t>(uniformBytes.size());
    bufferDesc.data = nullptr;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle pbrVertex = zenapp::createShader(driver, pbr_vert);
    const prisma::ShaderHandle pbrFragment = zenapp::createShader(driver, pbr_frag);
    const prisma::ShaderHandle skyVertex = zenapp::createShader(driver, sky_vert);
    const prisma::ShaderHandle skyFragment = zenapp::createShader(driver, sky_frag);

    prisma::PipelineDesc pbrDesc;
    pbrDesc.vertexShader = pbrVertex;
    pbrDesc.fragmentShader = pbrFragment;
    pbrDesc.vertexBuffers[0].stride = sizeof(Vertex);
    pbrDesc.vertexBufferCount = 1;
    pbrDesc.attributeCount = 2;
    pbrDesc.attributes[0].location = 0;
    pbrDesc.attributes[0].format = prisma::VertexFormat::Float3;
    pbrDesc.attributes[0].offset = 0;
    pbrDesc.attributes[1].location = 1;
    pbrDesc.attributes[1].format = prisma::VertexFormat::Float3;
    pbrDesc.attributes[1].offset = sizeof(float) * 3;
    pbrDesc.depthTest = true;
    pbrDesc.cullMode = prisma::CullMode::Back;
    pbrDesc.debugName = "pbr pipeline";
    const prisma::PipelineHandle pbrPipeline = driver->createPipeline(pbrDesc);

    prisma::PipelineDesc skyDesc;
    skyDesc.vertexShader = skyVertex;
    skyDesc.fragmentShader = skyFragment;
    skyDesc.depthTest = false;
    skyDesc.depthWrite = false;
    skyDesc.debugName = "sky pipeline";
    const prisma::PipelineHandle skyPipeline = driver->createPipeline(skyDesc);

    driver->destroy(pbrVertex);
    driver->destroy(pbrFragment);
    driver->destroy(skyVertex);
    driver->destroy(skyFragment);

    const bool ready = iblReady && vertexBuffer.valid() && indexBuffer.valid() &&
                       uniformBuffer.valid() && pbrPipeline.valid() && skyPipeline.valid();
    if (!ready) log_error("pbr ibl: resource creation failed");

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.0f;
    pass.clearColor[1] = 0.0f;
    pass.clearColor[2] = 0.0f;

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
        const float time = still ? 0.0f : static_cast<float>(time_seconds());

        const float halfWidth = (kColumns - 1) * kSpacing * 0.5f + 0.7f;
        const float tangent = tanf(0.45f);
        float distance = halfWidth / (tangent * (aspect < 1.4f ? aspect : 1.4f));
        distance = distance < 6.0f ? 6.0f : distance;
        const float orbit = 0.35f * sinf(time * 0.4f);
        const Math::Vec3 eye(distance * sinf(orbit), 1.2f, distance * cosf(orbit));
        const Math::Vec3 target(0.0f, 0.0f, 0.0f);
        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(0.9f, aspect, 0.1f, 100.0f);
        const Math::Mat4 view = Math::Mat4::LookAt(eye, target, Math::Vec3(0.0f, 1.0f, 0.0f));

        FrameUniforms frame;
        frame.viewProjection = projection * view;
        frame.inverseViewProjection = frame.viewProjection.Inverse();
        frame.camera[0] = eye.x;
        frame.camera[1] = eye.y;
        frame.camera[2] = eye.z;
        frame.camera[3] = 1.0f;
        frame.exposure[0] = 1.0f;
        frame.exposure[1] = 0.0f;
        frame.exposure[2] = frame.exposure[3] = 0.0f;
        memcpy(uniformBytes.data(), &frame, sizeof(frame));

        for (unsigned row = 0; row < kRows; ++row)
        {
            for (unsigned column = 0; column < kColumns; ++column)
            {
                ObjectUniforms object;
                const float x = (static_cast<float>(column) - (kColumns - 1) * 0.5f) * kSpacing;
                const float y = (1.0f - static_cast<float>(row)) * kSpacing;
                object.model = Math::Mat4::Translation(Math::Vec3(x, y, 0.0f)) *
                               Math::Mat4::Scale(Math::Vec3(0.48f, 0.48f, 0.48f));
                memcpy(object.baseColor, kRowColors[row], sizeof(object.baseColor));
                object.material[0] = kRowMetallic[row];
                object.material[1] = static_cast<float>(column) / static_cast<float>(kColumns - 1);
                object.material[2] = object.material[3] = 0.0f;
                memcpy(uniformBytes.data() + frameStride +
                                static_cast<size_t>(row * kColumns + column) * objectStride,
                        &object, sizeof(object));
            }
        }

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, uniformBytes.data(),
                static_cast<std::uint32_t>(uniformBytes.size()));
        driver->beginRenderPass(pass);

        driver->bindPipeline(skyPipeline);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(FrameUniforms));
        driver->bindTexture(0, ibl.environment, ibl.cubeSampler);
        driver->draw(3, 0);

        driver->bindPipeline(pbrPipeline);
        driver->bindVertexBuffer(0, vertexBuffer, 0);
        driver->bindIndexBuffer(indexBuffer);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(FrameUniforms));
        zenapp::bindIbl(driver, ibl);
        for (unsigned i = 0; i < objectCount; ++i)
        {
            driver->bindUniformBuffer(2, uniformBuffer, frameStride + i * objectStride,
                    sizeof(ObjectUniforms));
            driver->drawIndexed(static_cast<std::uint32_t>(indices.size()), 0);
        }
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(skyPipeline);
    driver->destroy(pbrPipeline);
    driver->destroy(uniformBuffer);
    driver->destroy(indexBuffer);
    driver->destroy(vertexBuffer);
    zenapp::destroyIbl(driver, &ibl);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
