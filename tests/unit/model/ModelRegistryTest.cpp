#include <grapple/model/ModelRegistry.hpp>

#include <TestAssert.hpp>

int main() {
  using namespace grapple;

  model::ModelRegistry registry;

  const auto registered = registry.registerModel(model::ModelRegistryEntry{
    foundation::ModelId{"model_gemini_3_1_pro"},
    "Gemini 3.1 Pro",
    "google",
    "gemini-3.1-pro",
    model::ModelCapabilitySet{
      true,
      true,
      false
    }
  });
  GRAPPLE_REQUIRE(registered);
  GRAPPLE_REQUIRE(registry.entries().size() == 1);

  const model::ModelRegistryEntry* entry = registry.find(foundation::ModelId{"model_gemini_3_1_pro"});
  GRAPPLE_REQUIRE(entry != nullptr);
  GRAPPLE_REQUIRE(entry->uiName == "Gemini 3.1 Pro");
  GRAPPLE_REQUIRE(entry->providerName == "google");
  GRAPPLE_REQUIRE(entry->providerModelId == "gemini-3.1-pro");
  GRAPPLE_REQUIRE(entry->capabilities.supports(model::ModelCapability::TextCompletion));
  GRAPPLE_REQUIRE(entry->capabilities.supports(model::ModelCapability::ImageAnalysis));
  GRAPPLE_REQUIRE(!entry->capabilities.supports(model::ModelCapability::Segmentation));

  GRAPPLE_REQUIRE(registry.find(foundation::ModelId{"Gemini 3.1 Pro"}) == nullptr);
  GRAPPLE_REQUIRE(registry.find(foundation::ModelId{"gemini-3.1-pro"}) == nullptr);

  const auto duplicate = registry.registerModel(model::ModelRegistryEntry{
    foundation::ModelId{"model_gemini_3_1_pro"},
    "Gemini 3.1 Pro Duplicate",
    "google",
    "gemini-3.1-pro",
    model::ModelCapabilitySet{true, false, false}
  });
  GRAPPLE_REQUIRE(!duplicate);
  GRAPPLE_REQUIRE(duplicate.error().code == "model.id_duplicate");

  const auto missingProviderModel = registry.registerModel(model::ModelRegistryEntry{
    foundation::ModelId{"model_bad"},
    "Bad Model",
    "google",
    "",
    model::ModelCapabilitySet{true, false, false}
  });
  GRAPPLE_REQUIRE(!missingProviderModel);
  GRAPPLE_REQUIRE(missingProviderModel.error().code == "model.provider_model_id_empty");

  return 0;
}

