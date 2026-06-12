#pragma once

#include <grapple/foundation/StrongId.hpp>
#include <grapple/runtime/RuntimeValue.hpp>

#include <string>
#include <vector>

namespace grapple::runtime {

struct RuntimeNamedValue {
  std::string name;
  RuntimeValue value;
};

using RuntimeValueMap = std::vector<RuntimeNamedValue>;

struct RuntimeEffectOutput {
  foundation::GraphId effectGraphId;
  foundation::NodeId targetNodeId;
  foundation::NodeId sourceNodeId;
  RuntimeValueMap values;
};

} // namespace grapple::runtime
