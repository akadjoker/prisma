#include "prisma/rhi/Driver.h"
#include "prisma/rhi/HandleCast.h"

namespace prisma
{

namespace
{

class NullDriver final : public Driver
{
public:
    DriverType type() const override { return DriverType::Null; }
    const Caps& caps() const override { return caps_; }

    BufferHandle createBuffer(const BufferDesc& desc) override
    {
        if (desc.size == 0) return BufferHandle();
        return handleCast<BufferHandle>(buffers_.insert(desc.size));
    }

    ShaderHandle createShader(const ShaderDesc& desc) override
    {
        if (!desc.source) return ShaderHandle();
        return handleCast<ShaderHandle>(shaders_.insert(0));
    }

    PipelineHandle createPipeline(const PipelineDesc& desc) override
    {
        if (!shaders_.contains(handleCast<Slot>(desc.vertexShader)) ||
                !shaders_.contains(handleCast<Slot>(desc.fragmentShader)))
            return PipelineHandle();
        return handleCast<PipelineHandle>(pipelines_.insert(0));
    }

    void destroy(BufferHandle handle) override { buffers_.erase(handleCast<Slot>(handle)); }
    void destroy(ShaderHandle handle) override { shaders_.erase(handleCast<Slot>(handle)); }
    void destroy(PipelineHandle handle) override { pipelines_.erase(handleCast<Slot>(handle)); }

    void beginFrame() override {}
    void beginRenderPass(const RenderPassDesc&) override {}
    void bindPipeline(PipelineHandle) override {}
    void bindVertexBuffer(BufferHandle, std::uint32_t) override {}
    void draw(std::uint32_t, std::uint32_t) override {}
    void endRenderPass() override {}
    void endFrame() override {}
    void present() override {}

private:
    using Slot = ct::Handle32<std::uint32_t>;

    Caps caps_;
    ct::SlotMap32<std::uint32_t> buffers_;
    ct::SlotMap32<std::uint32_t> shaders_;
    ct::SlotMap32<std::uint32_t> pipelines_;
};

} // namespace

Driver* createNullDriver(const DriverDesc&) { return new NullDriver(); }

} // namespace prisma
