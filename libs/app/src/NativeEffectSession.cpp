#include <grapple/app/NativeEffectSession.hpp>

#include <grapple/graph/GraphNode.hpp>
#include <grapple/timeline/EffectPayload.hpp>

#include <algorithm>
#include <utility>
#include <variant>

namespace grapple::app {

NativeEffectSession::NativeEffectSession(NativeProjectSession& project, NativeProjectCommandWriter& commandWriter)
  : project_{project},
    commandWriter_{commandWriter} {}

foundation::Result<storage::ProjectPackageSessionResult> NativeEffectSession::setNumericParam(
  foundation::NodeId effectNodeId,
  std::string paramName,
  double value,
  project::CommandSource source
) {
  if (paramName.empty()) {
    return foundation::Error{"effect.param_name_empty", "Effect parameter name must not be empty."};
  }

  const auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  const graph::GraphNode* effectNode = snapshot.value().graph.findNode(effectNodeId);
  if (effectNode == nullptr || effectNode->kind != graph::NodeKind::Effect) {
    return foundation::Error{"effect.node_invalid", "Effect node is missing or invalid."};
  }

  const auto* effectPayload = std::get_if<timeline::EffectPayload>(&effectNode->payload);
  if (effectPayload == nullptr) {
    return foundation::Error{"effect.payload_invalid", "Effect node must carry an effect payload."};
  }

  timeline::ParamSet params = effectPayload->params;
  auto param = std::find_if(params.values.begin(), params.values.end(), [&](const timeline::Param& current) {
    return current.name == paramName;
  });
  if (param == params.values.end()) {
    return foundation::Error{"effect.param_missing", "Effect does not define parameter " + paramName + "."};
  }
  if (!std::holds_alternative<double>(param->value)) {
    return foundation::Error{"effect.param_not_numeric", "Effect parameter " + paramName + " is not numeric."};
  }

  param->value = value;
  return commandWriter_.apply(
    project::SetEffectParamsCommand{effectNode->id, std::move(params)},
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
