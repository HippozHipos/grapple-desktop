#include <grapple/project/ProjectSerializer.hpp>

#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/graph/GraphEdge.hpp>
#include <grapple/graph/GraphNode.hpp>
#include <grapple/timeline/EffectPayload.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <json/json.h>

#include <memory>
#include <optional>
#include <string>

namespace grapple::project {

namespace {

foundation::Error parseError(const std::string& path, const std::string& message) {
  return foundation::Error{"project.snapshot_json_invalid", path + ": " + message};
}

foundation::Result<Json::Value> parseJson(const std::string& json) {
  Json::CharReaderBuilder builder;
  std::string errors;
  Json::Value root;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
  if (!reader->parse(json.data(), json.data() + json.size(), &root, &errors)) {
    return parseError("$", "Invalid JSON. " + errors);
  }
  if (!root.isObject()) {
    return parseError("$", "Project snapshot must be a JSON object.");
  }
  return root;
}

foundation::Result<Json::Value> requiredMember(const Json::Value& object, const char* key, const std::string& path) {
  if (!object.isObject()) {
    return parseError(path, "Expected object.");
  }
  if (!object.isMember(key)) {
    return parseError(path + "." + key, "Missing required field.");
  }
  return object[key];
}

foundation::Result<Json::Value> requiredObjectMember(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isObject()) {
    return parseError(path + "." + key, "Expected object.");
  }
  return value.value();
}

foundation::Result<Json::Value> requiredArrayMember(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isArray()) {
    return parseError(path + "." + key, "Expected array.");
  }
  return value.value();
}

foundation::Result<std::string> requiredStringMember(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isString()) {
    return parseError(path + "." + key, "Expected string.");
  }
  return value.value().asString();
}

foundation::Result<bool> requiredBoolMember(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isBool()) {
    return parseError(path + "." + key, "Expected boolean.");
  }
  return value.value().asBool();
}

foundation::Result<double> requiredDoubleMember(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isNumeric()) {
    return parseError(path + "." + key, "Expected number.");
  }
  return value.value().asDouble();
}

foundation::Result<std::int64_t> requiredInt64Member(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isIntegral()) {
    return parseError(path + "." + key, "Expected integer.");
  }
  return value.value().asInt64();
}

foundation::Result<int> requiredIntMember(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredInt64Member(object, key, path);
  if (!value) {
    return value.error();
  }
  return static_cast<int>(value.value());
}

foundation::Result<foundation::Hash256> parseHash(const std::string& hex, const std::string& path) {
  const std::optional<foundation::Hash256> hash = foundation::hashFromHex(hex);
  if (!hash.has_value()) {
    return parseError(path, "Hash must be 64 hex characters.");
  }
  return hash.value();
}

foundation::Result<asset::AssetMediaType> parseMediaType(const std::string& value, const std::string& path) {
  if (value == "video") {
    return asset::AssetMediaType::Video;
  }
  if (value == "audio") {
    return asset::AssetMediaType::Audio;
  }
  if (value == "image") {
    return asset::AssetMediaType::Image;
  }
  return parseError(path, "Unknown media type.");
}

foundation::Result<graph::NodeKind> parseNodeKind(const std::string& value, const std::string& path) {
  if (value == "composition") {
    return graph::NodeKind::Composition;
  }
  if (value == "track") {
    return graph::NodeKind::Track;
  }
  if (value == "clip") {
    return graph::NodeKind::Clip;
  }
  if (value == "camera") {
    return graph::NodeKind::Camera;
  }
  if (value == "effect") {
    return graph::NodeKind::Effect;
  }
  if (value == "asset") {
    return graph::NodeKind::Asset;
  }
  if (value == "note") {
    return graph::NodeKind::Note;
  }
  return parseError(path, "Unknown node kind.");
}

foundation::Result<graph::EdgeKind> parseEdgeKind(const std::string& value, const std::string& path) {
  if (value == "contains") {
    return graph::EdgeKind::Contains;
  }
  if (value == "references") {
    return graph::EdgeKind::References;
  }
  if (value == "connects") {
    return graph::EdgeKind::Connects;
  }
  if (value == "targets") {
    return graph::EdgeKind::Targets;
  }
  return parseError(path, "Unknown edge kind.");
}

