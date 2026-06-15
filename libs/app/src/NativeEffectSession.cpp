#include <grapple/app/NativeEffectSession.hpp>

#include <grapple/graph/GraphNode.hpp>

#include <algorithm>
#include <utility>
#include <variant>

namespace grapple::app {

namespace {

bool sameParamValueType(const timeline::ParamValue& left, const timeline::ParamValue& right) {
  return left.index() == right.index();
}

foundation::Result<const timeline::Param*> findEffectParam(
  const project::ProjectSnapshot& snapshot,
  const foundation::NodeId& effectNodeId,
  const std::string& paramName
) {
  const graph::GraphNode* effect = snapshot.graph.findNode(effectNodeId);
  if (effect == nullptr || effect->kind != graph::NodeKind::Effect) {
    return foundation::Error{"project.effect_missing", "Effect param values can only be set on an existing effect node."};
  }

  const auto* payload = std::get_if<timeline::EffectPayload>(&effect->payload);
  if (payload == nullptr) {
    return foundation::Error{"project.effect_payload_invalid", "Effect node must carry an effect payload."};
  }

  const auto param = std::find_if(payload->params.values.begin(), payload->params.values.end(), [&](const timeline::Param& current) {
    return current.name == paramName;
  });
  if (param == payload->params.values.end()) {
    return foundation::Error{"project.effect_param_missing", "Effect param value command requires an existing effect parameter."};
  }

  return &*param;
}

} // namespace

NativeEffectSession::NativeEffectSession(NativeProjectSession& project, NativeProjectCommandWriter& commandWriter)
  : project_{project},
    commandWriter_{commandWriter} {}

foundation::Result<NativeEffectParamValueResult> NativeEffectSession::setParamValue(
  foundation::NodeId effectNodeId,
  std::string paramName,
  timeline::ParamValue value,
  project::CommandSource source
) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  auto currentParam = findEffectParam(snapshot.value(), effectNodeId, paramName);
  if (!currentParam) {
    return currentParam.error();
  }
  if (!sameParamValueType(currentParam.value()->value, value)) {
    return foundation::Error{"project.effect_param_value_type_mismatch", "Effect param value must match the existing parameter value type."};
  }
  if (currentParam.value()->value == value) {
    return NativeEffectParamValueResult{
      false,
      std::move(snapshot.value()),
      std::nullopt
    };
  }

  auto committed = commandWriter_.apply(
    project::UpdateEffectParamValueCommand{
      std::move(effectNodeId),
      std::move(paramName),
      std::move(value)
    },
    std::move(source)
  );
  if (!committed) {
    return committed.error();
  }
  storage::ProjectPackageSessionResult committedValue = std::move(committed.value());

  return NativeEffectParamValueResult{
    true,
    committedValue.snapshot,
    std::move(committedValue)
  };
}

foundation::Result<storage::ProjectPackageSessionResult> NativeEffectSession::upsertParamKeyframe(
  foundation::NodeId effectNodeId,
  std::string paramName,
  timeline::Param::Keyframe keyframe,
  project::CommandSource source
) {
  return commandWriter_.apply(
    project::UpsertEffectParamKeyframeCommand{
      std::move(effectNodeId),
      std::move(paramName),
      std::move(keyframe)
    },
    std::move(source)
  );
}

foundation::Result<storage::ProjectPackageSessionResult> NativeEffectSession::deleteParamKeyframe(
  foundation::NodeId effectNodeId,
  std::string paramName,
  foundation::KeyframeId keyframeId,
  project::CommandSource source
) {
  return commandWriter_.apply(
    project::DeleteEffectParamKeyframeCommand{
      std::move(effectNodeId),
      std::move(paramName),
      std::move(keyframeId)
    },
    std::move(source)
  );
}

foundation::Result<storage::ProjectPackageSessionResult> NativeEffectSession::deleteEffect(
  foundation::NodeId effectNodeId,
  project::CommandSource source
) {
  return commandWriter_.apply(
    project::DeleteEffectCommand{std::move(effectNodeId)},
    std::move(source)
  );
}

} // namespace grapple::app
