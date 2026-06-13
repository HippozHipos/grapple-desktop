#include <grapple/model/ModelRegistry.hpp>
#include <grapple/model/RoutedModelService.hpp>

#include <TestAssert.hpp>

#include <string>
#include <utility>

namespace {

class TestProvider final : public grapple::model::IModelProvider {
public:
  explicit TestProvider(std::string name, bool failText = false)
    : name_{std::move(name)},
      failText_{failText} {}

  std::string providerName() const override {
    return name_;
  }

  grapple::foundation::Result<grapple::model::ProviderTextResponse> complete(
    const grapple::model::ProviderTextRequest& request
  ) override {
    ++textCalls;
    lastProviderModelId = request.providerModelId;
    lastInput = request.input;
    if (failText_) {
      return grapple::foundation::Error{"provider.text_failed", "Provider text call failed."};
    }
    return grapple::model::ProviderTextResponse{"text:" + request.providerModelId + ":" + request.input};
  }

  grapple::foundation::Result<grapple::model::ProviderVisionResponse> analyzeImage(
    const grapple::model::ProviderVisionRequest& request
  ) override {
    ++visionCalls;
    lastProviderModelId = request.providerModelId;
    lastInput = request.imageRef + ":" + request.prompt;
    return grapple::model::ProviderVisionResponse{"vision:" + request.providerModelId + ":" + request.imageRef + ":" + request.prompt};
  }

  grapple::foundation::Result<grapple::model::ProviderSegmentationResponse> segment(
    const grapple::model::ProviderSegmentationRequest& request
  ) override {
    ++segmentationCalls;
    lastProviderModelId = request.providerModelId;
    lastInput = request.imageRef + ":" + request.subject;
    return grapple::model::ProviderSegmentationResponse{"mask:" + request.providerModelId + ":" + request.imageRef + ":" + request.subject};
  }