foundation::Result<timeline::ClipKind> parseClipKind(const std::string& value, const std::string& path) {
  if (value == "video") {
    return timeline::ClipKind::Video;
  }
  if (value == "audio") {
    return timeline::ClipKind::Audio;
  }
  if (value == "image") {
    return timeline::ClipKind::Image;
  }
  return parseError(path, "Unknown clip kind.");
}

foundation::Result<timeline::EffectImplementationKind> parseEffectImplementationKind(const std::string& value, const std::string& path) {
  if (value == "builtin") {
    return timeline::EffectImplementationKind::Builtin;
  }
  if (value == "python") {
    return timeline::EffectImplementationKind::Python;
  }
  if (value == "shader") {
    return timeline::EffectImplementationKind::Shader;
  }
  return parseError(path, "Unknown effect implementation kind.");
}

foundation::Result<timeline::EffectSourceKind> parseEffectSourceKind(const std::string& value, const std::string& path) {
  if (value == "inline_source") {
    return timeline::EffectSourceKind::InlineSource;
  }
  if (value == "asset_source") {
    return timeline::EffectSourceKind::AssetSource;
  }
  return parseError(path, "Unknown effect source kind.");
}

foundation::Result<std::optional<foundation::FilePath>> optionalFilePathMember(
  const Json::Value& object,
  const char* key,
  const std::string& path
) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (value.value().isNull()) {
    return std::optional<foundation::FilePath>{};
  }
  if (!value.value().isString()) {
    return parseError(path + "." + key, "Expected string or null.");
  }
  return std::optional<foundation::FilePath>{foundation::FilePath{value.value().asString()}};
}

foundation::Result<std::optional<foundation::AssetId>> optionalAssetIdMember(
  const Json::Value& object,
  const char* key,
  const std::string& path
) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (value.value().isNull()) {
    return std::optional<foundation::AssetId>{};
  }
  if (!value.value().isString()) {
    return parseError(path + "." + key, "Expected string or null.");
  }
  return std::optional<foundation::AssetId>{foundation::AssetId{value.value().asString()}};
}

foundation::Result<std::optional<foundation::TimeSeconds>> optionalDurationMember(
  const Json::Value& object,
  const char* key,
  const std::string& path
) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (value.value().isNull()) {
    return std::optional<foundation::TimeSeconds>{};
  }
  if (!value.value().isNumeric()) {
    return parseError(path + "." + key, "Expected number or null.");
  }
  return std::optional<foundation::TimeSeconds>{foundation::TimeSeconds{value.value().asDouble()}};
}

foundation::Result<std::optional<foundation::Resolution>> optionalResolutionMember(
  const Json::Value& object,
  const char* key,
  const std::string& path
) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (value.value().isNull()) {
    return std::optional<foundation::Resolution>{};
  }
  if (!value.value().isObject()) {
    return parseError(path + "." + key, "Expected object or null.");
  }
  auto width = requiredIntMember(value.value(), "width", path + "." + key);
  if (!width) {
    return width.error();
  }
  auto height = requiredIntMember(value.value(), "height", path + "." + key);
  if (!height) {
    return height.error();
  }
  return std::optional<foundation::Resolution>{foundation::Resolution{width.value(), height.value()}};
}

foundation::Result<std::optional<foundation::FrameRate>> optionalFrameRateMember(
  const Json::Value& object,
  const char* key,
  const std::string& path
) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (value.value().isNull()) {
    return std::optional<foundation::FrameRate>{};
  }
  if (!value.value().isObject()) {
    return parseError(path + "." + key, "Expected object or null.");
  }
  auto numerator = requiredIntMember(value.value(), "numerator", path + "." + key);
  if (!numerator) {
    return numerator.error();
  }
  auto denominator = requiredIntMember(value.value(), "denominator", path + "." + key);
  if (!denominator) {
    return denominator.error();
  }
  return std::optional<foundation::FrameRate>{foundation::FrameRate{numerator.value(), denominator.value()}};
}

foundation::Result<foundation::Vec2> parseVec2(const Json::Value& object, const std::string& path) {
  auto x = requiredDoubleMember(object, "x", path);
  if (!x) {
    return x.error();
  }
  auto y = requiredDoubleMember(object, "y", path);
  if (!y) {
    return y.error();
  }
  return foundation::Vec2{x.value(), y.value()};
}

