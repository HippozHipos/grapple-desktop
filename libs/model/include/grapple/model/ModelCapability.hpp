#pragma once

namespace grapple::model {

enum class ModelCapability {
  TextCompletion,
  ImageAnalysis,
  Segmentation
};

struct ModelCapabilitySet {
  bool textCompletion = false;
  bool imageAnalysis = false;
  bool segmentation = false;

  [[nodiscard]] bool supports(ModelCapability capability) const noexcept {
    switch (capability) {
      case ModelCapability::TextCompletion:
        return textCompletion;
      case ModelCapability::ImageAnalysis:
        return imageAnalysis;
      case ModelCapability::Segmentation:
        return segmentation;
    }

    return false;
  }
};

} // namespace grapple::model

