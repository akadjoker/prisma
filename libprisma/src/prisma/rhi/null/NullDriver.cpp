#include "prisma/rhi/Driver.h"

namespace prisma
{

namespace
{

class NullDriver final : public Driver
{
public:
    DriverType type() const override { return DriverType::Null; }
    const Caps& caps() const override { return caps_; }

    void beginFrame() override {}
    void beginRenderPass(const RenderPassDesc&) override {}
    void endRenderPass() override {}
    void endFrame() override {}
    void present() override {}

private:
    Caps caps_;
};

} // namespace

Driver* createNullDriver(const DriverDesc&) { return new NullDriver(); }

} // namespace prisma