foundation::Result<foundation::Vec3> parseVec3(const Json::Value& object, const std::string& path) {
  auto x = requiredDoubleMember(object, "x", path);
  if (!x) {
    return x.error();
  }
  auto y = requiredDoubleMember(object, "y", path);
  if (!y) {
    return y.error();
  }
  auto z = requiredDoubleMember(object, "z", path);
  if (!z) {
    return z.error();
  }
  return foundation::Vec3{x.value(), y.value(), z.value()};
}

foundation::Result<foundation::Rect> parseRect(const Json::Value& object, const std::string& path) {
  auto x = requiredDoubleMember(object, "x", path);
  if (!x) {
    return x.error();
  }
  auto y = requiredDoubleMember(object, "y", path);
  if (!y) {
    return y.error();
  }
  auto width = requiredDoubleMember(object, "width", path);
  if (!width) {
    return width.error();
  }
  auto height = requiredDoubleMember(object, "height", path);
  if (!height) {
    return height.error();
  }
  return foundation::Rect{x.value(), y.value(), width.value(), height.value()};
}

foundation::Result<foundation::TimeRange> parseTimeRange(const Json::Value& object, const std::string& path) {
  auto start = requiredDoubleMember(object, "start", path);
  if (!start) {
    return start.error();
  }
  auto end = requiredDoubleMember(object, "end", path);
  if (!end) {
    return end.error();
  }
  return foundation::TimeRange{foundation::TimeSeconds{start.value()}, foundation::TimeSeconds{end.value()}};
}

foundation::Result<timeline::Transform> parseTransform(const Json::Value& object, const std::string& path) {
  auto positionValue = requiredObjectMember(object, "position", path);
  if (!positionValue) {
    return positionValue.error();
  }
  auto position = parseVec2(positionValue.value(), path + ".position");
  if (!position) {
    return position.error();
  }
  auto scaleValue = requiredObjectMember(object, "scale", path);
  if (!scaleValue) {
    return scaleValue.error();
  }
  auto scale = parseVec2(scaleValue.value(), path + ".scale");
  if (!scale) {
    return scale.error();
  }
  auto rotation = requiredDoubleMember(object, "rotationDegrees", path);
  if (!rotation) {
    return rotation.error();
  }
  auto opacity = requiredDoubleMember(object, "opacity", path);
  if (!opacity) {
    return opacity.error();
  }
  return timeline::Transform{position.value(), scale.value(), rotation.value(), opacity.value()};
}

foundation::Result<timeline::ParamValue> parseParamValue(const Json::Value& value, const std::string& path) {
  if (value.isBool()) {
    return timeline::ParamValue{value.asBool()};
  }
  if (value.isString()) {
    return timeline::ParamValue{value.asString()};
  }
  if (value.isNumeric()) {
    return timeline::ParamValue{value.asDouble()};
  }
  if (!value.isObject()) {
    return parseError(path, "Expected parameter scalar or typed object.");
  }
  if (value.isMember("width") || value.isMember("height")) {
    auto rect = parseRect(value, path);
    if (!rect) {
      return rect.error();
    }
    return timeline::ParamValue{rect.value()};
  }
  if (value.isMember("z")) {
    auto vec3 = parseVec3(value, path);
    if (!vec3) {
      return vec3.error();
    }
    return timeline::ParamValue{vec3.value()};
  }
  auto vec2 = parseVec2(value, path);
  if (!vec2) {
    return vec2.error();
  }
  return timeline::ParamValue{vec2.value()};
}

