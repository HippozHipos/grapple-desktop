#include <grapple/model/ModelRegistry.hpp>

#include <algorithm>

namespace grapple::model {

foundation::Result<void> ModelRegistry::registerModel(ModelRegistryEntry entry) {
  if (!entry.id) {
    return foundation::Error{"model.id_empty", "Model id must not be empty."};
  }

  if (entry.uiName.empty()) {
    return foundation::Error{"model.ui_name_empty", "Model UI name must not be empty."};
  }

  if (entry.providerName.empty()) {
    return foundation::Error{"model.provider_name_empty", "Provider name must not be empty."};
  }

  if (entry.providerModelId.empty()) {
    return foundation::Error{"model.provider_model_id_empty", "Provider model id must not be empty."};
  }

  const auto duplicateId = std::any_of(entries_.begin(), entries_.end(), [&](const ModelRegistryEntry& existing) {
    return existing.id == entry.id;
  });
  if (duplicateId) {
    return foundation::Error{"model.id_duplicate", "Model id already exists."};
  }

  entries_.push_back(std::move(entry));
  return {};
}

const ModelRegistryEntry* ModelRegistry::find(foundation::ModelId id) const noexcept {
  const auto iterator = std::find_if(entries_.begin(), entries_.end(), [&](const ModelRegistryEntry& entry) {
    return entry.id == id;
  });

  if (iterator == entries_.end()) {
    return nullptr;
  }

  return &*iterator;
}

const std::vector<ModelRegistryEntry>& ModelRegistry::entries() const noexcept {
  return entries_;
}

} // namespace grapple::model

