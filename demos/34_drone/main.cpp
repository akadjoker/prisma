#include "common/Equirect.h"
#include "common/GltfGpu.h"
#include "common/Ibl.h"
#include "common/Projection.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "bloom_down13.frag.h"
#include "bloom_down2x.frag.h"
#include "bloom_down9.frag.h"
#include "bloom_up.frag.h"
#include "drone.frag.h"
#include "drone.vert.h"
#include "final.frag.h"
#include "fullscreen.vert.h"
#include "sky.frag.h"
#include "sky.vert.h"

namespace
{

struct FrameUniforms
{
    Math::Mat4 viewProjection;
    Math::Mat4 inverseViewProjection;
    float camera[4];
    float exposure[4];
    float sunDirection[4];
    float sunColorIntensity[4];
};

struct ObjectUniforms
{
    Math::Mat4 model;
    Math::Mat4 normalMatrix;
};

const float kSunLux = 100000.0f;
const float kIblLuminance = 30000.0f;
const unsigned kBloomLevels = 6;
const unsigned kBloomHeight = 384;
const std::uint32_t kRenderWidth = 1280;
const std::uint32_t kRenderHeight = 720;
const float kAperture = 16.0f;
const float kShutter = 1.0f / 125.0f;
const float kSensitivity = 100.0f;

float exposureFactor()
{
    const float ev100 = log2f(kAperture * kAperture / kShutter * 100.0f / kSensitivity);
    return 1.0f / (1.2f * powf(2.0f, ev100));
}

void setTargetFormats(prisma::PipelineDesc* desc, bool depth)
{
    desc->targets.window = false;
    desc->targets.colorCount = 1;
    desc->targets.colors[0] = prisma::TextureFormat::RGBA16F;
    desc->targets.depth = depth ? prisma::TextureFormat::Depth32F : prisma::TextureFormat::None;
}

float numberArgument(int argc, char** argv, const char* name, float fallback)
{
    const char* value = zenapp::argumentValue(argc, argv, name);
    return value ? static_cast<float>(atof(value)) : fallback;
}

Math::Mat4 nodeMatrix(const zenapp::GltfNode& node)
{
    Math::Mat4 m;
    memcpy(m.Data(), node.world, 16 * sizeof(float));
    return m;
}

void modelBounds(const zenapp::GltfModel& model, Math::Vec3* center, float* radius)
{
    Math::Vec3 low(1e30f, 1e30f, 1e30f);
    Math::Vec3 high(-1e30f, -1e30f, -1e30f);
    for (size_t n = 0; n < model.nodes.size(); ++n)
    {
        if (model.nodes[n].mesh < 0) continue;
        const Math::Mat4 matrix = nodeMatrix(model.nodes[n]);
        const zenapp::GltfMesh& mesh = model.meshes[static_cast<size_t>(model.nodes[n].mesh)];
        for (unsigned p = 0; p < mesh.primitiveCount; ++p)
        {
            const zenapp::GltfPrimitive& primitive = model.primitives[mesh.firstPrimitive + p];
            for (int corner = 0; corner < 8; ++corner)
            {
                const Math::Vec4 local((corner & 1) ? primitive.boundsMax[0] : primitive.boundsMin[0],
                        (corner & 2) ? primitive.boundsMax[1] : primitive.boundsMin[1],
                        (corner & 4) ? primitive.boundsMax[2] : primitive.boundsMin[2], 1.0f);
                const Math::Vec4 world = matrix * local;
                low = Math::Vec3(fminf(low.x, world.x), fminf(low.y, world.y), fminf(low.z, world.z));
                high = Math::Vec3(fmaxf(high.x, world.x), fmaxf(high.y, world.y),
                        fmaxf(high.z, world.z));
            }
        }
    }
    *center = (low + high) * 0.5f;
    *radius = (high - low).Length() * 0.5f;
}

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = zenapp::frameLimit(argc, argv);
    const prisma::DriverType driverType = zenapp::driverType(argc, argv);
    const bool still = zenapp::hasArgument(argc, argv, "still");
    const float blur = numberArgument(argc, argv, "blur", 2.5f);
    const float yaw = numberArgument(argc, argv, "yaw", 0.0f);
    const float turn = numberArgument(argc, argv, "turn", 0.0f);
    const float ev = numberArgument(argc, argv, "ev", 0.0f);
    const float bloomStrength = numberArgument(argc, argv, "bloom", 0.10f);

