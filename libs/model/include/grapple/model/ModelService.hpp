#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/model/ModelCapability.hpp>

#include <string>

namespace grapple::model {

struct ModelRequest {
  foundation::ModelId modelId;
  std::string input;
};

struct ModelResponse {
  foundation::ModelId modelId;
  std::string output;
};

struct VisionRequest {
  foundation::ModelId modelId;
  std::string imageRef;
  std::string prompt;
};

struct VisionResponse {
  foundation::ModelId modelId;
  std::string output;
};

struct SegmentationRequest {
  foundation::ModelId modelId;
  std::string imageRef;
  std::string subject;
};

struct SegmentationResponse {
  foundation::ModelId modelId;
  std::string maskRef;
};

class IModelService {
public:
  virtual ~IModelService() = default;

  virtual foundation::Result<ModelResponse> complete(const ModelRequest& request) = 0;
  virtual foundation::Result<VisionResponse> analyzeImage(const VisionRequest& request) = 0;
  virtual foundation::Result<SegmentationResponse> segment(const SegmentationRequest& request) = 0;
};

} // namespace grapple::model
