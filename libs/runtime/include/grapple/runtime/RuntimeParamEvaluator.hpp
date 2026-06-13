#pragma once

#include <grapple/foundation/Time.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/runtime/RuntimeOutput.hpp>

#include <string>
#include <vector>

namespace grapple::runtime {

struct RuntimeParamKeyframe {
  foundation::KeyframeId id;
  foundation::TimeSeconds time;
  RuntimeValue value;
};

struct RuntimeParam {
  std::string name;
  RuntimeValue value;
  std::vector<RuntimeParamKeyframe> keyframes;
};

using RuntimeParamSet = std::vector<RuntimeParam>;

RuntimeParamSet runtimeParamsFromEffectNode(const projection::RenderEffectNode& node);

RuntimeValueMap evaluateRuntimeParams(
  const RuntimeParamSet& params,
  foundation::TimeSeconds time
);

} // namespace grapple::runtime
