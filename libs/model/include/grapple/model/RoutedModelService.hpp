#pragma once

#include <grapple/model/ModelRegistry.hpp>
#include <grapple/model/ModelService.hpp>

#include <span>
#include <string>

namespace grapple::model {

struct ProviderTextRequest {
  std::string providerModelId;
  std::string input;
};

struct ProviderTextResponse {
  std::string output;
};

struct ProviderVisionRequest {
  std::string providerModelId;
  std::string imageRef;
  std::string prompt;
};

struct ProviderVisionResponse {
  std::string output;
};

struct ProviderSegmentationRequest {
  std::string providerModelId;
  std::string imageRef;
  std::string subject;
};

struct ProviderSegmentationResponse {
  std::string maskRef;
};

class IModelProvider {
public:
  virtual ~IModelProvider() = default;

  [[nodiscard]] virtual std::string providerName() const = 0;
  virtual foundation::Result<ProviderTextResponse> complete(const ProviderTextRequest& request) = 0;
  virtual foundation::Result<ProviderVisionResponse> analyzeImage(const ProviderVisionRequest& request) = 0;
  virtual foundation::Result<ProviderSegmentationResponse> segment(const ProviderSegmentationRequest& request) = 0;
};

class RoutedModelService final : public IModelService {
public:
  RoutedModelService(const ModelRegistry& registry, std::span<IModelProvider* const> providers);

  foundation::Result<ModelResponse> complete(const ModelRequest& request) override;
  foundation::Result<VisionResponse> analyzeImage(const VisionRequest& request) override;
  foundation::Result<SegmentationResponse> segment(const SegmentationRequest& request) override;

private:
  foundation::Result<const ModelRegistryEntry*> requireModel(
    foundation::ModelId modelId,
    ModelCapability capability
  ) const;
  foundation::Result<IModelProvider*> requireProvider(const ModelRegistryEntry& model) const;

  const ModelRegistry& registry_;
  std::span<IModelProvider* const> providers_;
};

} // namespace grapple::model