foundation::Result<timeline::ParamSet> parseParamSet(const Json::Value& array, const std::string& path) {
  if (!array.isArray()) {
    return parseError(path, "Expected parameter array.");
  }

  timeline::ParamSet params;
  for (Json::ArrayIndex index = 0; index < array.size(); ++index) {
    const std::string itemPath = path + "[" + std::to_string(index) + "]";
    if (!array[index].isObject()) {
      return parseError(itemPath, "Expected parameter object.");
    }
    auto name = requiredStringMember(array[index], "name", itemPath);
    if (!name) {
      return name.error();
    }
    auto value = requiredMember(array[index], "value", itemPath);
    if (!value) {
      return value.error();
    }
    auto paramValue = parseParamValue(value.value(), itemPath + ".value");
    if (!paramValue) {
      return paramValue.error();
    }

    timeline::Param::Control control;
    if (array[index].isMember("label")) {
      auto label = requiredStringMember(array[index], "label", itemPath);
      if (!label) {
        return label.error();
      }
      control.label = label.value();
    }
    if (array[index].isMember("numeric")) {
      auto numericObject = requiredObjectMember(array[index], "numeric", itemPath);
      if (!numericObject) {
        return numericObject.error();
      }
      auto min = requiredDoubleMember(numericObject.value(), "min", itemPath + ".numeric");
      if (!min) {
        return min.error();
      }
      auto max = requiredDoubleMember(numericObject.value(), "max", itemPath + ".numeric");
      if (!max) {
        return max.error();
      }
      timeline::Param::NumericControl numeric{
        min.value(),
        max.value(),
        std::nullopt
      };
      if (numericObject.value().isMember("step")) {
        auto step = requiredDoubleMember(numericObject.value(), "step", itemPath + ".numeric");
        if (!step) {
          return step.error();
        }
        numeric.step = step.value();
      }
      control.numeric = numeric;
    }

    params.values.push_back(timeline::Param{name.value(), paramValue.value(), control});
  }
  return params;
}

foundation::Result<timeline::EffectPortSet> parsePortSet(const Json::Value& object, const std::string& path) {
  auto inputs = requiredArrayMember(object, "inputs", path);
  if (!inputs) {
    return inputs.error();
  }
  auto outputs = requiredArrayMember(object, "outputs", path);
  if (!outputs) {
    return outputs.error();
  }

  timeline::EffectPortSet ports;
  for (Json::ArrayIndex index = 0; index < inputs.value().size(); ++index) {
    const std::string itemPath = path + ".inputs[" + std::to_string(index) + "]";
    auto name = requiredStringMember(inputs.value()[index], "name", itemPath);
    if (!name) {
      return name.error();
    }
    ports.inputs.push_back(timeline::EffectPort{name.value()});
  }
  for (Json::ArrayIndex index = 0; index < outputs.value().size(); ++index) {
    const std::string itemPath = path + ".outputs[" + std::to_string(index) + "]";
    auto name = requiredStringMember(outputs.value()[index], "name", itemPath);
    if (!name) {
      return name.error();
    }
    ports.outputs.push_back(timeline::EffectPort{name.value()});
  }
  return ports;
}

foundation::Result<std::vector<timeline::EffectModelDependency>> parseModelDependencies(
  const Json::Value& array,
  const std::string& path
) {
  if (!array.isArray()) {
    return parseError(path, "Expected model dependency array.");
  }

  std::vector<timeline::EffectModelDependency> modelDependencies;
  for (Json::ArrayIndex index = 0; index < array.size(); ++index) {
    const std::string itemPath = path + "[" + std::to_string(index) + "]";
    auto modelId = requiredStringMember(array[index], "modelId", itemPath);
    if (!modelId) {
      return modelId.error();
    }
    auto versionHashHex = requiredStringMember(array[index], "versionHash", itemPath);
    if (!versionHashHex) {
      return versionHashHex.error();
    }
    auto versionHash = parseHash(versionHashHex.value(), itemPath + ".versionHash");
    if (!versionHash) {
      return versionHash.error();
    }
    modelDependencies.push_back(timeline::EffectModelDependency{
      foundation::ModelId{modelId.value()},
      versionHash.value()
    });
  }
  return modelDependencies;
}

