#pragma once

#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/model/ModelCapability.hpp>
#include <grapple/runtime/RuntimeValue.hpp>

#include <optional>

namespace grapple::runtime {

struct RuntimeCacheKey {
  foundation::Hash256 planHash;
  foundation::NodeId nodeId;
  foundation::Hash256 implementationHash;
  foundation::Hash256 paramsHash;
  foundation::Hash256 inputsHash;
  std::optional<foundation::AssetId> assetId;
  foundation::Hash256 assetVersionHash;
  std::optional<foundation::ModelId> modelId;
  std::string modelVersion;
  foundation::TimeRange range;
  std::string runtimeVersion;
};

class IRuntimeCache {
public:
  virtual ~IRuntimeCache() = default;

  virtual std::optional<RuntimeValue> get(const RuntimeCacheKey& key) = 0;
  virtual foundation::Result<void> put(const RuntimeCacheKey& key, RuntimeValue value) = 0;
};

} // namespace grapple::runtime

