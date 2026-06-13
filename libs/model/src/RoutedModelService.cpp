#include <grapple/model/RoutedModelService.hpp>

#include <utility>

namespace grapple::model {

RoutedModelService::RoutedModelService(
  const ModelRegistry& registry,
  std::span<IModelProvider* const> providers
) : registry_{registry},
    providers_{providers} {}

foundation::Result<const ModelRegistryEntry*> RoutedModelService::requireModel(
  foundation::ModelId modelId,
  ModelCapability capability
) const {
  const ModelRegistryEntry* model = registry_.find(std::move(modelId));
  if (model == nullptr) {
    return foundation::Error{"model.not_registered", "Requested model is not registered."};
  }

  if (!model->capabilities.supports(capability)) {
    return foundation::Error{"model.capability_missing", "Requested model does not support the requested capability."};
  }

  return model;
}

foundation::Result<IModelProvider*> RoutedModelService::requireProvider(
  const ModelRegistryEntry& model
) const {
  for (IModelProvider* provider : providers_) {
    if (provider != nullptr && provider->providerName() == model.providerName) {
      return provider;
    }
  }

  return foundation::Error{"model.provider_missing", "Registered model provider is not available."};
}

foundation::Result<ModelResponse> RoutedModelService::complete(const ModelRequest& request) {
  auto model = requireModel(request.modelId, ModelCapability::TextCompletion);
  if (!model) {
    return model.error();
  }
  auto provider = requireProvider(*model.value());
  if (!provider) {
    return provider.error();
  }

  auto response = provider.value()->complete(ProviderTextRequest{
    model.value()->providerModelId,
    request.input
  });
  if (!response) {
    return response.error();
  }

  return ModelResponse{
    request.modelId,
    response.value().output
  };
}

foundation::Result<VisionResponse> RoutedModelService::analyzeImage(const VisionRequest& request) {
  auto model = requireModel(request.modelId, ModelCapability::ImageAnalysis);
  if (!model) {
    return model.error();
  }
  auto provider = requireProvider(*model.value());
  if (!provider) {
    return provider.error();
  }

  auto response = provider.value()->analyzeImage(ProviderVisionRequest{
    model.value()->providerModelId,
    request.imageRef,
    request.prompt
  });
  if (!response) {
    return response.error();
  }

  return VisionResponse{
    request.modelId,
    response.value().output
  };
}

foundation::Result<SegmentationResponse> RoutedModelService::segment(const SegmentationRequest& request) {
  auto model = requireModel(request.modelId, ModelCapability::Segmentation);
  if (!model) {
    return model.error();
  }
  auto provider = requireProvider(*model.value());
  if (!provider) {
    return provider.error();
  }

  auto response = provider.value()->segment(ProviderSegmentationRequest{
    model.value()->providerModelId,
    request.imageRef,
    request.subject
  });
  if (!response) {
    return response.error();
  }

  return SegmentationResponse{
    request.modelId,
    response.value().maskRef
  };
}

} // namespace grapple::model