foundation::Result<asset::Asset> parseAsset(const Json::Value& object, const std::string& path) {
  auto id = requiredStringMember(object, "id", path);
  if (!id) {
    return id.error();
  }
  auto name = requiredStringMember(object, "name", path);
  if (!name) {
    return name.error();
  }
  auto metadata = requiredObjectMember(object, "metadata", path);
  if (!metadata) {
    return metadata.error();
  }
  auto mediaTypeName = requiredStringMember(metadata.value(), "mediaType", path + ".metadata");
  if (!mediaTypeName) {
    return mediaTypeName.error();
  }
  auto mediaType = parseMediaType(mediaTypeName.value(), path + ".metadata.mediaType");
  if (!mediaType) {
    return mediaType.error();
  }
  auto sourcePath = requiredStringMember(metadata.value(), "sourcePath", path + ".metadata");
  if (!sourcePath) {
    return sourcePath.error();
  }
  auto thumbnailPath = optionalFilePathMember(metadata.value(), "thumbnailPath", path + ".metadata");
  if (!thumbnailPath) {
    return thumbnailPath.error();
  }
  auto duration = optionalDurationMember(metadata.value(), "duration", path + ".metadata");
  if (!duration) {
    return duration.error();
  }
  auto dimensions = optionalResolutionMember(metadata.value(), "dimensions", path + ".metadata");
  if (!dimensions) {
    return dimensions.error();
  }
  auto frameRate = optionalFrameRateMember(metadata.value(), "frameRate", path + ".metadata");
  if (!frameRate) {
    return frameRate.error();
  }

  return asset::Asset{
    foundation::AssetId{id.value()},
    name.value(),
    asset::AssetMetadata{
      mediaType.value(),
      foundation::FilePath{sourcePath.value()},
      thumbnailPath.value(),
      duration.value(),
      dimensions.value(),
      frameRate.value()
    }
  };
}

foundation::Result<timeline::ClipPayload> parseClipPayload(const Json::Value& object, const std::string& path) {
  auto kindName = requiredStringMember(object, "kind", path);
  if (!kindName) {
    return kindName.error();
  }
  auto kind = parseClipKind(kindName.value(), path + ".kind");
  if (!kind) {
    return kind.error();
  }
  auto timelineRangeObject = requiredObjectMember(object, "timelineRange", path);
  if (!timelineRangeObject) {
    return timelineRangeObject.error();
  }
  auto timelineRange = parseTimeRange(timelineRangeObject.value(), path + ".timelineRange");
  if (!timelineRange) {
    return timelineRange.error();
  }
  auto sourceRangeObject = requiredObjectMember(object, "sourceRange", path);
  if (!sourceRangeObject) {
    return sourceRangeObject.error();
  }
  auto sourceRange = parseTimeRange(sourceRangeObject.value(), path + ".sourceRange");
  if (!sourceRange) {
    return sourceRange.error();
  }
  auto playbackRate = requiredDoubleMember(object, "playbackRate", path);
  if (!playbackRate) {
    return playbackRate.error();
  }
  auto assetId = requiredStringMember(object, "assetId", path);
  if (!assetId) {
    return assetId.error();
  }
  auto transformObject = requiredObjectMember(object, "transform", path);
  if (!transformObject) {
    return transformObject.error();
  }
  auto transform = parseTransform(transformObject.value(), path + ".transform");
  if (!transform) {
    return transform.error();
  }
  return timeline::ClipPayload{
    kind.value(),
    timelineRange.value(),
    sourceRange.value(),
    playbackRate.value(),
    foundation::AssetId{assetId.value()},
    transform.value()
  };
}

foundation::Result<timeline::CameraPayload> parseCameraPayload(const Json::Value& object, const std::string& path) {
  auto name = requiredStringMember(object, "name", path);
  if (!name) {
    return name.error();
  }
  auto transformObject = requiredObjectMember(object, "transform", path);
  if (!transformObject) {
    return transformObject.error();
  }
  auto transform = parseTransform(transformObject.value(), path + ".transform");
  if (!transform) {
    return transform.error();
  }
  auto lensObject = requiredObjectMember(object, "lens", path);
  if (!lensObject) {
    return lensObject.error();
  }
  auto focalLength = requiredDoubleMember(lensObject.value(), "focalLength", path + ".lens");
  if (!focalLength) {
    return focalLength.error();
  }
  return timeline::CameraPayload{name.value(), transform.value(), timeline::CameraLens{focalLength.value()}};
}

