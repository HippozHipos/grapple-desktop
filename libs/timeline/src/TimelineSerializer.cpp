#include <grapple/timeline/TimelineSerializer.hpp>

#include <grapple/foundation/Json.hpp>

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <variant>

namespace grapple::timeline {

namespace {

void writeNumber(std::ostringstream& stream, double value) {
  stream << std::setprecision(17) << value;
}

void writeVec2(std::ostringstream& stream, const foundation::Vec2& value) {
  stream << "{\"x\":";
  writeNumber(stream, value.x);
  stream << ",\"y\":";
  writeNumber(stream, value.y);
  stream << '}';
}

void writeVec3(std::ostringstream& stream, const foundation::Vec3& value) {
  stream << "{\"x\":";
  writeNumber(stream, value.x);
  stream << ",\"y\":";
  writeNumber(stream, value.y);
  stream << ",\"z\":";
  writeNumber(stream, value.z);
  stream << '}';
}

void writeRect(std::ostringstream& stream, const foundation::Rect& value) {
  stream << "{\"x\":";
  writeNumber(stream, value.x);
  stream << ",\"y\":";
  writeNumber(stream, value.y);
  stream << ",\"width\":";
  writeNumber(stream, value.width);
  stream << ",\"height\":";
  writeNumber(stream, value.height);
  stream << '}';
}

const char* clipKindName(ClipKind kind) {
  switch (kind) {
    case ClipKind::Video:
      return "video";
    case ClipKind::Audio:
      return "audio";
    case ClipKind::Image:
      return "image";
  }
  std::abort();
}

const char* effectImplementationKindName(EffectImplementationKind kind) {
  switch (kind) {
    case EffectImplementationKind::Builtin:
      return "builtin";
    case EffectImplementationKind::Python:
      return "python";
    case EffectImplementationKind::Shader:
      return "shader";
  }
  std::abort();
}

const char* effectSourceKindName(EffectSourceKind kind) {
  switch (kind) {
    case EffectSourceKind::InlineSource:
      return "inline_source";
    case EffectSourceKind::AssetSource:
      return "asset_source";
  }
  std::abort();
}

void writeEffectParamsBody(std::ostringstream& stream, const EffectPayload& payload) {
  stream << "\"ports\":{\"inputs\":[";
  for (std::size_t index = 0; index < payload.ports.inputs.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    stream << '{';
    foundation::writeJsonStringProperty(stream, "name", payload.ports.inputs[index].name);
    stream << '}';
  }
  stream << "],\"outputs\":[";
  for (std::size_t index = 0; index < payload.ports.outputs.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    stream << '{';
    foundation::writeJsonStringProperty(stream, "name", payload.ports.outputs[index].name);
    stream << '}';
  }
  stream << "]},\"params\":" << serializeCanonicalParamSet(payload.params);
  stream << ",\"activeRange\":" << serializeCanonicalTimeRange(payload.activeRange);
  std::vector<EffectModelDependency> modelDependencies = payload.modelDependencies;
  std::sort(
    modelDependencies.begin(),
    modelDependencies.end(),
    [](const EffectModelDependency& left, const EffectModelDependency& right) {
      return left.modelId < right.modelId;
    }
  );
  stream << ",\"modelDependencies\":[";
  for (std::size_t index = 0; index < modelDependencies.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    stream << '{';
    foundation::writeJsonStringProperty(stream, "modelId", modelDependencies[index].modelId.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "versionHash", modelDependencies[index].versionHash.toHex());
    stream << '}';
  }
  stream << ']';
}

} // namespace

std::string serializeCanonicalTimeRange(const foundation::TimeRange& range) {
  std::ostringstream stream;
  stream << "{\"start\":";
  writeNumber(stream, range.start.value);
  stream << ",\"end\":";
  writeNumber(stream, range.end.value);
  stream << '}';
  return stream.str();
}

std::string serializeCanonicalTransform(const Transform& transform) {
  std::ostringstream stream;
  stream << "{\"position\":";
  writeVec2(stream, transform.position);
  stream << ",\"scale\":";
  writeVec2(stream, transform.scale);
  stream << ",\"rotationDegrees\":";
  writeNumber(stream, transform.rotationDegrees);
  stream << ",\"opacity\":";
  writeNumber(stream, transform.opacity);
  stream << '}';
  return stream.str();
}

std::string serializeCanonicalParamValue(const ParamValue& value) {
  std::ostringstream stream;
  std::visit(
    [&](const auto& typedValue) {
      using Value = std::decay_t<decltype(typedValue)>;
      if constexpr (std::is_same_v<Value, double>) {
        writeNumber(stream, typedValue);
      } else if constexpr (std::is_same_v<Value, bool>) {
        stream << (typedValue ? "true" : "false");
      } else if constexpr (std::is_same_v<Value, std::string>) {
        stream << foundation::jsonQuoted(typedValue);
      } else if constexpr (std::is_same_v<Value, foundation::Vec2>) {
        writeVec2(stream, typedValue);
      } else if constexpr (std::is_same_v<Value, foundation::Vec3>) {
        writeVec3(stream, typedValue);
      } else if constexpr (std::is_same_v<Value, foundation::Rect>) {
        writeRect(stream, typedValue);
      }
    },
    value
  );
  return stream.str();
}

std::string serializeCanonicalParamKeyframe(const Param::Keyframe& keyframe) {
  std::ostringstream stream;
  stream << '{';
  foundation::writeJsonStringProperty(stream, "id", keyframe.id.value());
  stream << ",\"time\":";
  writeNumber(stream, keyframe.time.value);
  stream << ",\"value\":" << serializeCanonicalParamValue(keyframe.value);
  stream << '}';
  return stream.str();
}

std::string serializeCanonicalParamSet(const ParamSet& params) {
  std::vector<Param> sortedParams = params.values;
  std::sort(sortedParams.begin(), sortedParams.end(), [](const Param& left, const Param& right) {
    return left.name < right.name;
  });

  std::ostringstream stream;
  stream << '[';
  for (std::size_t index = 0; index < sortedParams.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    stream << '{';
    foundation::writeJsonStringProperty(stream, "name", sortedParams[index].name);
    if (!sortedParams[index].control.label.empty()) {
      stream << ',';
      foundation::writeJsonStringProperty(stream, "label", sortedParams[index].control.label);
    }
    if (sortedParams[index].control.numeric.has_value()) {
      stream << ",\"numeric\":{\"min\":";
      writeNumber(stream, sortedParams[index].control.numeric->min);
      stream << ",\"max\":";
      writeNumber(stream, sortedParams[index].control.numeric->max);
      if (sortedParams[index].control.numeric->step.has_value()) {
        stream << ",\"step\":";
        writeNumber(stream, *sortedParams[index].control.numeric->step);
      }
      stream << '}';
    }
    stream << ",\"value\":" << serializeCanonicalParamValue(sortedParams[index].value);
    std::vector<Param::Keyframe> sortedKeyframes = sortedParams[index].keyframes;
    std::sort(sortedKeyframes.begin(), sortedKeyframes.end(), [](const Param::Keyframe& left, const Param::Keyframe& right) {
      if (left.time != right.time) {
        return left.time < right.time;
      }
      return left.id < right.id;
    });
    stream << ",\"keyframes\":[";
    for (std::size_t keyframeIndex = 0; keyframeIndex < sortedKeyframes.size(); ++keyframeIndex) {
      if (keyframeIndex != 0) {
        stream << ',';
      }
      stream << serializeCanonicalParamKeyframe(sortedKeyframes[keyframeIndex]);
    }
    stream << ']';
    stream << '}';
  }
  stream << ']';
  return stream.str();
}

std::string serializeCanonicalCameraPayload(const CameraPayload& payload) {
  std::ostringstream stream;
  stream << '{';
  foundation::writeJsonStringProperty(stream, "name", payload.name);
  stream << ",\"transform\":" << serializeCanonicalTransform(payload.transform);
  stream << ",\"lens\":{\"focalLength\":";
  writeNumber(stream, payload.lens.focalLength);
  stream << "}}";
  return stream.str();
}

std::string serializeCanonicalClipPayload(const ClipPayload& payload) {
  std::ostringstream stream;
  stream << '{';
  foundation::writeJsonStringProperty(stream, "kind", clipKindName(payload.kind));
  stream << ",\"timelineRange\":" << serializeCanonicalTimeRange(payload.timelineRange);
  stream << ",\"sourceRange\":" << serializeCanonicalTimeRange(payload.sourceRange);
  stream << ",\"playbackRate\":";
  writeNumber(stream, payload.playbackRate);
  stream << ',';
  foundation::writeJsonStringProperty(stream, "assetId", payload.assetId.value());
  stream << ",\"transform\":" << serializeCanonicalTransform(payload.transform);
  stream << '}';
  return stream.str();
}

std::string serializeCanonicalEffectImplementation(const EffectImplementation& implementation) {
  std::ostringstream stream;
  stream << '{';
  foundation::writeJsonStringProperty(stream, "kind", effectImplementationKindName(implementation.kind));
  stream << ',';
  foundation::writeJsonStringProperty(stream, "entrypoint", implementation.entrypoint);
  stream << ",\"source\":{";
  foundation::writeJsonStringProperty(stream, "kind", effectSourceKindName(implementation.source.kind));
  stream << ',';
  foundation::writeJsonStringProperty(stream, "language", implementation.source.language);
  stream << ',';
  foundation::writeJsonStringProperty(stream, "inlineSource", implementation.source.inlineSource);
  stream << ",\"sourceAssetId\":";
  if (implementation.source.sourceAssetId.has_value()) {
    stream << foundation::jsonQuoted(implementation.source.sourceAssetId.value().value());
  } else {
    stream << "null";
  }
  stream << ',';
  foundation::writeJsonStringProperty(stream, "sourceHash", implementation.source.sourceHash.toHex());
  stream << "}}";
  return stream.str();
}

std::string serializeCanonicalEffectParams(const EffectPayload& payload) {
  std::ostringstream stream;
  stream << '{';
  foundation::writeJsonStringProperty(stream, "displayName", payload.displayName);
  stream << ',';
  writeEffectParamsBody(stream, payload);
  stream << '}';
  return stream.str();
}

std::string serializeCanonicalEffectPayload(const EffectPayload& payload) {
  std::ostringstream stream;
  stream << '{';
  foundation::writeJsonStringProperty(stream, "displayName", payload.displayName);
  stream << ",\"implementation\":" << serializeCanonicalEffectImplementation(payload.implementation);
  stream << ',';
  writeEffectParamsBody(stream, payload);
  stream << '}';
  return stream.str();
}

} // namespace grapple::timeline
