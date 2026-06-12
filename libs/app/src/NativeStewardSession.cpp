#include <grapple/app/NativeStewardSession.hpp>

#include <grapple/foundation/Hash.hpp>
#include <grapple/graph/GraphNode.hpp>
#include <grapple/timeline/EffectPayload.hpp>

#include <optional>
#include <string>
#include <variant>

namespace grapple::app {

namespace {

project::CommandSource stewardSource() {
  return project::CommandSource{
    project::CommandSourceKind::Agent,
    foundation::RunId{"run_local_steward"},
    "steward"
  };
}

foundation::Result<void> ensureCameraCanReceiveTransformEffect(
  const project::ProjectSnapshot& snapshot,
  const foundation::NodeId& cameraNodeId
) {
  const graph::GraphNode* selectedNode = snapshot.graph.findNode(cameraNodeId);
  if (selectedNode == nullptr || selectedNode->kind != graph::NodeKind::Camera) {
    return foundation::Error{"steward.selected_node_not_camera", "Camera Transform requires a selected camera node."};
  }

  for (const graph::GraphEdge& edge : snapshot.graph.edges()) {
    if (!edge.enabled ||
        edge.kind != graph::EdgeKind::Targets ||
        edge.targetNodeId != cameraNodeId) {
      continue;
    }

    const graph::GraphNode* effectNode = snapshot.graph.findNode(edge.sourceNodeId);
    if (effectNode == nullptr || effectNode->kind != graph::NodeKind::Effect) {
      return foundation::Error{"steward.effect_node_invalid", "Camera target edge points to a missing or invalid effect node."};
    }

    const auto* effectPayload = std::get_if<timeline::EffectPayload>(&effectNode->payload);
    if (effectPayload == nullptr) {
      return foundation::Error{"steward.effect_payload_invalid", "Camera target effect node must carry an effect payload."};
    }

    if (effectPayload->implementation.kind == timeline::EffectImplementationKind::Builtin &&
        effectPayload->implementation.entrypoint == "camera_transform") {
      return foundation::Error{"steward.camera_transform_exists", "Selected camera already has a Camera Transform effect."};
    }
  }

  return {};
}

timeline::EffectPayload cameraTransformPayload(foundation::TimeRange activeRange) {
  const std::string effectSource = "builtin:camera_transform";
  return timeline::EffectPayload{
    "Camera Transform",
    timeline::EffectImplementation{
      timeline::EffectImplementationKind::Builtin,
      "camera_transform",
      timeline::EffectSource{
        timeline::EffectSourceKind::InlineSource,
        "builtin",
        effectSource,
        std::nullopt,
        foundation::stableHash(effectSource)
      }
    },
    timeline::EffectPortSet{
      {timeline::EffectPort{"frame"}},
      {timeline::EffectPort{"camera_transform"}}
    },
    timeline::ParamSet{
      {
        timeline::Param{
          "position_x",
          0.0,
          timeline::Param::Control{
            "Position X",
            timeline::Param::NumericControl{-1.0, 1.0, 0.01}
          }
        },
        timeline::Param{
          "position_y",
          0.0,
          timeline::Param::Control{
            "Position Y",
            timeline::Param::NumericControl{-1.0, 1.0, 0.01}
          }
        }
      }
    },
    activeRange
  };
}

} // namespace

NativeStewardSession::NativeStewardSession(NativeProjectSession& project, NativeProjectCommandWriter& commandWriter)
  : project_{project},
    commandWriter_{commandWriter} {}

foundation::Result<storage::ProjectPackageSessionResult> NativeStewardSession::createCameraTransformEffect(
  foundation::NodeId cameraNodeId,
  foundation::TimeRange activeRange
) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  auto targetReady = ensureCameraCanReceiveTransformEffect(snapshot.value(), cameraNodeId);
  if (!targetReady) {
    return targetReady.error();
  }

  return commandWriter_.apply(
    project::CreateEffectCommand{
      commandWriter_.nextNodeId("effect"),
      cameraNodeId,
      commandWriter_.nextEdgeId("effect targets"),
      cameraTransformPayload(activeRange),
      graph::PortName{"camera_transform"},
      graph::PortName{"input"},
      0
    },
    stewardSource()
  );
}

} // namespace grapple::app
