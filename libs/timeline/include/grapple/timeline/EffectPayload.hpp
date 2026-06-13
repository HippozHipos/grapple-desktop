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

  friend bool operator==(const EffectSource&, const EffectSource&) = default;
};

struct EffectModelDependency {
  foundation::ModelId modelId;
  foundation::Hash256 versionHash;

  friend bool operator==(const EffectModelDependency&, const EffectModelDependency&) = default;
};

struct EffectImplementation {
  EffectImplementationKind kind = EffectImplementationKind::Builtin;
  std::string entrypoint;
  EffectSource source;

  friend bool operator==(const EffectImplementation&, const EffectImplementation&) = default;
};

struct EffectPort {
  std::string name;

  friend bool operator==(const EffectPort&, const EffectPort&) = default;
};

struct EffectPortSet {
  std::vector<EffectPort> inputs;
  std::vector<EffectPort> outputs;

  friend bool operator==(const EffectPortSet&, const EffectPortSet&) = default;
};

struct Param {
  std::string name;
  ParamValue value;

  struct NumericControl {
    double min = 0.0;
    double max = 1.0;
    std::optional<double> step;

    friend bool operator==(const NumericControl&, const NumericControl&) = default;
  };

  struct Control {
    std::string label;
    std::optional<NumericControl> numeric;

    friend bool operator==(const Control&, const Control&) = default;
  };

  Control control;

  friend bool operator==(const Param&, const Param&) = default;
};

struct ParamSet {
  std::vector<Param> values;

  friend bool operator==(const ParamSet&, const ParamSet&) = default;
};

struct EffectPayload {
  std::string displayName;
  EffectImplementation implementation;
  EffectPortSet ports;
  ParamSet params;
  foundation::TimeRange activeRange;
  std::vector<EffectModelDependency> modelDependencies;

  friend bool operator==(const EffectPayload&, const EffectPayload&) = default;
};

} // namespace grapple::timeline