    char modelPath[1024];
    const char* modelArgument = zenapp::argumentValue(argc, argv, "model");
    if (modelArgument)
        snprintf(modelPath, sizeof(modelPath), "%s", modelArgument);
    else
        snprintf(modelPath, sizeof(modelPath), "models/BusterDrone/scene.gltf");
    char environmentPath[1024];
    const char* environmentArgument = zenapp::argumentValue(argc, argv, "env");
    if (environmentArgument)
        snprintf(environmentPath, sizeof(environmentPath), "%s", environmentArgument);
    else
        snprintf(environmentPath, sizeof(environmentPath), "%s/../environments/venetian_crossroads_2k.hdr",
                PRISMA_MODELS_DIR);

    zenapp::GltfModel model;
    if (!zenapp::loadGltf(modelPath, &model))
    {
        log_error("drone: cannot read %s: %s", modelPath, model.error.c_str());
        return 1;
    }
    zenapp::EnvironmentFaces faces;
    if (!zenapp::loadEquirectFaces(environmentPath, 0, &faces))
    {
        log_error("drone: cannot read %s", environmentPath);
        return 1;
    }
    Math::Vec3 center;
    float radius;
    modelBounds(model, &center, &radius);

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }
    PlatformWindow* window = zenapp::openWindow("prisma 34 drone", driverType);
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
        log_error("drone: this GPU cannot render to float textures");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    const float exposure = exposureFactor() * powf(2.0f, ev);
    zenapp::Ibl ibl;
    const bool iblReady = zenapp::createIbl(driver, faces, &ibl, kIblLuminance * exposure);
    zenapp::GltfGpuOptions gpuOptions;
    zenapp::GltfGpu gpu;
    const bool gpuReady = zenapp::createGltfGpu(driver, model, gpuOptions, &gpu);
    log_info("drone: %u vertices, %u triangles, %u textures loaded, %u failed",
            static_cast<unsigned>(model.vertices.size()),
            static_cast<unsigned>(model.indices.size() / 3), gpu.texturesLoaded, gpu.texturesFailed);

    const unsigned alignment = driver->caps().uniformBufferOffsetAlignment;
    const unsigned objectStride = (sizeof(ObjectUniforms) + alignment - 1) / alignment * alignment;
    ct::Vector<unsigned char> objectBytes;
    objectBytes.resize(static_cast<size_t>(objectStride) * (model.nodes.size() + 1));
    memset(objectBytes.data(), 0, objectBytes.size());
    for (size_t n = 0; n < model.nodes.size(); ++n)
    {
        ObjectUniforms object;
        object.model = Math::Mat4::Translation(center) * Math::Mat4::RotationY(turn) *
                       Math::Mat4::Translation(-center) * nodeMatrix(model.nodes[n]);
        object.normalMatrix = object.model.Inverse().Transposed();
        memcpy(objectBytes.data() + n * objectStride, &object, sizeof(object));
    }

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(FrameUniforms);
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "frame uniforms";
    const prisma::BufferHandle frameBuffer = driver->createBuffer(bufferDesc);
    prisma::BufferDesc objectDesc;
    objectDesc.usage = prisma::BufferUsage::Uniform;
    objectDesc.size = static_cast<std::uint32_t>(objectBytes.size());
    objectDesc.data = objectBytes.data();
    objectDesc.debugName = "object uniforms";
    const prisma::BufferHandle objectBuffer = driver->createBuffer(objectDesc);

    const bool linear = driver->caps().floatLinearFiltering;
    prisma::SamplerDesc samplerDesc;
    samplerDesc.minFilter = linear ? prisma::Filter::Linear : prisma::Filter::Nearest;
    samplerDesc.magFilter = samplerDesc.minFilter;
    samplerDesc.mipFilter = prisma::MipFilter::None;
    samplerDesc.addressU = prisma::AddressMode::ClampToEdge;
    samplerDesc.addressV = prisma::AddressMode::ClampToEdge;
    samplerDesc.debugName = "scene sampler";
    const prisma::SamplerHandle sceneSampler = driver->createSampler(samplerDesc);

    prisma::TextureDesc colorDesc;
    colorDesc.format = prisma::TextureFormat::RGBA16F;
    colorDesc.width = kRenderWidth;
    colorDesc.height = kRenderHeight;
    colorDesc.usage = prisma::kTextureSampled | prisma::kTextureRenderTarget;
    colorDesc.debugName = "scene color";
    const prisma::TextureHandle sceneColor = driver->createTexture(colorDesc);
    prisma::TextureDesc depthDesc;
    depthDesc.format = prisma::TextureFormat::Depth32F;
    depthDesc.width = kRenderWidth;
    depthDesc.height = kRenderHeight;
    depthDesc.usage = prisma::kTextureRenderTarget;
    depthDesc.debugName = "scene depth";
    const prisma::TextureHandle sceneDepth = driver->createTexture(depthDesc);

    unsigned bloomWidth[kBloomLevels];
    unsigned bloomHeightAt[kBloomLevels];
    prisma::TextureHandle bloomTexture[kBloomLevels];
    bool bloomReady = true;
    const unsigned baseBloomWidth = static_cast<unsigned>(
            floorf(static_cast<float>(kBloomHeight) * static_cast<float>(kRenderWidth) /
                   static_cast<float>(kRenderHeight)));
    for (unsigned level = 0; level < kBloomLevels; ++level)
    {
        bloomWidth[level] = baseBloomWidth >> level > 1 ? baseBloomWidth >> level : 1;
        bloomHeightAt[level] = kBloomHeight >> level > 1 ? kBloomHeight >> level : 1;
        prisma::TextureDesc bloomDesc;
        bloomDesc.format = prisma::TextureFormat::RGBA16F;
        bloomDesc.width = bloomWidth[level];
        bloomDesc.height = bloomHeightAt[level];
        bloomDesc.usage = prisma::kTextureSampled | prisma::kTextureRenderTarget;
        bloomDesc.debugName = "bloom";
        bloomTexture[level] = driver->createTexture(bloomDesc);
        bloomReady = bloomReady && bloomTexture[level].valid();
    }

    const unsigned postStride = (sizeof(float) * 4 + alignment - 1) / alignment * alignment;
    const unsigned postRanges = 2 + kBloomLevels;
    ct::Vector<unsigned char> postBytes;
    postBytes.resize(static_cast<size_t>(postStride) * postRanges);
    memset(postBytes.data(), 0, postBytes.size());
    const float downParams[4] = { 1.0f, 1.0f, 1.0f / 1000.0f, 0.0f };
    memcpy(postBytes.data(), downParams, sizeof(downParams));
    for (unsigned level = 0; level + 1 < kBloomLevels; ++level)
    {
        const float w = static_cast<float>(bloomWidth[level]);
        const float h = static_cast<float>(bloomHeightAt[level]);
        const float resolution[4] = { w, h, 1.0f / w, 1.0f / h };
        memcpy(postBytes.data() + static_cast<size_t>(1 + level) * postStride, resolution,
                sizeof(resolution));
    }
    const float finalParams[4] = { bloomStrength, 0.0f, 0.0f, 0.0f };
    memcpy(postBytes.data() + static_cast<size_t>(1 + kBloomLevels) * postStride, finalParams,
            sizeof(finalParams));
    prisma::BufferDesc postDesc;
    postDesc.usage = prisma::BufferUsage::Uniform;
    postDesc.size = static_cast<std::uint32_t>(postBytes.size());
    postDesc.data = postBytes.data();
    postDesc.debugName = "post uniforms";
    const prisma::BufferHandle postBuffer = driver->createBuffer(postDesc);

    const prisma::ShaderHandle down2xFragment = zenapp::createShader(driver, bloom_down2x_frag);
    const prisma::ShaderHandle down13Fragment = zenapp::createShader(driver, bloom_down13_frag);
    const prisma::ShaderHandle down9Fragment = zenapp::createShader(driver, bloom_down9_frag);
    const prisma::ShaderHandle upFragment = zenapp::createShader(driver, bloom_up_frag);
    const prisma::ShaderHandle fullscreenVertex = zenapp::createShader(driver, fullscreen_vert);
    const prisma::ShaderHandle finalFragment = zenapp::createShader(driver, final_frag);
    const prisma::ShaderHandle droneVertex = zenapp::createShader(driver, drone_vert);
    const prisma::ShaderHandle droneFragment = zenapp::createShader(driver, drone_frag);
    const prisma::ShaderHandle skyVertex = zenapp::createShader(driver, sky_vert);
    const prisma::ShaderHandle skyFragment = zenapp::createShader(driver, sky_frag);

    prisma::PipelineDesc opaqueDesc;
    opaqueDesc.vertexShader = droneVertex;
    opaqueDesc.fragmentShader = droneFragment;
    opaqueDesc.vertexBuffers[0].stride = sizeof(zenapp::GltfVertex);
    opaqueDesc.vertexBufferCount = 1;
    opaqueDesc.attributeCount = 4;
    opaqueDesc.attributes[0].location = 0;
    opaqueDesc.attributes[0].format = prisma::VertexFormat::Float3;
    opaqueDesc.attributes[0].offset = offsetof(zenapp::GltfVertex, position);
    opaqueDesc.attributes[1].location = 1;
    opaqueDesc.attributes[1].format = prisma::VertexFormat::Float3;
    opaqueDesc.attributes[1].offset = offsetof(zenapp::GltfVertex, normal);
    opaqueDesc.attributes[2].location = 2;
    opaqueDesc.attributes[2].format = prisma::VertexFormat::Float4;
    opaqueDesc.attributes[2].offset = offsetof(zenapp::GltfVertex, tangent);
    opaqueDesc.attributes[3].location = 3;
    opaqueDesc.attributes[3].format = prisma::VertexFormat::Float2;
    opaqueDesc.attributes[3].offset = offsetof(zenapp::GltfVertex, uv);
    opaqueDesc.depthTest = true;
    opaqueDesc.cullMode = prisma::CullMode::Back;
    setTargetFormats(&opaqueDesc, true);
    opaqueDesc.debugName = "drone opaque";
    const prisma::PipelineHandle opaquePipeline = driver->createPipeline(opaqueDesc);

    prisma::PipelineDesc doubleDesc = opaqueDesc;
    doubleDesc.cullMode = prisma::CullMode::None;
    doubleDesc.debugName = "drone double sided";
    const prisma::PipelineHandle doublePipeline = driver->createPipeline(doubleDesc);

    prisma::PipelineDesc blendDesc = doubleDesc;
    blendDesc.depthWrite = false;
    blendDesc.blend = true;
    blendDesc.srcColor = prisma::BlendFactor::SrcAlpha;
    blendDesc.dstColor = prisma::BlendFactor::OneMinusSrcAlpha;
    blendDesc.srcAlpha = prisma::BlendFactor::One;
    blendDesc.dstAlpha = prisma::BlendFactor::OneMinusSrcAlpha;
    blendDesc.debugName = "drone blend";
    const prisma::PipelineHandle blendPipeline = driver->createPipeline(blendDesc);

    prisma::PipelineDesc skyDesc;
    skyDesc.vertexShader = skyVertex;
    skyDesc.fragmentShader = skyFragment;
    skyDesc.depthTest = false;
    skyDesc.depthWrite = false;
    skyDesc.debugName = "sky pipeline";
    setTargetFormats(&skyDesc, true);
    const prisma::PipelineHandle skyPipeline = driver->createPipeline(skyDesc);

    prisma::PipelineDesc finalDesc;
    finalDesc.vertexShader = fullscreenVertex;
    finalDesc.fragmentShader = finalFragment;
    finalDesc.depthWrite = false;
    finalDesc.debugName = "final pipeline";
    const prisma::PipelineHandle finalPipeline = driver->createPipeline(finalDesc);

    prisma::PipelineDesc down2xDesc;
    down2xDesc.vertexShader = fullscreenVertex;
    down2xDesc.fragmentShader = down2xFragment;
    down2xDesc.depthWrite = false;
    setTargetFormats(&down2xDesc, false);
    down2xDesc.debugName = "bloom down 2x";
    const prisma::PipelineHandle down2xPipeline = driver->createPipeline(down2xDesc);
    prisma::PipelineDesc down13Desc = down2xDesc;
    down13Desc.fragmentShader = down13Fragment;
    down13Desc.debugName = "bloom down 13";
    const prisma::PipelineHandle down13Pipeline = driver->createPipeline(down13Desc);
    prisma::PipelineDesc down9Desc = down2xDesc;
    down9Desc.fragmentShader = down9Fragment;
    down9Desc.debugName = "bloom down 9";
    const prisma::PipelineHandle down9Pipeline = driver->createPipeline(down9Desc);
    prisma::PipelineDesc upDesc = down2xDesc;
    upDesc.fragmentShader = upFragment;
    upDesc.blend = true;
    upDesc.srcColor = prisma::BlendFactor::One;
    upDesc.dstColor = prisma::BlendFactor::One;
    upDesc.srcAlpha = prisma::BlendFactor::One;
    upDesc.dstAlpha = prisma::BlendFactor::One;
    upDesc.debugName = "bloom up";
    const prisma::PipelineHandle upPipeline = driver->createPipeline(upDesc);

    driver->destroy(droneVertex);
    driver->destroy(droneFragment);
    driver->destroy(skyVertex);
    driver->destroy(skyFragment);
    driver->destroy(fullscreenVertex);
    driver->destroy(finalFragment);
    driver->destroy(down2xFragment);
    driver->destroy(down13Fragment);
    driver->destroy(down9Fragment);
    driver->destroy(upFragment);

    const bool ready = iblReady && gpuReady && frameBuffer.valid() && objectBuffer.valid() &&
                       opaquePipeline.valid() && doublePipeline.valid() && blendPipeline.valid() &&
                       skyPipeline.valid() && finalPipeline.valid() && sceneSampler.valid() &&
                       sceneColor.valid() && sceneDepth.valid() && bloomReady &&
                       postBuffer.valid() && down2xPipeline.valid() && down13Pipeline.valid() &&
                       down9Pipeline.valid() && upPipeline.valid();
    if (!ready) log_error("drone: resource creation failed");

    prisma::RenderPassDesc scenePass;
    scenePass.colors[0].texture = sceneColor;
    scenePass.colorCount = 1;
    scenePass.depth.texture = sceneDepth;
    scenePass.depthStore = prisma::StoreOp::Discard;
    scenePass.clearColor[0] = scenePass.clearColor[1] = scenePass.clearColor[2] = 0.0f;
    prisma::RenderPassDesc windowPass;
    windowPass.depthLoad = prisma::LoadOp::DontCare;

    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        if (width < 2 || height < 2) continue;
        const float aspect = static_cast<float>(kRenderWidth) / static_cast<float>(kRenderHeight);
        const float angle = yaw + (still ? 0.0f : static_cast<float>(time_seconds()) * 0.3f);

        const float distance = radius * 2.2f;
        const Math::Vec3 eye(center.x + distance * sinf(angle), center.y + radius * 0.08f,
                center.z - distance * cosf(angle));
        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(0.6f, aspect, radius * 0.1f,
                radius * 20.0f);
        const Math::Mat4 view = Math::Mat4::LookAt(eye, center, Math::Vec3(0.0f, 1.0f, 0.0f));

        FrameUniforms frame;
        memset(&frame, 0, sizeof(frame));
        frame.viewProjection = projection * view;
        frame.inverseViewProjection = frame.viewProjection.Inverse();
        frame.camera[0] = eye.x;
        frame.camera[1] = eye.y;
        frame.camera[2] = eye.z;
        frame.camera[3] = 1.0f;
        frame.exposure[0] = kIblLuminance * exposure;
        frame.exposure[1] = blur;
        frame.sunDirection[1] = 1.0f;
        frame.sunColorIntensity[0] = frame.sunColorIntensity[1] = frame.sunColorIntensity[2] = 1.0f;
        frame.sunColorIntensity[3] = kSunLux * exposure;

        driver->beginFrame();
        driver->updateBuffer(frameBuffer, 0, &frame, sizeof(frame));
        driver->beginRenderPass(scenePass);

        driver->bindPipeline(skyPipeline);
        driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
        driver->bindTexture(0, ibl.environment, ibl.cubeSampler);
        driver->draw(3, 0);

        zenapp::bindGltfGeometry(driver, gpu);
        for (int blended = 0; blended < 2; ++blended)
        {
            for (size_t n = 0; n < model.nodes.size(); ++n)
            {
                if (model.nodes[n].mesh < 0) continue;
                const zenapp::GltfMesh& mesh = model.meshes[static_cast<size_t>(model.nodes[n].mesh)];
                for (unsigned p = 0; p < mesh.primitiveCount; ++p)
                {
                    const zenapp::GltfPrimitive& primitive = model.primitives[mesh.firstPrimitive + p];
                    if (primitive.material < 0) continue;
                    const zenapp::GltfMaterial& material =
                            model.materials[static_cast<size_t>(primitive.material)];
                    const bool isBlend = material.alpha == zenapp::GltfMaterial::Alpha::Blend;
                    if (isBlend != (blended == 1)) continue;
                    driver->bindPipeline(isBlend ? blendPipeline
                                                 : (material.doubleSided ? doublePipeline
                                                                         : opaquePipeline));
                    driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
                    zenapp::bindIbl(driver, ibl);
                    driver->bindUniformBuffer(2, objectBuffer, static_cast<std::uint32_t>(n * objectStride),
                            sizeof(ObjectUniforms));
                    zenapp::bindGltfMaterial(driver, gpu, model, primitive.material);
                    zenapp::drawGltfPrimitive(driver, gpu, primitive);
                }
            }
        }
        driver->endRenderPass();

        if (bloomStrength > 0.0f)
        {
            prisma::RenderPassDesc bloomPass;
            bloomPass.colorCount = 1;
            bloomPass.depthLoad = prisma::LoadOp::DontCare;
            bloomPass.stencilLoad = prisma::LoadOp::DontCare;

            bloomPass.colors[0].texture = bloomTexture[0];
            driver->beginRenderPass(bloomPass);
            driver->bindPipeline(down2xPipeline);
            driver->bindUniformBuffer(0, postBuffer, 0, sizeof(float) * 4);
            driver->bindTexture(0, sceneColor, sceneSampler);
            driver->draw(3, 0);
            driver->endRenderPass();

            for (unsigned level = 1; level < kBloomLevels; ++level)
            {
                const bool odd = (bloomWidth[level - 1] & 1) || (bloomHeightAt[level - 1] & 1);
                bloomPass.colors[0].texture = bloomTexture[level];
                driver->beginRenderPass(bloomPass);
                driver->bindPipeline(odd ? down13Pipeline : down9Pipeline);
                driver->bindTexture(0, bloomTexture[level - 1], sceneSampler);
                driver->draw(3, 0);
                driver->endRenderPass();
            }

            bloomPass.colorLoad = prisma::LoadOp::Load;
            for (unsigned level = kBloomLevels - 1; level >= 1; --level)
            {
                bloomPass.colors[0].texture = bloomTexture[level - 1];
                driver->beginRenderPass(bloomPass);
                driver->bindPipeline(upPipeline);
                driver->bindUniformBuffer(0, postBuffer, level * postStride, sizeof(float) * 4);
                driver->bindTexture(0, bloomTexture[level], sceneSampler);
                driver->draw(3, 0);
                driver->endRenderPass();
            }
        }

        driver->beginRenderPass(windowPass);
        driver->bindPipeline(finalPipeline);
        driver->bindUniformBuffer(0, postBuffer, (1 + kBloomLevels) * postStride, sizeof(float) * 4);
        driver->bindTexture(0, sceneColor, sceneSampler);
        driver->bindTexture(1, bloomTexture[0], sceneSampler);
        driver->draw(3, 0);
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(upPipeline);
    driver->destroy(down9Pipeline);
    driver->destroy(down13Pipeline);
    driver->destroy(down2xPipeline);
    driver->destroy(finalPipeline);
    driver->destroy(skyPipeline);
    driver->destroy(blendPipeline);
    driver->destroy(doublePipeline);
    driver->destroy(opaquePipeline);
    for (unsigned level = 0; level < kBloomLevels; ++level) driver->destroy(bloomTexture[level]);
    driver->destroy(postBuffer);
    driver->destroy(sceneDepth);
    driver->destroy(sceneColor);
    driver->destroy(sceneSampler);
    driver->destroy(objectBuffer);
    driver->destroy(frameBuffer);
    zenapp::destroyGltfGpu(driver, &gpu);
    zenapp::destroyIbl(driver, &ibl);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