foundation::Result<timeline::EffectPayload> parseEffectPayload(const Json::Value& object, const std::string& path) {
  auto displayName = requiredStringMember(object, "displayName", path);
  if (!displayName) {
    return displayName.error();
  }
  auto implementationObject = requiredObjectMember(object, "implementation", path);
  if (!implementationObject) {
    return implementationObject.error();
  }
  auto implementationKindName = requiredStringMember(implementationObject.value(), "kind", path + ".implementation");
  if (!implementationKindName) {
    return implementationKindName.error();
  }
  auto implementationKind = parseEffectImplementationKind(implementationKindName.value(), path + ".implementation.kind");
  if (!implementationKind) {
    return implementationKind.error();
  }
  auto entrypoint = requiredStringMember(implementationObject.value(), "entrypoint", path + ".implementation");
  if (!entrypoint) {
    return entrypoint.error();
  }
  auto sourceObject = requiredObjectMember(implementationObject.value(), "source", path + ".implementation");
  if (!sourceObject) {
    return sourceObject.error();
  }
  auto sourceKindName = requiredStringMember(sourceObject.value(), "kind", path + ".implementation.source");
  if (!sourceKindName) {
    return sourceKindName.error();
  }
  auto sourceKind = parseEffectSourceKind(sourceKindName.value(), path + ".implementation.source.kind");
  if (!sourceKind) {
    return sourceKind.error();
  }
  auto language = requiredStringMember(sourceObject.value(), "language", path + ".implementation.source");
  if (!language) {
    return language.error();
  }
  auto inlineSource = requiredStringMember(sourceObject.value(), "inlineSource", path + ".implementation.source");
  if (!inlineSource) {
    return inlineSource.error();
  }
  auto sourceAssetId = optionalAssetIdMember(sourceObject.value(), "sourceAssetId", path + ".implementation.source");
  if (!sourceAssetId) {
    return sourceAssetId.error();
  }
  auto sourceHashHex = requiredStringMember(sourceObject.value(), "sourceHash", path + ".implementation.source");
  if (!sourceHashHex) {
    return sourceHashHex.error();
  }
  auto sourceHash = parseHash(sourceHashHex.value(), path + ".implementation.source.sourceHash");
  if (!sourceHash) {
    return sourceHash.error();
  }
  auto portsObject = requiredObjectMember(object, "ports", path);
  if (!portsObject) {
    return portsObject.error();
  }
  auto ports = parsePortSet(portsObject.value(), path + ".ports");
  if (!ports) {
    return ports.error();
  }
  auto paramsArray = requiredArrayMember(object, "params", path);
  if (!paramsArray) {
    return paramsArray.error();
  }
  auto params = parseParamSet(paramsArray.value(), path + ".params");
  if (!params) {
    return params.error();
  }
  auto activeRangeObject = requiredObjectMember(object, "activeRange", path);
  if (!activeRangeObject) {
    return activeRangeObject.error();
  }
  auto activeRange = parseTimeRange(activeRangeObject.value(), path + ".activeRange");
  if (!activeRange) {
    return activeRange.error();
  }
  auto modelDependenciesArray = requiredArrayMember(object, "modelDependencies", path);
  if (!modelDependenciesArray) {
    return modelDependenciesArray.error();
  }
  auto modelDependencies = parseModelDependencies(modelDependenciesArray.value(), path + ".modelDependencies");
  if (!modelDependencies) {
    return modelDependencies.error();
  }
  return timeline::EffectPayload{
    displayName.value(),
    timeline::EffectImplementation{
      implementationKind.value(),
      entrypoint.value(),
      timeline::EffectSource{
        sourceKind.value(),
        language.value(),
        inlineSource.value(),
        sourceAssetId.value(),
        sourceHash.value()
      }
    },
    ports.value(),
    params.value(),
    activeRange.value(),
    modelDependencies.value()
  };
}

