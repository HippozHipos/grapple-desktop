#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/model/ModelCapability.hpp>

#include <optional>
#include <string>
#include <vector>

namespace grapple::model {

struct ModelRegistryEntry {
  foundation::ModelId id;
  std::string uiName;
  std::string providerName;
  std::string providerModelId;
  ModelCapabilitySet capabilities;
};

class ModelRegistry {
public:
  foundation::Result<void> registerModel(ModelRegistryEntry entry);

  [[nodiscard]] const ModelRegistryEntry* find(foundation::ModelId id) const noexcept;
  [[nodiscard]] const std::vector<ModelRegistryEntry>& entries() const noexcept;

private:
  std::vector<ModelRegistryEntry> entries_;
};

} // namespace grapple::model

