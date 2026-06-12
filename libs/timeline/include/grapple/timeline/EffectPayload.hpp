#pragma once

#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/timeline/ParamValue.hpp>

#include <optional>
#include <string>
#include <vector>

namespace grapple::timeline {

enum class EffectImplementationKind {
  Builtin,
  Python,
  Shader
};

enum class EffectSourceKind {
  InlineSource,
  AssetSource
};

struct EffectSource {
  EffectSourceKind kind = EffectSourceKind::InlineSource;
  std::string language;
  std::string inlineSource;
  std::optional<foundation::AssetId> sourceAssetId;
  foundation::Hash256 sourceHash;
};

struct EffectImplementation {
  EffectImplementationKind kind = EffectImplementationKind::Builtin;
  std::string entrypoint;
  EffectSource source;
};

struct EffectPort {
  std::string name;
};

struct EffectPortSet {
  std::vector<EffectPort> inputs;
  std::vector<EffectPort> outputs;
};

struct Param {
  std::string name;
  ParamValue value;
};

struct ParamSet {
  std::vector<Param> values;
};

struct EffectPayload {
  std::string displayName;
  EffectImplementation implementation;
  EffectPortSet ports;
  ParamSet params;
  foundation::TimeRange activeRange;
};

} // namespace grapple::timeline