foundation::Result<graph::NodePayload> parseNodePayload(
  const Json::Value& object,
  graph::NodeKind nodeKind,
  const std::string& path
) {
  auto type = requiredStringMember(object, "type", path);
  if (!type) {
    return type.error();
  }

  if (type.value() == "composition") {
    auto name = requiredStringMember(object, "name", path);
    if (!name) {
      return name.error();
    }
    return graph::NodePayload{timeline::CompositionPayload{name.value()}};
  }
  if (type.value() == "track") {
    auto name = requiredStringMember(object, "name", path);
    if (!name) {
      return name.error();
    }
    return graph::NodePayload{timeline::TrackPayload{name.value()}};
  }
  if (type.value() == "clip") {
    auto payloadObject = requiredObjectMember(object, "payload", path);
    if (!payloadObject) {
      return payloadObject.error();
    }
    auto payload = parseClipPayload(payloadObject.value(), path + ".payload");
    if (!payload) {
      return payload.error();
    }
    return graph::NodePayload{payload.value()};
  }
  if (type.value() == "camera") {
    auto payloadObject = requiredObjectMember(object, "payload", path);
    if (!payloadObject) {
      return payloadObject.error();
    }
    auto payload = parseCameraPayload(payloadObject.value(), path + ".payload");
    if (!payload) {
      return payload.error();
    }
    return graph::NodePayload{payload.value()};
  }
  if (type.value() == "effect") {
    auto payloadObject = requiredObjectMember(object, "payload", path);
    if (!payloadObject) {
      return payloadObject.error();
    }
    auto payload = parseEffectPayload(payloadObject.value(), path + ".payload");
    if (!payload) {
      return payload.error();
    }
    return graph::NodePayload{payload.value()};
  }
  if (type.value() == "asset") {
    auto assetId = requiredStringMember(object, "assetId", path);
    if (!assetId) {
      return assetId.error();
    }
    return graph::NodePayload{timeline::AssetPayload{foundation::AssetId{assetId.value()}}};
  }
  if (type.value() == "note") {
    auto title = requiredStringMember(object, "title", path);
    if (!title) {
      return title.error();
    }
    auto markdown = requiredStringMember(object, "markdown", path);
    if (!markdown) {
      return markdown.error();
    }
    return graph::NodePayload{timeline::NotePayload{title.value(), markdown.value()}};
  }

  (void)nodeKind;
  return parseError(path + ".type", "Unknown node payload type.");
}

foundation::Result<graph::GraphNode> parseNode(const Json::Value& object, const std::string& path) {
  auto id = requiredStringMember(object, "id", path);
  if (!id) {
    return id.error();
  }
  auto kindName = requiredStringMember(object, "kind", path);
  if (!kindName) {
    return kindName.error();
  }
  auto kind = parseNodeKind(kindName.value(), path + ".kind");
  if (!kind) {
    return kind.error();
  }
  auto enabled = requiredBoolMember(object, "enabled", path);
  if (!enabled) {
    return enabled.error();
  }
  auto payloadObject = requiredObjectMember(object, "payload", path);
  if (!payloadObject) {
    return payloadObject.error();
  }
  auto payload = parseNodePayload(payloadObject.value(), kind.value(), path + ".payload");
  if (!payload) {
    return payload.error();
  }
  return graph::GraphNode{foundation::NodeId{id.value()}, kind.value(), payload.value(), enabled.value()};
}

foundation::Result<graph::GraphEdge> parseEdge(const Json::Value& object, const std::string& path) {
  auto id = requiredStringMember(object, "id", path);
  if (!id) {
    return id.error();
  }
  auto kindName = requiredStringMember(object, "kind", path);
  if (!kindName) {
    return kindName.error();
  }
  auto kind = parseEdgeKind(kindName.value(), path + ".kind");
  if (!kind) {
    return kind.error();
  }
  auto sourceNodeId = requiredStringMember(object, "sourceNodeId", path);
  if (!sourceNodeId) {
    return sourceNodeId.error();
  }
  auto sourcePort = requiredStringMember(object, "sourcePort", path);
  if (!sourcePort) {
    return sourcePort.error();
  }
  auto targetNodeId = requiredStringMember(object, "targetNodeId", path);
  if (!targetNodeId) {
    return targetNodeId.error();
  }
  auto targetPort = requiredStringMember(object, "targetPort", path);
  if (!targetPort) {
    return targetPort.error();
  }
  auto order = requiredInt64Member(object, "order", path);
  if (!order) {
    return order.error();
  }
  auto enabled = requiredBoolMember(object, "enabled", path);
  if (!enabled) {
    return enabled.error();
  }
  return graph::GraphEdge{
    foundation::EdgeId{id.value()},
    kind.value(),
    foundation::NodeId{sourceNodeId.value()},
    graph::PortName{sourcePort.value()},
    foundation::NodeId{targetNodeId.value()},
    graph::PortName{targetPort.value()},
    order.value(),
    enabled.value()
  };
}

