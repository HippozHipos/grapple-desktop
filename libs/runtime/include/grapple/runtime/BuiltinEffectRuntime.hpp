#pragma once

#include <grapple/runtime/EffectRuntime.hpp>

namespace grapple::runtime {

class BuiltinEffectRuntime final : public IEffectRuntime {
public:
  [[nodiscard]] bool supports(const projection::RenderEffectNode& node) const override;
  foundation::Result<EffectPrepareResult> prepare(const EffectPrepareRequest& request) override;
  foundation::Result<EffectProcessResult> process(const EffectProcessRequest& request) override;
};

} // namespace grapple::runtime
