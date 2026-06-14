#include <grapple/agent/ProjectTools.hpp>

#include <grapple/agent/AgentToolRegistry.hpp>
#include <grapple/foundation/Geometry.hpp>
#include <grapple/foundation/Json.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/project/ProjectCommand.hpp>
#include <grapple/project/ProjectQuery.hpp>

#include <json/json.h>

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>

namespace grapple::agent {

namespace {

constexpr const char ProjectInspectSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "properties": {}
})json";

constexpr const char AssetListSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "properties": {}
})json";

constexpr const char AssetImportSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["assetId", "name", "mediaType", "sourcePath"],
  "properties": {
    "assetId": {"type": "string", "minLength": 1},
    "name": {"type": "string", "minLength": 1},
    "mediaType": {"enum": ["video", "audio", "image"]},
    "sourcePath": {"type": "string", "minLength": 1},
    "thumbnailPath": {"type": "string"},
    "duration": {"type": "number"},
    "dimensions": {
      "type": "object",
      "additionalProperties": false,
      "required": ["width", "height"],
      "properties": {
        "width": {"type": "integer"},
        "height": {"type": "integer"}
      }
    },
    "frameRate": {
      "type": "object",
      "additionalProperties": false,
      "required": ["numerator", "denominator"],
      "properties": {
        "numerator": {"type": "integer"},
        "denominator": {"type": "integer"}
      }
    }
  }
})json";

constexpr const char CompositionInspectSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "properties": {}
})json";

constexpr const char CameraCreateSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["compositionNodeId", "name", "focalLength"],
  "properties": {
    "compositionNodeId": {"type": "string", "minLength": 1},
    "name": {"type": "string", "minLength": 1},
    "focalLength": {"type": "number"}
  }
})json";

constexpr const char CameraUpdateSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["cameraNodeId", "name", "focalLength"],
  "properties": {
    "cameraNodeId": {"type": "string", "minLength": 1},
    "name": {"type": "string", "minLength": 1},
    "focalLength": {"type": "number"}
  }
})json";

constexpr const char TimelineCreateTrackSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["compositionNodeId", "name", "kind"],
  "properties": {
    "compositionNodeId": {"type": "string", "minLength": 1},
    "name": {"type": "string", "minLength": 1},
    "kind": {"type": "string", "enum": ["visual", "audio"]}
  }
})json";

constexpr const char TimelineCreateClipSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["trackNodeId", "assetId", "kind", "timelineRange", "sourceRange", "playbackRate"],
  "properties": {
    "trackNodeId": {"type": "string", "minLength": 1},
    "assetId": {"type": "string", "minLength": 1},
    "kind": {"enum": ["video", "audio", "image"]},
    "timelineRange": {
      "type": "object",
      "additionalProperties": false,
      "required": ["start", "end"],
      "properties": {
        "start": {"type": "number"},
        "end": {"type": "number"}
      }
    },
    "sourceRange": {
      "type": "object",
      "additionalProperties": false,
      "required": ["start", "end"],
      "properties": {
        "start": {"type": "number"},
        "end": {"type": "number"}
      }
    },
    "playbackRate": {"type": "number"}
  }
})json";

constexpr const char TimelineMoveClipSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["clipNodeId", "newStart"],
  "properties": {
    "clipNodeId": {"type": "string", "minLength": 1},
    "newStart": {"type": "number"}
  }
})json";

constexpr const char TimelineTrimClipSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["clipNodeId", "timelineRange", "sourceRange"],
  "properties": {
    "clipNodeId": {"type": "string", "minLength": 1},
    "timelineRange": {
      "type": "object",
      "additionalProperties": false,
      "required": ["start", "end"],
      "properties": {
        "start": {"type": "number"},
        "end": {"type": "number"}
      }
    },
    "sourceRange": {
      "type": "object",
      "additionalProperties": false,
      "required": ["start", "end"],
      "properties": {
        "start": {"type": "number"},
        "end": {"type": "number"}
      }
    }
  }
})json";

constexpr const char TimelineUpdateClipTransformSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["clipNodeId", "position", "scale", "rotationDegrees", "opacity"],
  "properties": {
    "clipNodeId": {"type": "string", "minLength": 1},
    "position": {
      "type": "object",
      "additionalProperties": false,
      "required": ["x", "y"],
      "properties": {
        "x": {"type": "number"},
        "y": {"type": "number"}
      }
    },
    "scale": {
      "type": "object",
      "additionalProperties": false,
      "required": ["x", "y"],
      "properties": {
        "x": {"type": "number"},
        "y": {"type": "number"}
      }
    },
    "rotationDegrees": {"type": "number"},
    "opacity": {"type": "number"}
  }
})json";

constexpr const char EffectCreateNodeSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": [
    "targetNodeId",
    "displayName",
    "implementationKind",
    "language",
    "entrypoint",
    "source",
    "sourcePort",
    "targetPort",
    "inputPorts",
    "outputPorts",
    "activeRange",
    "params"
  ],
  "properties": {
    "targetNodeId": {"type": "string", "minLength": 1},
    "displayName": {"type": "string", "minLength": 1},
    "implementationKind": {"enum": ["builtin", "python", "shader"]},
    "language": {"type": "string", "minLength": 1},
    "entrypoint": {"type": "string", "minLength": 1},
    "source": {"type": "string", "minLength": 1},
    "sourcePort": {"type": "string", "minLength": 1},
    "targetPort": {"type": "string", "minLength": 1},
    "inputPorts": {"type": "array", "items": {"type": "string", "minLength": 1}},
    "outputPorts": {"type": "array", "items": {"type": "string", "minLength": 1}},
    "activeRange": {
      "type": "object",
      "additionalProperties": false,
      "required": ["start", "end"],
      "properties": {
        "start": {"type": "number"},
        "end": {"type": "number"}
      }
    },
    "params": {
      "type": "array",
      "minItems": 1,
      "items": {
        "type": "object",
        "additionalProperties": false,
        "required": ["name", "label", "value"],
        "properties": {
          "name": {"type": "string", "minLength": 1},
          "label": {"type": "string", "minLength": 1},
          "value": {
            "oneOf": [
              {"type": "number"},
              {"type": "boolean"},
              {"type": "string"},
              {"type": "object"}
            ]
          },
          "numeric": {
            "type": "object",
            "additionalProperties": false,
            "required": ["min", "max"],
            "properties": {
              "min": {"type": "number"},
              "max": {"type": "number"},
              "step": {"type": "number"}
            }
          }
        }
      }
    }
  }
})json";

constexpr const char EffectUpdateParamValueSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": [
    "effectNodeId",
    "paramName",
    "value"
  ],
  "properties": {
    "effectNodeId": {"type": "string", "minLength": 1},
    "paramName": {"type": "string", "minLength": 1},
    "value": {
      "oneOf": [
        {"type": "number"},
        {"type": "boolean"},
        {"type": "string"},
        {"type": "object"}
      ]
    }
  }
})json";

constexpr const char EffectConnectPortsSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["edgeId", "sourceNodeId", "sourcePort", "targetNodeId", "targetPort"],
  "properties": {
    "edgeId": {"type": "string", "minLength": 1},
    "sourceNodeId": {"type": "string", "minLength": 1},
    "sourcePort": {"type": "string", "minLength": 1},
    "targetNodeId": {"type": "string", "minLength": 1},
    "targetPort": {"type": "string", "minLength": 1},
    "order": {"type": "integer"}
  }
})json";

constexpr const char EffectDisconnectPortsSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["edgeId"],
  "properties": {
    "edgeId": {"type": "string", "minLength": 1}
  }
})json";

constexpr const char RenderPlanInspectSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "properties": {}
})json";

constexpr const char RuntimeInspectDiagnosticsSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "properties": {}
})json";

constexpr const char NoteCreateSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["title", "markdown"],
  "properties": {
    "title": {"type": "string", "minLength": 1},
    "markdown": {"type": "string", "minLength": 1}
  }
})json";

