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
      } else if constexpr (std::is_same_v<Command, CreateCameraCommand>) {
        return CommandKind::CreateCamera;
      } else if constexpr (std::is_same_v<Command, CreateEffectCommand>) {
        return CommandKind::CreateEffect;
      } else if constexpr (std::is_same_v<Command, SetEffectParamsCommand>) {
        return CommandKind::SetEffectParams;
      } else if constexpr (std::is_same_v<Command, RestoreSnapshotCommand>) {
        return CommandKind::RestoreSnapshot;
      }
    },
    command
  );
}

} // namespace grapple::project
