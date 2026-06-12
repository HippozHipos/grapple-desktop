#pragma once

#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/runtime/RuntimeDependencyGraph.hpp>
#include <grapple/runtime/RuntimeValue.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace grapple::runtime {

struct RuntimeCacheKey {
  foundation::Hash256 planHash;
  foundation::NodeId nodeId;
  foundation::Hash256 implementationHash;
  foundation::Hash256 paramsHash;
  foundation::Hash256 inputsHash;
  std::vector<RuntimeAssetDependency> assetDependencies;
  foundation::TimeRange range;
  std::string runtimeVersion;
};

bool operator==(const RuntimeCacheKey& left, const RuntimeCacheKey& right);
foundation::Hash256 hashRuntimeCacheInputs(
  const RuntimeDependencyGraph& graph,
  const RuntimeDependencyNode& node
);
RuntimeCacheKey runtimeCacheKeyForDependency(
  const RuntimeDependencyGraph& graph,
  const RuntimeDependencyNode& node,
  std::string_view runtimeVersion
);

class IRuntimeCache {
public:
  virtual ~IRuntimeCache() = default;

  virtual std::optional<RuntimeValue> get(const RuntimeCacheKey& key) = 0;
  virtual foundation::Result<void> put(const RuntimeCacheKey& key, RuntimeValue value) = 0;
  virtual foundation::Result<void> invalidate(const std::vector<RuntimeCacheKey>& keys) = 0;
};

} // namespace grapple::runtime