constexpr const char NoteUpdateSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["nodeId", "title", "markdown"],
  "properties": {
    "nodeId": {"type": "string", "minLength": 1},
    "title": {"type": "string", "minLength": 1},
    "markdown": {"type": "string", "minLength": 1}
  }
})json";

foundation::Error argumentError(const std::string& path, const std::string& message) {
  return foundation::Error{"agent.tool_arguments_invalid", path + ": " + message};
}

foundation::Result<Json::Value> parseArguments(const std::string& json) {
  Json::CharReaderBuilder builder;
  std::string errors;
  Json::Value root;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
  if (!reader->parse(json.data(), json.data() + json.size(), &root, &errors)) {
    return argumentError("$", "Invalid JSON. " + errors);
  }
  if (!root.isObject()) {
    return argumentError("$", "Tool arguments must be a JSON object.");
  }
  return root;
}

foundation::Result<Json::Value> requiredMember(const Json::Value& object, const char* key, const std::string& path) {
  if (!object.isObject()) {
    return argumentError(path, "Expected object.");
  }
  if (!object.isMember(key)) {
    return argumentError(path + "." + key, "Missing required field.");
  }
  return object[key];
}

foundation::Result<void> requireOnlyMembers(
  const Json::Value& object,
  std::initializer_list<std::string_view> allowed,
  const std::string& path
) {
  if (!object.isObject()) {
    return argumentError(path, "Expected object.");
  }

  for (const std::string& member : object.getMemberNames()) {
    bool expected = false;
    for (std::string_view allowedMember : allowed) {
      if (member == allowedMember) {
        expected = true;
        break;
      }
    }
    if (!expected) {
      return argumentError(path + "." + member, "Unexpected tool argument.");
    }
  }

  return {};
}

foundation::Result<std::string> requiredStringMember(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isString()) {
    return argumentError(path + "." + key, "Expected string.");
  }
  if (value.value().asString().empty()) {
    return argumentError(path + "." + key, "Expected non-empty string.");
  }
  return value.value().asString();
}

foundation::Result<std::optional<std::string>> optionalStringMember(
  const Json::Value& object,
  const char* key,
  const std::string& path
) {
  if (!object.isMember(key)) {
    return std::optional<std::string>{};
  }
  if (!object[key].isString()) {
    return argumentError(path + "." + key, "Expected string.");
  }
  return std::optional<std::string>{object[key].asString()};
}

foundation::Result<double> requiredDoubleMember(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isNumeric()) {
    return argumentError(path + "." + key, "Expected number.");
  }
  return value.value().asDouble();
}

foundation::Result<std::optional<double>> optionalDoubleMember(
  const Json::Value& object,
  const char* key,
  const std::string& path
) {
  if (!object.isMember(key)) {
    return std::optional<double>{};
  }
  if (!object[key].isNumeric()) {
    return argumentError(path + "." + key, "Expected number.");
  }
  return std::optional<double>{object[key].asDouble()};
}

foundation::Result<std::int64_t> requiredInt64Member(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isInt64()) {
    return argumentError(path + "." + key, "Expected integer.");
  }
  return value.value().asInt64();
}

foundation::Result<std::optional<foundation::Resolution>> optionalResolutionMember(
  const Json::Value& object,
  const char* key,
  const std::string& path
) {
  if (!object.isMember(key)) {
    return std::optional<foundation::Resolution>{};
  }
  if (!object[key].isObject()) {
    return argumentError(path + "." + key, "Expected object.");
  }
  auto members = requireOnlyMembers(object[key], {"width", "height"}, path + "." + key);
  if (!members) {
    return members.error();
  }
  auto width = requiredInt64Member(object[key], "width", path + "." + key);
  if (!width) {
    return width.error();
  }
  auto height = requiredInt64Member(object[key], "height", path + "." + key);
  if (!height) {
    return height.error();
  }
  return std::optional<foundation::Resolution>{foundation::Resolution{
    static_cast<int>(width.value()),
    static_cast<int>(height.value())
  }};
}

foundation::Result<std::optional<foundation::FrameRate>> optionalFrameRateMember(
  const Json::Value& object,
  const char* key,
  const std::string& path
) {
  if (!object.isMember(key)) {
    return std::optional<foundation::FrameRate>{};
  }
  if (!object[key].isObject()) {
    return argumentError(path + "." + key, "Expected object.");
  }
  auto members = requireOnlyMembers(object[key], {"numerator", "denominator"}, path + "." + key);
  if (!members) {
    return members.error();
  }
  auto numerator = requiredInt64Member(object[key], "numerator", path + "." + key);
  if (!numerator) {
    return numerator.error();
  }
  auto denominator = requiredInt64Member(object[key], "denominator", path + "." + key);
  if (!denominator) {
    return denominator.error();
  }
  return std::optional<foundation::FrameRate>{foundation::FrameRate{
    static_cast<std::int32_t>(numerator.value()),
    static_cast<std::int32_t>(denominator.value())
  }};
}

foundation::Result<Json::Value> requiredArrayMember(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isArray()) {
    return argumentError(path + "." + key, "Expected array.");
  }
  return value.value();
}