  int textCalls = 0;
  int visionCalls = 0;
  int segmentationCalls = 0;
  std::string lastProviderModelId;
  std::string lastInput;

private:
  std::string name_;
  bool failText_ = false;
};

} // namespace

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

  const auto duplicateUiName = registry.registerModel(model::ModelRegistryEntry{
    foundation::ModelId{"model_duplicate_ui_name"},
    "Gemini 3.1 Pro",
    "google",
    "gemini-3.1-flash",
    model::ModelCapabilitySet{true, false, false}
  });
  GRAPPLE_REQUIRE(!duplicateUiName);
  GRAPPLE_REQUIRE(duplicateUiName.error().code == "model.ui_name_duplicate");

  const auto duplicateProviderModel = registry.registerModel(model::ModelRegistryEntry{
    foundation::ModelId{"model_duplicate_provider"},
    "Gemini 3.1 Pro Duplicate Provider",
    "google",
    "gemini-3.1-pro",
    model::ModelCapabilitySet{true, false, false}
  });
  GRAPPLE_REQUIRE(!duplicateProviderModel);
  GRAPPLE_REQUIRE(duplicateProviderModel.error().code == "model.provider_model_duplicate");

  const auto missingProviderModel = registry.registerModel(model::ModelRegistryEntry{
    foundation::ModelId{"model_bad"},
    "Bad Model",
    "google",
    "",
    model::ModelCapabilitySet{true, false, false}
  });
  GRAPPLE_REQUIRE(!missingProviderModel);
  GRAPPLE_REQUIRE(missingProviderModel.error().code == "model.provider_model_id_empty");

  const auto registeredSegmenter = registry.registerModel(model::ModelRegistryEntry{
    foundation::ModelId{"model_sam_2"},
    "SAM 2",
    "local",
    "sam2-large",
    model::ModelCapabilitySet{false, false, true}
  });
  GRAPPLE_REQUIRE(registeredSegmenter);

  TestProvider googleProvider{"google"};
  TestProvider localProvider{"local"};
  model::IModelProvider* providers[] = {&googleProvider, &localProvider};
  model::RoutedModelService service{registry, providers};

  const auto text = service.complete(model::ModelRequest{
    foundation::ModelId{"model_gemini_3_1_pro"},
    "center subject"
  });
  GRAPPLE_REQUIRE(text);
  GRAPPLE_REQUIRE(text.value().modelId == foundation::ModelId{"model_gemini_3_1_pro"});
  GRAPPLE_REQUIRE(text.value().output == "text:gemini-3.1-pro:center subject");
  GRAPPLE_REQUIRE(googleProvider.textCalls == 1);
  GRAPPLE_REQUIRE(googleProvider.lastProviderModelId == "gemini-3.1-pro");
  GRAPPLE_REQUIRE(localProvider.textCalls == 0);

  const auto vision = service.analyzeImage(model::VisionRequest{
    foundation::ModelId{"model_gemini_3_1_pro"},
    "frame_0001.png",
    "find subject"
  });
  GRAPPLE_REQUIRE(vision);
  GRAPPLE_REQUIRE(vision.value().modelId == foundation::ModelId{"model_gemini_3_1_pro"});
  GRAPPLE_REQUIRE(vision.value().output == "vision:gemini-3.1-pro:frame_0001.png:find subject");
  GRAPPLE_REQUIRE(googleProvider.visionCalls == 1);
  GRAPPLE_REQUIRE(localProvider.visionCalls == 0);

  const auto segmentation = service.segment(model::SegmentationRequest{
    foundation::ModelId{"model_sam_2"},
    "frame_0001.png",
    "woman"
  });
  GRAPPLE_REQUIRE(segmentation);
  GRAPPLE_REQUIRE(segmentation.value().modelId == foundation::ModelId{"model_sam_2"});
  GRAPPLE_REQUIRE(segmentation.value().maskRef == "mask:sam2-large:frame_0001.png:woman");
  GRAPPLE_REQUIRE(localProvider.segmentationCalls == 1);
  GRAPPLE_REQUIRE(googleProvider.segmentationCalls == 0);

  const auto unsupportedCapability = service.complete(model::ModelRequest{
    foundation::ModelId{"model_sam_2"},
    "describe"
  });
  GRAPPLE_REQUIRE(!unsupportedCapability);
  GRAPPLE_REQUIRE(unsupportedCapability.error().code == "model.capability_missing");
  GRAPPLE_REQUIRE(localProvider.textCalls == 0);

  const auto unknownModel = service.complete(model::ModelRequest{
    foundation::ModelId{"model_unknown"},
    "describe"
  });
  GRAPPLE_REQUIRE(!unknownModel);
  GRAPPLE_REQUIRE(unknownModel.error().code == "model.not_registered");

  model::IModelProvider* onlyLocalProvider[] = {&localProvider};
  model::RoutedModelService missingProviderService{registry, onlyLocalProvider};
  const auto missingProvider = missingProviderService.complete(model::ModelRequest{
    foundation::ModelId{"model_gemini_3_1_pro"},
    "describe"
  });
  GRAPPLE_REQUIRE(!missingProvider);
  GRAPPLE_REQUIRE(missingProvider.error().code == "model.provider_missing");
  GRAPPLE_REQUIRE(localProvider.textCalls == 0);

  TestProvider failingGoogleProvider{"google", true};
  TestProvider unusedLocalProvider{"local"};
  model::IModelProvider* failingProviders[] = {&failingGoogleProvider, &unusedLocalProvider};
  model::RoutedModelService failingService{registry, failingProviders};
  const auto providerFailure = failingService.complete(model::ModelRequest{
    foundation::ModelId{"model_gemini_3_1_pro"},
    "describe"
  });
  GRAPPLE_REQUIRE(!providerFailure);
  GRAPPLE_REQUIRE(providerFailure.error().code == "provider.text_failed");
  GRAPPLE_REQUIRE(failingGoogleProvider.textCalls == 1);
  GRAPPLE_REQUIRE(unusedLocalProvider.textCalls == 0);

  return 0;
}
