#include <grapple/project/ProjectCommand.hpp>

#include <type_traits>

namespace grapple::project {

CommandKind commandKind(const ProjectCommand& command) {
  return std::visit(
    [](const auto& typedCommand) -> CommandKind {
      using Command = std::decay_t<decltype(typedCommand)>;
      if constexpr (std::is_same_v<Command, RegisterAssetCommand>) {
        return CommandKind::RegisterAsset;
      } else if constexpr (std::is_same_v<Command, CreateCompositionCommand>) {
        return CommandKind::CreateComposition;
      } else if constexpr (std::is_same_v<Command, CreateTrackCommand>) {
        return CommandKind::CreateTrack;
      } else if constexpr (std::is_same_v<Command, CreateClipCommand>) {
        return CommandKind::CreateClip;
      } else if constexpr (std::is_same_v<Command, MoveClipCommand>) {
        return CommandKind::MoveClip;
      } else if constexpr (std::is_same_v<Command, TrimClipCommand>) {
        return CommandKind::TrimClip;
      } else if constexpr (std::is_same_v<Command, UpdateClipCommand>) {
        return CommandKind::UpdateClip;
      } else if constexpr (std::is_same_v<Command, DeleteClipCommand>) {
        return CommandKind::DeleteClip;
      } else if constexpr (std::is_same_v<Command, CreateCameraCommand>) {
        return CommandKind::CreateCamera;
      } else if constexpr (std::is_same_v<Command, UpdateCameraCommand>) {
        return CommandKind::UpdateCamera;
      } else if constexpr (std::is_same_v<Command, CreateEffectCommand>) {
        return CommandKind::CreateEffect;
      } else if constexpr (std::is_same_v<Command, DeleteEffectCommand>) {
        return CommandKind::DeleteEffect;
      } else if constexpr (std::is_same_v<Command, ConnectPortsCommand>) {
        return CommandKind::ConnectPorts;
      } else if constexpr (std::is_same_v<Command, DisconnectPortsCommand>) {
        return CommandKind::DisconnectPorts;
      } else if constexpr (std::is_same_v<Command, UpdateEffectParamsCommand>) {
        return CommandKind::UpdateEffectParams;
      } else if constexpr (std::is_same_v<Command, UpdateEffectParamValueCommand>) {
        return CommandKind::UpdateEffectParamValue;
      } else if constexpr (std::is_same_v<Command, UpsertEffectParamKeyframeCommand>) {
        return CommandKind::UpsertEffectParamKeyframe;
      } else if constexpr (std::is_same_v<Command, DeleteEffectParamKeyframeCommand>) {
        return CommandKind::DeleteEffectParamKeyframe;
      } else if constexpr (std::is_same_v<Command, CreateNoteCommand>) {
        return CommandKind::CreateNote;
      } else if constexpr (std::is_same_v<Command, UpdateNoteCommand>) {
        return CommandKind::UpdateNote;
      } else if constexpr (std::is_same_v<Command, RestoreSnapshotCommand>) {
        return CommandKind::RestoreSnapshot;
      }
    },
    command
  );
}

} // namespace grapple::project