foundation::Result<foundation::TimeRange> parseTimeRange(const Json::Value& object, const std::string& path) {
  if (!object.isObject()) {
    return argumentError(path, "Expected time range object.");
  }
  auto members = requireOnlyMembers(object, {"start", "end"}, path);
  if (!members) {
    return members.error();
  }
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

foundation::Result<foundation::Vec2> parseVec2(const Json::Value& object, const std::string& path) {
  if (!object.isObject()) {
    return argumentError(path, "Expected vector object.");
  }
  auto members = requireOnlyMembers(object, {"x", "y"}, path);
  if (!members) {
    return members.error();
  }
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

foundation::Result<timeline::EffectImplementationKind> parseImplementationKind(const std::string& value, const std::string& path) {
  if (value == "builtin") {
    return timeline::EffectImplementationKind::Builtin;
  }
  if (value == "python") {
    return timeline::EffectImplementationKind::Python;
  }
  if (value == "shader") {
    return timeline::EffectImplementationKind::Shader;
  }
  return argumentError(path, "Expected builtin, python, or shader.");
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
  return argumentError(path, "Expected video, audio, or image.");
}

foundation::Result<timeline::TrackKind> parseTrackKind(const std::string& value, const std::string& path) {
  if (value == "visual") {
    return timeline::TrackKind::Visual;
  }
  if (value == "audio") {
    return timeline::TrackKind::Audio;
  }
  return argumentError(path, "Expected visual or audio.");
}

foundation::Result<asset::AssetMediaType> parseAssetMediaType(const std::string& value, const std::string& path) {
  if (value == "video") {
    return asset::AssetMediaType::Video;
  }
  if (value == "audio") {
    return asset::AssetMediaType::Audio;
  }
  if (value == "image") {
    return asset::AssetMediaType::Image;
  }
  return argumentError(path, "Expected video, audio, or image.");
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
    return argumentError(path, "Expected parameter scalar or typed object.");
  }

  if (value.isMember("width") || value.isMember("height")) {
    auto members = requireOnlyMembers(value, {"x", "y", "width", "height"}, path);
    if (!members) {
      return members.error();
    }
  } else if (value.isMember("z")) {
    auto members = requireOnlyMembers(value, {"x", "y", "z"}, path);
    if (!members) {
      return members.error();
    }
  } else {
    auto members = requireOnlyMembers(value, {"x", "y"}, path);
    if (!members) {
      return members.error();
    }
  }

  auto x = requiredDoubleMember(value, "x", path);
  if (!x) {
    return x.error();
  }
  auto y = requiredDoubleMember(value, "y", path);
  if (!y) {
    return y.error();
  }

  if (value.isMember("width") || value.isMember("height")) {
    auto width = requiredDoubleMember(value, "width", path);
    if (!width) {
      return width.error();
    }
    auto height = requiredDoubleMember(value, "height", path);
    if (!height) {
      return height.error();
    }
    return timeline::ParamValue{foundation::Rect{x.value(), y.value(), width.value(), height.value()}};
  }

  if (value.isMember("z")) {
    auto z = requiredDoubleMember(value, "z", path);
    if (!z) {
      return z.error();
    }
    return timeline::ParamValue{foundation::Vec3{x.value(), y.value(), z.value()}};
  }

  return timeline::ParamValue{foundation::Vec2{x.value(), y.value()}};
}

foundation::Result<timeline::ParamSet> parseParamSet(const Json::Value& array, const std::string& path) {
  if (!array.isArray()) {
    return argumentError(path, "Expected parameter array.");
  }
  if (array.empty()) {
    return argumentError(path, "Agent-created effects must expose at least one editable parameter.");
  }

  timeline::ParamSet params;
  for (Json::ArrayIndex index = 0; index < array.size(); ++index) {
    const std::string itemPath = path + "[" + std::to_string(index) + "]";
    if (!array[index].isObject()) {
      return argumentError(itemPath, "Expected parameter object.");
    }
    auto members = requireOnlyMembers(array[index], {"name", "label", "value", "numeric"}, itemPath);
    if (!members) {
      return members.error();
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

    auto label = requiredStringMember(array[index], "label", itemPath);
    if (!label) {
      return label.error();
    }
    timeline::Param::Control control{label.value(), std::nullopt};
    if (array[index].isMember("numeric")) {
      if (!std::holds_alternative<double>(paramValue.value())) {
        return argumentError(itemPath + ".numeric", "Numeric controls require a numeric parameter value.");
      }
      const Json::Value& numericObject = array[index]["numeric"];
      if (!numericObject.isObject()) {
        return argumentError(itemPath + ".numeric", "Expected numeric control object.");
      }
      auto numericMembers = requireOnlyMembers(numericObject, {"min", "max", "step"}, itemPath + ".numeric");
      if (!numericMembers) {
        return numericMembers.error();
      }
      auto min = requiredDoubleMember(numericObject, "min", itemPath + ".numeric");
      if (!min) {
        return min.error();
      }
      auto max = requiredDoubleMember(numericObject, "max", itemPath + ".numeric");
      if (!max) {
        return max.error();
      }
      timeline::Param::NumericControl numeric{min.value(), max.value(), std::nullopt};
      if (numericObject.isMember("step")) {
        auto step = requiredDoubleMember(numericObject, "step", itemPath + ".numeric");
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

foundation::Result<std::vector<timeline::EffectPort>> parsePorts(const Json::Value& array, const std::string& path) {
  if (!array.isArray()) {
    return argumentError(path, "Expected port array.");
  }

  std::vector<timeline::EffectPort> ports;
  ports.reserve(array.size());
  for (Json::ArrayIndex index = 0; index < array.size(); ++index) {
    if (!array[index].isString()) {
      return argumentError(path + "[" + std::to_string(index) + "]", "Expected port name string.");
    }
    if (array[index].asString().empty()) {
      return argumentError(path + "[" + std::to_string(index) + "]", "Expected non-empty port name.");
    }
    ports.push_back(timeline::EffectPort{array[index].asString()});
  }
  return ports;
}

foundation::Result<timeline::EffectPortSet> parsePortSet(const Json::Value& object, const std::string& path) {
  auto inputs = requiredArrayMember(object, "inputPorts", path);
  if (!inputs) {
    return inputs.error();
  }
  auto outputs = requiredArrayMember(object, "outputPorts", path);
  if (!outputs) {
    return outputs.error();
  }
  auto parsedInputs = parsePorts(inputs.value(), path + ".inputPorts");
  if (!parsedInputs) {
    return parsedInputs.error();
  }
  auto parsedOutputs = parsePorts(outputs.value(), path + ".outputPorts");
  if (!parsedOutputs) {
    return parsedOutputs.error();
  }
  return timeline::EffectPortSet{parsedInputs.value(), parsedOutputs.value()};
}

foundation::Result<project::ProjectSnapshot> readProjectSnapshot(
  AgentToolContext& context,
  std::string_view operation
) {
  auto query = context.queries.query(project::GetProjectSnapshotQuery{});
  if (!query) {
    return query.error();
  }
  const auto* snapshotResult = std::get_if<project::ProjectSnapshotResult>(&query.value());
  if (snapshotResult == nullptr) {
    return foundation::Error{
      "agent.project_snapshot_result_missing",
      std::string{operation} + " query returned the wrong result type."
    };
  }
  return snapshotResult->snapshot;
}

foundation::Result<project::AssetCatalogResult> readAssetCatalog(
  AgentToolContext& context,
  std::string_view operation
) {
  auto query = context.queries.query(project::GetAssetCatalogQuery{});
  if (!query) {
    return query.error();
  }
  const auto* assetCatalogResult = std::get_if<project::AssetCatalogResult>(&query.value());
  if (assetCatalogResult == nullptr) {
    return foundation::Error{
      "agent.asset_catalog_result_missing",
      std::string{operation} + " query returned the wrong result type."
    };
  }
  return *assetCatalogResult;
}

const char* assetMediaTypeText(asset::AssetMediaType mediaType) {
  switch (mediaType) {
    case asset::AssetMediaType::Video:
      return "video";
    case asset::AssetMediaType::Audio:
      return "audio";
    case asset::AssetMediaType::Image:
      return "image";
  }

  return "unknown";
}

const char* clipKindText(timeline::ClipKind kind) {
  switch (kind) {
    case timeline::ClipKind::Video:
      return "video";
    case timeline::ClipKind::Audio:
      return "audio";
    case timeline::ClipKind::Image:
      return "image";
  }

  return "unknown";
}

const char* trackKindText(timeline::TrackKind kind) {
  switch (kind) {
    case timeline::TrackKind::Visual:
      return "visual";
    case timeline::TrackKind::Audio:
      return "audio";
  }

  return "unknown";
}

void writeAssetJson(std::ostream& stream, const asset::Asset& asset) {
  stream << '{'
         << "\"assetId\":" << foundation::jsonQuoted(asset.id.value())
         << ",\"name\":" << foundation::jsonQuoted(asset.name)
         << ",\"mediaType\":" << foundation::jsonQuoted(assetMediaTypeText(asset.metadata.mediaType))
         << ",\"sourcePath\":" << foundation::jsonQuoted(asset.metadata.sourcePath.value);

  if (asset.metadata.thumbnailPath.has_value()) {
    stream << ",\"thumbnailPath\":" << foundation::jsonQuoted(asset.metadata.thumbnailPath->value);
  }
  if (asset.metadata.duration.has_value()) {
    stream << ",\"duration\":" << asset.metadata.duration->value;
  }
  if (asset.metadata.dimensions.has_value()) {
    stream << ",\"dimensions\":{\"width\":" << asset.metadata.dimensions->width
           << ",\"height\":" << asset.metadata.dimensions->height
           << '}';
  }
  if (asset.metadata.frameRate.has_value()) {
    stream << ",\"frameRate\":{\"numerator\":" << asset.metadata.frameRate->numerator
           << ",\"denominator\":" << asset.metadata.frameRate->denominator
           << '}';
  }

  stream << '}';
}

void writeCompositionClipJson(std::ostream& stream, const project::CompositionClipSummary& clip) {
  stream << '{'
         << "\"nodeId\":" << foundation::jsonQuoted(clip.nodeId.value())
         << ",\"trackNodeId\":" << foundation::jsonQuoted(clip.trackNodeId.value())
         << ",\"assetId\":" << foundation::jsonQuoted(clip.assetId.value())
         << ",\"kind\":" << foundation::jsonQuoted(clipKindText(clip.kind))
         << ",\"timelineRange\":{\"start\":" << clip.timelineRange.start.value
         << ",\"end\":" << clip.timelineRange.end.value
         << "},\"enabled\":" << (clip.enabled ? "true" : "false")
         << '}';
}

void writeCompositionTrackJson(std::ostream& stream, const project::CompositionTrackSummary& track) {
  stream << '{'
         << "\"nodeId\":" << foundation::jsonQuoted(track.nodeId.value())
         << ",\"name\":" << foundation::jsonQuoted(track.name)
         << ",\"kind\":" << foundation::jsonQuoted(trackKindText(track.kind))
         << ",\"enabled\":" << (track.enabled ? "true" : "false")
         << ",\"clips\":[";
  for (std::size_t index = 0; index < track.clips.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    writeCompositionClipJson(stream, track.clips[index]);
  }
  stream << "]}";
}

void writeCompositionCameraJson(std::ostream& stream, const project::CompositionCameraSummary& camera) {
  stream << '{'
         << "\"nodeId\":" << foundation::jsonQuoted(camera.nodeId.value())
         << ",\"name\":" << foundation::jsonQuoted(camera.name)
         << ",\"enabled\":" << (camera.enabled ? "true" : "false")
         << '}';
}

void writeCompositionEffectJson(std::ostream& stream, const project::CompositionEffectSummary& effect) {
  stream << '{'
         << "\"nodeId\":" << foundation::jsonQuoted(effect.nodeId.value())
         << ",\"targetNodeId\":" << foundation::jsonQuoted(effect.targetNodeId.value())
         << ",\"displayName\":" << foundation::jsonQuoted(effect.displayName)
         << ",\"enabled\":" << (effect.enabled ? "true" : "false")
         << '}';
}

void writeCompositionJson(std::ostream& stream, const project::CompositionSummary& composition) {
  stream << '{'
         << "\"nodeId\":" << foundation::jsonQuoted(composition.nodeId.value())
         << ",\"name\":" << foundation::jsonQuoted(composition.name)
         << ",\"enabled\":" << (composition.enabled ? "true" : "false")
         << ",\"tracks\":[";
  for (std::size_t index = 0; index < composition.tracks.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    writeCompositionTrackJson(stream, composition.tracks[index]);
  }
  stream << "],\"cameras\":[";
  for (std::size_t index = 0; index < composition.cameras.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    writeCompositionCameraJson(stream, composition.cameras[index]);
  }
  stream << "],\"effects\":[";
  for (std::size_t index = 0; index < composition.effects.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    writeCompositionEffectJson(stream, composition.effects[index]);
  }
  stream << "]}";
}

void writeRenderPlanLayerJson(std::ostream& stream, const project::RenderPlanLayerSummary& layer) {
  stream << '{'
         << "\"nodeId\":" << foundation::jsonQuoted(layer.nodeId.value())
         << ",\"name\":" << foundation::jsonQuoted(layer.name)
         << '}';
}

void writeRenderPlanClipJson(std::ostream& stream, const project::RenderPlanClipSummary& clip) {
  stream << '{'
         << "\"nodeId\":" << foundation::jsonQuoted(clip.nodeId.value())
         << ",\"trackNodeId\":" << foundation::jsonQuoted(clip.trackNodeId.value())
         << ",\"assetId\":" << foundation::jsonQuoted(clip.assetId.value())
         << ",\"kind\":" << foundation::jsonQuoted(clipKindText(clip.kind))
         << ",\"timelineRange\":{\"start\":" << clip.timelineRange.start.value
         << ",\"end\":" << clip.timelineRange.end.value
         << "}}";
}

void writeRenderPlanCameraJson(std::ostream& stream, const project::RenderPlanCameraSummary& camera) {
  stream << '{'
         << "\"nodeId\":" << foundation::jsonQuoted(camera.nodeId.value())
         << ",\"name\":" << foundation::jsonQuoted(camera.name)
         << '}';
}

void writeRenderPlanEffectGraphJson(std::ostream& stream, const project::RenderPlanEffectGraphSummary& effectGraph) {
  stream << '{'
         << "\"graphId\":" << foundation::jsonQuoted(effectGraph.graphId.value())
         << ",\"targetNodeId\":" << foundation::jsonQuoted(effectGraph.targetNodeId.value())
         << ",\"nodeCount\":" << effectGraph.nodeCount
         << ",\"edgeCount\":" << effectGraph.edgeCount
         << '}';
}

void writeRenderPlanInspectJson(std::ostream& stream, const project::RenderPlanInspectResult& result) {
  stream << '{'
         << "\"projectId\":" << foundation::jsonQuoted(result.projectId.value())
         << ",\"revision\":" << foundation::jsonQuoted(result.revision.value())
         << ",\"duration\":" << result.duration.value
         << ",\"assetCount\":" << result.assetCount
         << ",\"layers\":[";
  for (std::size_t index = 0; index < result.layers.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    writeRenderPlanLayerJson(stream, result.layers[index]);
  }
  stream << "],\"audioTracks\":[";
  for (std::size_t index = 0; index < result.audioTracks.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    writeRenderPlanLayerJson(stream, result.audioTracks[index]);
  }
  stream << "],\"clips\":[";
  for (std::size_t index = 0; index < result.clips.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    writeRenderPlanClipJson(stream, result.clips[index]);
  }
  stream << "],\"audioClips\":[";
  for (std::size_t index = 0; index < result.audioClips.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    writeRenderPlanClipJson(stream, result.audioClips[index]);
  }
  stream << "],\"cameras\":[";
  for (std::size_t index = 0; index < result.cameras.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    writeRenderPlanCameraJson(stream, result.cameras[index]);
  }
  stream << "],\"effectGraphs\":[";
  for (std::size_t index = 0; index < result.effectGraphs.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    writeRenderPlanEffectGraphJson(stream, result.effectGraphs[index]);
  }
  stream << "],\"diagnosticCount\":" << result.diagnosticCount
         << '}';
}

const char* runtimeDiagnosticSeverityText(project::RuntimeDiagnosticSeveritySummary severity) {
  switch (severity) {
    case project::RuntimeDiagnosticSeveritySummary::Info:
      return "info";
    case project::RuntimeDiagnosticSeveritySummary::Warning:
      return "warning";
    case project::RuntimeDiagnosticSeveritySummary::Error:
      return "error";
  }

  std::abort();
}

void writeRuntimeDiagnosticJson(std::ostream& stream, const project::RuntimeDiagnosticSummary& diagnostic) {
  stream << '{'
         << "\"code\":" << foundation::jsonQuoted(diagnostic.code)
         << ",\"severity\":" << foundation::jsonQuoted(runtimeDiagnosticSeverityText(diagnostic.severity))
         << ",\"location\":{\"projectId\":" << foundation::jsonQuoted(diagnostic.projectId.value())
         << ",\"revision\":" << foundation::jsonQuoted(diagnostic.revision.value());
  if (diagnostic.nodeId.has_value()) {
    stream << ",\"nodeId\":" << foundation::jsonQuoted(diagnostic.nodeId->value());
  }
  stream << "},\"message\":" << foundation::jsonQuoted(diagnostic.message)
         << '}';
}

void writeRuntimeInspectDiagnosticsJson(
  std::ostream& stream,
  const project::RuntimeInspectDiagnosticsResult& result
) {
  stream << '{'
         << "\"revision\":" << foundation::jsonQuoted(result.revision.value())
         << ",\"diagnostics\":[";
  for (std::size_t index = 0; index < result.diagnostics.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    writeRuntimeDiagnosticJson(stream, result.diagnostics[index]);
  }
  stream << "]}";
}

} // namespace

foundation::Result<void> registerProjectTools(AgentToolRegistry& registry) {
  auto registered = registry.registerTool(makeProjectInspectTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeAssetListTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeAssetImportTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeCompositionInspectTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeCameraCreateTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeCameraUpdateTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeTimelineCreateTrackTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeTimelineCreateClipTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeTimelineMoveClipTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeTimelineTrimClipTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeTimelineUpdateClipTransformTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeEffectCreateNodeTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeEffectUpdateParamValueTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeEffectConnectPortsTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeEffectDisconnectPortsTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeRenderPlanInspectTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeRuntimeInspectDiagnosticsTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeNoteCreateTool());
  if (!registered) {
    return registered.error();
  }
  registered = registry.registerTool(makeNoteUpdateTool());
  if (!registered) {
    return registered.error();
  }
  return {};
}

AgentTool makeProjectInspectTool() {
  return AgentTool{
    foundation::ToolId{"tool_project_inspect"},
    "project.inspect",
    "Inspect Project",
    "Returns the current project revision and graph counts.",
    ProjectInspectSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(arguments.value(), {}, "$");
      if (!members) {
        return members.error();
      }
      auto snapshot = readProjectSnapshot(context, "Project inspect");
      if (!snapshot) {
        return snapshot.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"projectId\":" << foundation::jsonQuoted(snapshot.value().info.id.value())
              << ",\"revision\":" << foundation::jsonQuoted(snapshot.value().revision.value())
              << ",\"revisionNumber\":" << snapshot.value().revisionNumber
              << ",\"canonicalHash\":" << foundation::jsonQuoted(snapshot.value().canonicalHash.toHex())
              << ",\"graph\":{\"nodes\":" << snapshot.value().graph.nodes().size()
              << ",\"edges\":" << snapshot.value().graph.edges().size()
              << "},\"assets\":{\"count\":" << snapshot.value().assets.assets().size()
              << "}}";

      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        snapshot.value().revision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeAssetListTool() {
  return AgentTool{
    foundation::ToolId{"tool_asset_list"},
    "asset.list",
    "List Assets",
    "Lists project assets from the canonical asset catalog query.",
    AssetListSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(arguments.value(), {}, "$");
      if (!members) {
        return members.error();
      }
      auto catalog = readAssetCatalog(context, "Asset list");
      if (!catalog) {
        return catalog.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"revision\":" << foundation::jsonQuoted(catalog.value().revision.value())
              << ",\"assets\":[";
      const std::vector<asset::Asset>& assets = catalog.value().assets.assets();
      for (std::size_t index = 0; index < assets.size(); ++index) {
        if (index != 0) {
          payload << ',';
        }
        writeAssetJson(payload, assets[index]);
      }
      payload << "]}";

      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        catalog.value().revision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeAssetImportTool() {
  return AgentTool{
    foundation::ToolId{"tool_asset_import"},
    "asset.import",
    "Import Asset",
    "Registers an explicitly described asset through Project Core.",
    AssetImportSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(
        arguments.value(),
        {"assetId", "name", "mediaType", "sourcePath", "thumbnailPath", "duration", "dimensions", "frameRate"},
        "$"
      );
      if (!members) {
        return members.error();
      }
      auto assetId = requiredStringMember(arguments.value(), "assetId", "$");
      if (!assetId) {
        return assetId.error();
      }
      auto name = requiredStringMember(arguments.value(), "name", "$");
      if (!name) {
        return name.error();
      }
      auto mediaTypeName = requiredStringMember(arguments.value(), "mediaType", "$");
      if (!mediaTypeName) {
        return mediaTypeName.error();
      }
      auto mediaType = parseAssetMediaType(mediaTypeName.value(), "$.mediaType");
      if (!mediaType) {
        return mediaType.error();
      }
      auto sourcePath = requiredStringMember(arguments.value(), "sourcePath", "$");
      if (!sourcePath) {
        return sourcePath.error();
      }
      auto thumbnailPath = optionalStringMember(arguments.value(), "thumbnailPath", "$");
      if (!thumbnailPath) {
        return thumbnailPath.error();
      }
      auto duration = optionalDoubleMember(arguments.value(), "duration", "$");
      if (!duration) {
        return duration.error();
      }
      auto dimensions = optionalResolutionMember(arguments.value(), "dimensions", "$");
      if (!dimensions) {
        return dimensions.error();
      }
      auto frameRate = optionalFrameRateMember(arguments.value(), "frameRate", "$");
      if (!frameRate) {
        return frameRate.error();
      }

      std::optional<foundation::FilePath> parsedThumbnailPath;
      if (thumbnailPath.value().has_value()) {
        parsedThumbnailPath = foundation::FilePath{thumbnailPath.value().value()};
      }
      std::optional<foundation::TimeSeconds> parsedDuration;
      if (duration.value().has_value()) {
        parsedDuration = foundation::TimeSeconds{duration.value().value()};
      }

      const foundation::CommandId commandId = context.ids.nextCommandId();
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::RegisterAssetCommand{
          asset::Asset{
            foundation::AssetId{assetId.value()},
            name.value(),
            asset::AssetMetadata{
              mediaType.value(),
              foundation::FilePath{sourcePath.value()},
              parsedThumbnailPath,
              parsedDuration,
              dimensions.value(),
              frameRate.value()
            }
          }
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"assetId\":" << foundation::jsonQuoted(assetId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeCompositionInspectTool() {
  return AgentTool{
    foundation::ToolId{"tool_composition_inspect"},
    "composition.inspect",
    "Inspect Composition",
    "Returns authored composition membership through Project Core.",
    CompositionInspectSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(arguments.value(), {}, "$");
      if (!members) {
        return members.error();
      }
      auto query = context.queries.query(project::InspectCompositionsQuery{});
      if (!query) {
        return query.error();
      }
      const auto* compositionResult = std::get_if<project::CompositionInspectResult>(&query.value());
      if (compositionResult == nullptr) {
        return foundation::Error{
          "agent.composition_inspect_result_missing",
          "Composition inspect query returned the wrong result type."
        };
      }

      std::ostringstream payload;
      payload << '{'
              << "\"revision\":" << foundation::jsonQuoted(compositionResult->revision.value())
              << ",\"compositions\":[";
      for (std::size_t index = 0; index < compositionResult->compositions.size(); ++index) {
        if (index != 0) {
          payload << ',';
        }
        writeCompositionJson(payload, compositionResult->compositions[index]);
      }
      payload << "]}";

      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        compositionResult->revision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeCameraCreateTool() {
  return AgentTool{
    foundation::ToolId{"tool_camera_create"},
    "camera.create",
    "Create Camera",
    "Creates a camera in an explicit composition through Project Core. Camera motion and framing edits should be represented as editable effects.",
    CameraCreateSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(arguments.value(), {"compositionNodeId", "name", "focalLength"}, "$");
      if (!members) {
        return members.error();
      }
      auto compositionNodeId = requiredStringMember(arguments.value(), "compositionNodeId", "$");
      if (!compositionNodeId) {
        return compositionNodeId.error();
      }
      auto name = requiredStringMember(arguments.value(), "name", "$");
      if (!name) {
        return name.error();
      }
      auto focalLength = requiredDoubleMember(arguments.value(), "focalLength", "$");
      if (!focalLength) {
        return focalLength.error();
      }

      const foundation::CommandId commandId = context.ids.nextCommandId();
      const foundation::NodeId cameraNodeId = context.ids.nextNodeId("camera");
      const foundation::EdgeId containmentEdgeId = context.ids.nextEdgeId("contains_camera");
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::CreateCameraCommand{
          cameraNodeId,
          foundation::NodeId{compositionNodeId.value()},
          containmentEdgeId,
          timeline::CameraPayload{
            name.value(),
            timeline::Transform{},
            timeline::CameraLens{focalLength.value()}
          },
          0
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"cameraNodeId\":" << foundation::jsonQuoted(cameraNodeId.value())
              << ",\"containmentEdgeId\":" << foundation::jsonQuoted(containmentEdgeId.value())
              << ",\"compositionNodeId\":" << foundation::jsonQuoted(compositionNodeId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeCameraUpdateTool() {
  return AgentTool{
    foundation::ToolId{"tool_camera_update"},
    "camera.update",
    "Update Camera",
    "Updates camera identity and lens properties through Project Core while preserving its authored transform.",
    CameraUpdateSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(arguments.value(), {"cameraNodeId", "name", "focalLength"}, "$");
      if (!members) {
        return members.error();
      }
      auto cameraNodeId = requiredStringMember(arguments.value(), "cameraNodeId", "$");
      if (!cameraNodeId) {
        return cameraNodeId.error();
      }
      auto name = requiredStringMember(arguments.value(), "name", "$");
      if (!name) {
        return name.error();
      }
      auto focalLength = requiredDoubleMember(arguments.value(), "focalLength", "$");
      if (!focalLength) {
        return focalLength.error();
      }

      auto snapshot = readProjectSnapshot(context, "camera.update");
      if (!snapshot) {
        return snapshot.error();
      }

      const foundation::NodeId cameraId{cameraNodeId.value()};
      const graph::GraphNode* node = snapshot.value().graph.findNode(cameraId);
      if (node == nullptr || node->kind != graph::NodeKind::Camera) {
        return foundation::Error{"agent.camera_missing", "Camera update requires an existing camera node."};
      }
      const auto* currentPayload = std::get_if<timeline::CameraPayload>(&node->payload);
      if (currentPayload == nullptr) {
        return foundation::Error{"agent.camera_payload_missing", "Camera node must contain a camera payload."};
      }

      const foundation::CommandId commandId = context.ids.nextCommandId();
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::UpdateCameraCommand{
          cameraId,
          timeline::CameraPayload{
            name.value(),
            currentPayload->transform,
            timeline::CameraLens{focalLength.value()}
          }
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"cameraNodeId\":" << foundation::jsonQuoted(cameraId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeTimelineCreateTrackTool() {
  return AgentTool{
    foundation::ToolId{"tool_timeline_create_track"},
    "timeline.create_track",
    "Create Timeline Track",
    "Creates a timeline track in an explicit composition through Project Core.",
    TimelineCreateTrackSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(arguments.value(), {"compositionNodeId", "name", "kind"}, "$");
      if (!members) {
        return members.error();
      }
      auto compositionNodeId = requiredStringMember(arguments.value(), "compositionNodeId", "$");
      if (!compositionNodeId) {
        return compositionNodeId.error();
      }
      auto name = requiredStringMember(arguments.value(), "name", "$");
      if (!name) {
        return name.error();
      }
      auto kindName = requiredStringMember(arguments.value(), "kind", "$");
      if (!kindName) {
        return kindName.error();
      }
      auto kind = parseTrackKind(kindName.value(), "$.kind");
      if (!kind) {
        return kind.error();
      }

      const foundation::CommandId commandId = context.ids.nextCommandId();
      const foundation::NodeId trackNodeId = context.ids.nextNodeId("track");
      const foundation::EdgeId containmentEdgeId = context.ids.nextEdgeId("contains_track");
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::CreateTrackCommand{
          trackNodeId,
          foundation::NodeId{compositionNodeId.value()},
          containmentEdgeId,
          name.value(),
          kind.value(),
          0
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"trackNodeId\":" << foundation::jsonQuoted(trackNodeId.value())
              << ",\"containmentEdgeId\":" << foundation::jsonQuoted(containmentEdgeId.value())
              << ",\"compositionNodeId\":" << foundation::jsonQuoted(compositionNodeId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeTimelineCreateClipTool() {
  return AgentTool{
    foundation::ToolId{"tool_timeline_create_clip"},
    "timeline.create_clip",
    "Create Timeline Clip",
    "Creates a clip with explicit timing, media kind, asset, and track through Project Core.",
    TimelineCreateClipSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(
        arguments.value(),
        {"trackNodeId", "assetId", "kind", "timelineRange", "sourceRange", "playbackRate"},
        "$"
      );
      if (!members) {
        return members.error();
      }
      auto trackNodeId = requiredStringMember(arguments.value(), "trackNodeId", "$");
      if (!trackNodeId) {
        return trackNodeId.error();
      }
      auto assetId = requiredStringMember(arguments.value(), "assetId", "$");
      if (!assetId) {
        return assetId.error();
      }
      auto kindName = requiredStringMember(arguments.value(), "kind", "$");
      if (!kindName) {
        return kindName.error();
      }
      auto kind = parseClipKind(kindName.value(), "$.kind");
      if (!kind) {
        return kind.error();
      }
      auto timelineRangeObject = requiredMember(arguments.value(), "timelineRange", "$");
      if (!timelineRangeObject) {
        return timelineRangeObject.error();
      }
      auto timelineRange = parseTimeRange(timelineRangeObject.value(), "$.timelineRange");
      if (!timelineRange) {
        return timelineRange.error();
      }
      auto sourceRangeObject = requiredMember(arguments.value(), "sourceRange", "$");
      if (!sourceRangeObject) {
        return sourceRangeObject.error();
      }
      auto sourceRange = parseTimeRange(sourceRangeObject.value(), "$.sourceRange");
      if (!sourceRange) {
        return sourceRange.error();
      }
      auto playbackRate = requiredDoubleMember(arguments.value(), "playbackRate", "$");
      if (!playbackRate) {
        return playbackRate.error();
      }

      const foundation::CommandId commandId = context.ids.nextCommandId();
      const foundation::NodeId clipNodeId = context.ids.nextNodeId("clip");
      const foundation::EdgeId containmentEdgeId = context.ids.nextEdgeId("contains_clip");
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::CreateClipCommand{
          clipNodeId,
          foundation::NodeId{trackNodeId.value()},
          containmentEdgeId,
          timeline::ClipPayload{
            kind.value(),
            timelineRange.value(),
            sourceRange.value(),
            playbackRate.value(),
            foundation::AssetId{assetId.value()},
            timeline::Transform{}
          },
          0
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"clipNodeId\":" << foundation::jsonQuoted(clipNodeId.value())
              << ",\"containmentEdgeId\":" << foundation::jsonQuoted(containmentEdgeId.value())
              << ",\"trackNodeId\":" << foundation::jsonQuoted(trackNodeId.value())
              << ",\"assetId\":" << foundation::jsonQuoted(assetId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeTimelineMoveClipTool() {
  return AgentTool{
    foundation::ToolId{"tool_timeline_move_clip"},
    "timeline.move_clip",
    "Move Timeline Clip",
    "Moves a clip to an explicit timeline start through Project Core.",
    TimelineMoveClipSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(arguments.value(), {"clipNodeId", "newStart"}, "$");
      if (!members) {
        return members.error();
      }
      auto clipNodeId = requiredStringMember(arguments.value(), "clipNodeId", "$");
      if (!clipNodeId) {
        return clipNodeId.error();
      }
      auto newStart = requiredDoubleMember(arguments.value(), "newStart", "$");
      if (!newStart) {
        return newStart.error();
      }

      const foundation::CommandId commandId = context.ids.nextCommandId();
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::MoveClipCommand{
          foundation::NodeId{clipNodeId.value()},
          foundation::TimeSeconds{newStart.value()}
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"clipNodeId\":" << foundation::jsonQuoted(clipNodeId.value())
              << ",\"newStart\":" << newStart.value()
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeTimelineTrimClipTool() {
  return AgentTool{
    foundation::ToolId{"tool_timeline_trim_clip"},
    "timeline.trim_clip",
    "Trim Timeline Clip",
    "Trims a clip to explicit timeline and source ranges through Project Core.",
    TimelineTrimClipSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(arguments.value(), {"clipNodeId", "timelineRange", "sourceRange"}, "$");
      if (!members) {
        return members.error();
      }
      auto clipNodeId = requiredStringMember(arguments.value(), "clipNodeId", "$");
      if (!clipNodeId) {
        return clipNodeId.error();
      }
      auto timelineRangeObject = requiredMember(arguments.value(), "timelineRange", "$");
      if (!timelineRangeObject) {
        return timelineRangeObject.error();
      }
      auto timelineRange = parseTimeRange(timelineRangeObject.value(), "$.timelineRange");
      if (!timelineRange) {
        return timelineRange.error();
      }
      auto sourceRangeObject = requiredMember(arguments.value(), "sourceRange", "$");
      if (!sourceRangeObject) {
        return sourceRangeObject.error();
      }
      auto sourceRange = parseTimeRange(sourceRangeObject.value(), "$.sourceRange");
      if (!sourceRange) {
        return sourceRange.error();
      }

      const foundation::CommandId commandId = context.ids.nextCommandId();
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::TrimClipCommand{
          foundation::NodeId{clipNodeId.value()},
          timelineRange.value(),
          sourceRange.value()
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"clipNodeId\":" << foundation::jsonQuoted(clipNodeId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeTimelineUpdateClipTransformTool() {
  return AgentTool{
    foundation::ToolId{"tool_timeline_update_clip_transform"},
    "timeline.update_clip_transform",
    "Update Clip Transform",
    "Updates a clip's authored placement, scale, rotation, and opacity through Project Core while preserving its timing, media, and track membership.",
    TimelineUpdateClipTransformSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(
        arguments.value(),
        {"clipNodeId", "position", "scale", "rotationDegrees", "opacity"},
        "$"
      );
      if (!members) {
        return members.error();
      }
      auto clipNodeId = requiredStringMember(arguments.value(), "clipNodeId", "$");
      if (!clipNodeId) {
        return clipNodeId.error();
      }
      auto positionObject = requiredMember(arguments.value(), "position", "$");
      if (!positionObject) {
        return positionObject.error();
      }
      auto position = parseVec2(positionObject.value(), "$.position");
      if (!position) {
        return position.error();
      }
      auto scaleObject = requiredMember(arguments.value(), "scale", "$");
      if (!scaleObject) {
        return scaleObject.error();
      }
      auto scale = parseVec2(scaleObject.value(), "$.scale");
      if (!scale) {
        return scale.error();
      }
      auto rotationDegrees = requiredDoubleMember(arguments.value(), "rotationDegrees", "$");
      if (!rotationDegrees) {
        return rotationDegrees.error();
      }
      auto opacity = requiredDoubleMember(arguments.value(), "opacity", "$");
      if (!opacity) {
        return opacity.error();
      }

      auto snapshot = readProjectSnapshot(context, "timeline.update_clip_transform");
      if (!snapshot) {
        return snapshot.error();
      }

      const foundation::NodeId clipId{clipNodeId.value()};
      const graph::GraphNode* node = snapshot.value().graph.findNode(clipId);
      if (node == nullptr || node->kind != graph::NodeKind::Clip) {
        return foundation::Error{"agent.clip_missing", "Clip transform update requires an existing clip node."};
      }
      const auto* currentPayload = std::get_if<timeline::ClipPayload>(&node->payload);
      if (currentPayload == nullptr) {
        return foundation::Error{"agent.clip_payload_missing", "Clip node must contain a clip payload."};
      }

      const foundation::CommandId commandId = context.ids.nextCommandId();
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::UpdateClipCommand{
          clipId,
          timeline::ClipPayload{
            currentPayload->kind,
            currentPayload->timelineRange,
            currentPayload->sourceRange,
            currentPayload->playbackRate,
            currentPayload->assetId,
            timeline::Transform{
              position.value(),
              scale.value(),
              rotationDegrees.value(),
              opacity.value()
            }
          }
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"clipNodeId\":" << foundation::jsonQuoted(clipId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeEffectCreateNodeTool() {
  return AgentTool{
    foundation::ToolId{"tool_effect_create_node"},
    "effect.create_node",
    "Create Effect Node",
    "Creates an editable effect node with canonical code, ports, params, and user-facing parameter controls. The tool allocates project ids; agents provide edit intent and payload data only.",
    EffectCreateNodeSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(
        arguments.value(),
        {
          "targetNodeId",
          "displayName",
          "implementationKind",
          "language",
          "entrypoint",
          "source",
          "sourcePort",
          "targetPort",
          "inputPorts",
          "outputPorts",
          "activeRange",
          "params"
        },
        "$"
      );
      if (!members) {
        return members.error();
      }

      auto targetNodeId = requiredStringMember(arguments.value(), "targetNodeId", "$");
      if (!targetNodeId) {
        return targetNodeId.error();
      }
      auto displayName = requiredStringMember(arguments.value(), "displayName", "$");
      if (!displayName) {
        return displayName.error();
      }
      auto implementationKindName = requiredStringMember(arguments.value(), "implementationKind", "$");
      if (!implementationKindName) {
        return implementationKindName.error();
      }
      auto implementationKind = parseImplementationKind(implementationKindName.value(), "$.implementationKind");
      if (!implementationKind) {
        return implementationKind.error();
      }
      auto language = requiredStringMember(arguments.value(), "language", "$");
      if (!language) {
        return language.error();
      }
      auto entrypoint = requiredStringMember(arguments.value(), "entrypoint", "$");
      if (!entrypoint) {
        return entrypoint.error();
      }
      auto source = requiredStringMember(arguments.value(), "source", "$");
      if (!source) {
        return source.error();
      }
      auto sourcePort = requiredStringMember(arguments.value(), "sourcePort", "$");
      if (!sourcePort) {
        return sourcePort.error();
      }
      auto targetPort = requiredStringMember(arguments.value(), "targetPort", "$");
      if (!targetPort) {
        return targetPort.error();
      }
      auto activeRangeObject = requiredMember(arguments.value(), "activeRange", "$");
      if (!activeRangeObject) {
        return activeRangeObject.error();
      }
      auto activeRange = parseTimeRange(activeRangeObject.value(), "$.activeRange");
      if (!activeRange) {
        return activeRange.error();
      }
      auto ports = parsePortSet(arguments.value(), "$");
      if (!ports) {
        return ports.error();
      }
      auto paramsArray = requiredArrayMember(arguments.value(), "params", "$");
      if (!paramsArray) {
        return paramsArray.error();
      }
      auto params = parseParamSet(paramsArray.value(), "$.params");
      if (!params) {
        return params.error();
      }

      const foundation::CommandId commandId = context.ids.nextCommandId();
      const foundation::NodeId effectNodeId = context.ids.nextNodeId("effect");
      const foundation::EdgeId targetEdgeId = context.ids.nextEdgeId("effect_targets");
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::CreateEffectCommand{
          effectNodeId,
          foundation::NodeId{targetNodeId.value()},
          targetEdgeId,
          timeline::EffectPayload{
            displayName.value(),
            timeline::EffectImplementation{
              implementationKind.value(),
              entrypoint.value(),
              timeline::EffectSource{
                timeline::EffectSourceKind::InlineSource,
                language.value(),
                source.value(),
                std::nullopt,
                foundation::stableHash(source.value())
              }
            },
            ports.value(),
            params.value(),
            activeRange.value()
          },
          graph::PortName{sourcePort.value()},
          graph::PortName{targetPort.value()},
          0
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"effectNodeId\":" << foundation::jsonQuoted(effectNodeId.value())
              << ",\"targetEdgeId\":" << foundation::jsonQuoted(targetEdgeId.value())
              << ",\"targetNodeId\":" << foundation::jsonQuoted(targetNodeId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeEffectUpdateParamValueTool() {
  return AgentTool{
    foundation::ToolId{"tool_effect_update_param_value"},
    "effect.update_param_value",
    "Update Effect Param Value",
    "Updates one existing editable effect parameter value through Project Core without replacing controls or keyframes.",
    EffectUpdateParamValueSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(arguments.value(), {"effectNodeId", "paramName", "value"}, "$");
      if (!members) {
        return members.error();
      }
      auto effectNodeId = requiredStringMember(arguments.value(), "effectNodeId", "$");
      if (!effectNodeId) {
        return effectNodeId.error();
      }
      auto paramName = requiredStringMember(arguments.value(), "paramName", "$");
      if (!paramName) {
        return paramName.error();
      }
      auto value = requiredMember(arguments.value(), "value", "$");
      if (!value) {
        return value.error();
      }
      auto paramValue = parseParamValue(value.value(), "$.value");
      if (!paramValue) {
        return paramValue.error();
      }

      const foundation::CommandId commandId = context.ids.nextCommandId();
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::UpdateEffectParamValueCommand{
          foundation::NodeId{effectNodeId.value()},
          paramName.value(),
          paramValue.value()
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"effectNodeId\":" << foundation::jsonQuoted(effectNodeId.value())
              << ",\"paramName\":" << foundation::jsonQuoted(paramName.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeEffectConnectPortsTool() {
  return AgentTool{
    foundation::ToolId{"tool_effect_connect_ports"},
    "effect.connect_ports",
    "Connect Effect Ports",
    "Connects explicit graph ports through Project Core.",
    EffectConnectPortsSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(
        arguments.value(),
        {"edgeId", "sourceNodeId", "sourcePort", "targetNodeId", "targetPort", "order"},
        "$"
      );
      if (!members) {
        return members.error();
      }
      auto edgeId = requiredStringMember(arguments.value(), "edgeId", "$");
      if (!edgeId) {
        return edgeId.error();
      }
      auto sourceNodeId = requiredStringMember(arguments.value(), "sourceNodeId", "$");
      if (!sourceNodeId) {
        return sourceNodeId.error();
      }
      auto sourcePort = requiredStringMember(arguments.value(), "sourcePort", "$");
      if (!sourcePort) {
        return sourcePort.error();
      }
      auto targetNodeId = requiredStringMember(arguments.value(), "targetNodeId", "$");
      if (!targetNodeId) {
        return targetNodeId.error();
      }
      auto targetPort = requiredStringMember(arguments.value(), "targetPort", "$");
      if (!targetPort) {
        return targetPort.error();
      }

      std::int64_t order = 0;
      if (arguments.value().isMember("order")) {
        auto parsedOrder = requiredInt64Member(arguments.value(), "order", "$");
        if (!parsedOrder) {
          return parsedOrder.error();
        }
        order = parsedOrder.value();
      }

      const foundation::CommandId commandId = context.ids.nextCommandId();
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::ConnectPortsCommand{
          foundation::EdgeId{edgeId.value()},
          foundation::NodeId{sourceNodeId.value()},
          graph::PortName{sourcePort.value()},
          foundation::NodeId{targetNodeId.value()},
          graph::PortName{targetPort.value()},
          order
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"edgeId\":" << foundation::jsonQuoted(edgeId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeEffectDisconnectPortsTool() {
  return AgentTool{
    foundation::ToolId{"tool_effect_disconnect_ports"},
    "effect.disconnect_ports",
    "Disconnect Effect Ports",
    "Disconnects a graph port edge through Project Core.",
    EffectDisconnectPortsSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(arguments.value(), {"edgeId"}, "$");
      if (!members) {
        return members.error();
      }
      auto edgeId = requiredStringMember(arguments.value(), "edgeId", "$");
      if (!edgeId) {
        return edgeId.error();
      }

      const foundation::CommandId commandId = context.ids.nextCommandId();
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::DisconnectPortsCommand{foundation::EdgeId{edgeId.value()}}
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"edgeId\":" << foundation::jsonQuoted(edgeId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeRenderPlanInspectTool() {
  return AgentTool{
    foundation::ToolId{"tool_render_plan_inspect"},
    "render_plan.inspect",
    "Inspect RenderPlan",
    "Returns the current RenderPlan summary through Project Query APIs.",
    RenderPlanInspectSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(arguments.value(), {}, "$");
      if (!members) {
        return members.error();
      }
      auto query = context.queries.query(project::InspectRenderPlanQuery{});
      if (!query) {
        return query.error();
      }
      const auto* renderPlanResult = std::get_if<project::RenderPlanInspectResult>(&query.value());
      if (renderPlanResult == nullptr) {
        return foundation::Error{
          "agent.render_plan_inspect_result_missing",
          "RenderPlan inspect query returned the wrong result type."
        };
      }

      std::ostringstream payload;
      writeRenderPlanInspectJson(payload, *renderPlanResult);
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        renderPlanResult->revision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeRuntimeInspectDiagnosticsTool() {
  return AgentTool{
    foundation::ToolId{"tool_runtime_inspect_diagnostics"},
    "runtime.inspect_diagnostics",
    "Inspect Runtime Diagnostics",
    "Returns current runtime diagnostic summaries through Project Query APIs.",
    RuntimeInspectDiagnosticsSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(arguments.value(), {}, "$");
      if (!members) {
        return members.error();
      }
      auto query = context.queries.query(project::InspectRuntimeDiagnosticsQuery{});
      if (!query) {
        return query.error();
      }
      const auto* diagnosticsResult = std::get_if<project::RuntimeInspectDiagnosticsResult>(&query.value());
      if (diagnosticsResult == nullptr) {
        return foundation::Error{
          "agent.runtime_inspect_diagnostics_result_missing",
          "Runtime diagnostics query returned the wrong result type."
        };
      }

      std::ostringstream payload;
      writeRuntimeInspectDiagnosticsJson(payload, *diagnosticsResult);
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        diagnosticsResult->revision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeNoteCreateTool() {
  return AgentTool{
    foundation::ToolId{"tool_note_create"},
    "note.create",
    "Create Note",
    "Creates a project note through Project Core.",
    NoteCreateSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(arguments.value(), {"title", "markdown"}, "$");
      if (!members) {
        return members.error();
      }
      auto title = requiredStringMember(arguments.value(), "title", "$");
      if (!title) {
        return title.error();
      }
      auto markdown = requiredStringMember(arguments.value(), "markdown", "$");
      if (!markdown) {
        return markdown.error();
      }

      const foundation::CommandId commandId = context.ids.nextCommandId();
      const foundation::NodeId noteNodeId = context.ids.nextNodeId("note");
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::CreateNoteCommand{
          noteNodeId,
          timeline::NotePayload{title.value(), markdown.value()}
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"noteNodeId\":" << foundation::jsonQuoted(noteNodeId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

AgentTool makeNoteUpdateTool() {
  return AgentTool{
    foundation::ToolId{"tool_note_update"},
    "note.update",
    "Update Note",
    "Updates an existing project note through Project Core.",
    NoteUpdateSchema,
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto arguments = parseArguments(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto members = requireOnlyMembers(arguments.value(), {"nodeId", "title", "markdown"}, "$");
      if (!members) {
        return members.error();
      }
      auto nodeId = requiredStringMember(arguments.value(), "nodeId", "$");
      if (!nodeId) {
        return nodeId.error();
      }
      auto title = requiredStringMember(arguments.value(), "title", "$");
      if (!title) {
        return title.error();
      }
      auto markdown = requiredStringMember(arguments.value(), "markdown", "$");
      if (!markdown) {
        return markdown.error();
      }

      const foundation::CommandId commandId = context.ids.nextCommandId();
      const foundation::NodeId noteNodeId{nodeId.value()};
      auto command = context.commands.apply(project::ProjectCommandEnvelope{
        commandId,
        call.projectId,
        call.expectedRevision,
        project::CommandSource{project::CommandSourceKind::Agent, call.runId, "agent"},
        project::UpdateNoteCommand{
          noteNodeId,
          timeline::NotePayload{title.value(), markdown.value()}
        }
      });
      if (!command) {
        return command.error();
      }

      std::ostringstream payload;
      payload << '{'
              << "\"commandId\":" << foundation::jsonQuoted(commandId.value())
              << ",\"noteNodeId\":" << foundation::jsonQuoted(noteNodeId.value())
              << ",\"revision\":" << foundation::jsonQuoted(command.value().afterRevision.value())
              << '}';
      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        command.value().afterRevision,
        payload.str(),
        {}
      };
    }
  };
}

} // namespace grapple::agent