foundation::Result<graph::GraphDocument> parseGraph(const Json::Value& object, const std::string& path) {
  auto nodes = requiredArrayMember(object, "nodes", path);
  if (!nodes) {
    return nodes.error();
  }
  auto edges = requiredArrayMember(object, "edges", path);
  if (!edges) {
    return edges.error();
  }

  graph::GraphDocument graph;
  for (Json::ArrayIndex index = 0; index < nodes.value().size(); ++index) {
    const std::string itemPath = path + ".nodes[" + std::to_string(index) + "]";
    auto node = parseNode(nodes.value()[index], itemPath);
    if (!node) {
      return node.error();
    }
    auto added = graph.addNode(node.value());
    if (!added) {
      return parseError(itemPath, added.error().message);
    }
  }
  for (Json::ArrayIndex index = 0; index < edges.value().size(); ++index) {
    const std::string itemPath = path + ".edges[" + std::to_string(index) + "]";
    auto edge = parseEdge(edges.value()[index], itemPath);
    if (!edge) {
      return edge.error();
    }
    auto added = graph.addEdge(edge.value());
    if (!added) {
      return parseError(itemPath, added.error().message);
    }
  }
  return graph;
}

foundation::Result<ProjectSettings> parseSettings(const Json::Value& object, const std::string& path) {
  auto defaultDuration = requiredMember(object, "defaultDuration", path);
  if (!defaultDuration) {
    return defaultDuration.error();
  }
  ProjectSettings settings;
  if (defaultDuration.value().isNull()) {
    return settings;
  }
  if (!defaultDuration.value().isNumeric()) {
    return parseError(path + ".defaultDuration", "Expected number or null.");
  }
  settings.defaultDuration = foundation::TimeSeconds{defaultDuration.value().asDouble()};
  return settings;
}

} // namespace

foundation::Result<ProjectSnapshot> deserializeCanonicalProjectSnapshot(const std::string& json) {
  auto root = parseJson(json);
  if (!root) {
    return root.error();
  }

  auto projectId = requiredStringMember(root.value(), "projectId", "$");
  if (!projectId) {
    return projectId.error();
  }
  auto name = requiredStringMember(root.value(), "name", "$");
  if (!name) {
    return name.error();
  }
  auto revision = requiredStringMember(root.value(), "revision", "$");
  if (!revision) {
    return revision.error();
  }
  auto revisionNumber = requiredInt64Member(root.value(), "revisionNumber", "$");
  if (!revisionNumber) {
    return revisionNumber.error();
  }
  auto settingsObject = requiredObjectMember(root.value(), "settings", "$");
  if (!settingsObject) {
    return settingsObject.error();
  }
  auto settings = parseSettings(settingsObject.value(), "$.settings");
  if (!settings) {
    return settings.error();
  }
  auto assetsArray = requiredArrayMember(root.value(), "assets", "$");
  if (!assetsArray) {
    return assetsArray.error();
  }
  auto graphObject = requiredObjectMember(root.value(), "graph", "$");
  if (!graphObject) {
    return graphObject.error();
  }

  asset::AssetCatalog assets;
  for (Json::ArrayIndex index = 0; index < assetsArray.value().size(); ++index) {
    const std::string itemPath = "$.assets[" + std::to_string(index) + "]";
    auto parsedAsset = parseAsset(assetsArray.value()[index], itemPath);
    if (!parsedAsset) {
      return parsedAsset.error();
    }
    auto registered = assets.registerAsset(parsedAsset.value());
    if (!registered) {
      return parseError(itemPath, registered.error().message);
    }
  }

  auto graph = parseGraph(graphObject.value(), "$.graph");
  if (!graph) {
    return graph.error();
  }

  ProjectSnapshot snapshot{
    ProjectInfo{foundation::ProjectId{projectId.value()}, name.value()},
    foundation::RevisionId{revision.value()},
    revisionNumber.value(),
    settings.value(),
    assets,
    graph.value(),
    foundation::Hash256{}
  };
  snapshot.canonicalHash = hashProjectSnapshot(snapshot);
  return snapshot;
}

} // namespace grapple::project
